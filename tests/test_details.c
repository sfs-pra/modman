#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

#include "../include/ui.h"

typedef struct DetailsHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
    gchar *fake_modman;
    gchar *log_path;
} DetailsHarness;

static gboolean ensure_gtk_ready(void)
{
    static gboolean initialized = FALSE;

    if (initialized) {
        return TRUE;
    }

    if (!gtk_init_check(NULL, NULL)) {
        return FALSE;
    }

    initialized = TRUE;
    return TRUE;
}

static void drain_events(void)
{
    while (g_main_context_iteration(NULL, FALSE)) {
    }
}

static gboolean wait_until_label_contains(GtkLabel *label, const char *needle)
{
    gint64 deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);

    while (g_get_monotonic_time() < deadline) {
        const char *text = gtk_label_get_text(label);
        if (text != NULL && strstr(text, needle) != NULL) {
            return TRUE;
        }
        g_main_context_iteration(NULL, TRUE);
    }

    return FALSE;
}

static gchar *make_fake_modman_details_script(const gchar *modules,
                                              const gchar *format_detail,
                                              gint status_code)
{
    gchar *path = g_strdup_printf("%s/modman-details-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(path);
    GString *script;

    ck_assert_int_ne(fd, -1);
    close(fd);

    script = g_string_new("#!/bin/sh\n");
    g_string_append(script, "if [ -n \"${MODMAN_TEST_LOG:-}\" ]; then printf '%s\\n' \"$*\" >> \"$MODMAN_TEST_LOG\"; fi\n");
    g_string_append(script, "case \" $* \" in\n");
    g_string_append(script, "*\" -Qp \"*)\n");
    if (status_code == 0) {
        gchar *quoted_modules = g_shell_quote(modules != NULL ? modules : "");
        gchar *quoted_format_detail = g_shell_quote(format_detail != NULL ? format_detail : "");
        g_string_append_printf(script,
                               "printf 'file\\t%s\\tdemo\\tsquashfs\\tlocal\\t12M\\t%s\\t%s\\t\\n' \"$3\" %s %s\n",
                               "%s",
                               "%s",
                               "%s",
                               quoted_format_detail,
                               quoted_modules);
        g_free(quoted_format_detail);
        g_free(quoted_modules);
    } else {
        g_string_append(script, "printf 'error\\tbroken file info\\n'\n");
    }
    g_string_append_printf(script, "exit %d ;;\n", status_code);
    g_string_append(script, "*) exit 0 ;;\nesac\n");

    ck_assert(g_file_set_contents(path, script->str, -1, NULL));
    ck_assert_int_eq(g_chmod(path, 0700), 0);
    g_string_free(script, TRUE);
    return path;
}

static ModuleInfo *test_module_new(const char *name, const char *path, gboolean is_local)
{
    ModuleInfo *module = g_new0(ModuleInfo, 1);

    module->name = g_strdup(name);
    module->path = g_strdup(path);
    module->desc = g_strdup("test module");
    module->version = g_strdup("1.0");
    module->is_local = is_local;
    return module;
}

static void details_harness_init_with_format(DetailsHarness *h,
                                             const gchar *modules,
                                             const gchar *format_detail,
                                             gint status_code)
{
    ck_assert_ptr_nonnull(h);
    ck_assert(ensure_gtk_ready());

    h->application = gtk_application_new("org.puppyrus.modman.tests.details", G_APPLICATION_NON_UNIQUE);
    ck_assert_ptr_nonnull(h->application);
    ck_assert(g_application_register(G_APPLICATION(h->application), NULL, NULL));

    h->fake_modman = make_fake_modman_details_script(modules, format_detail, status_code);
    h->log_path = g_strdup_printf("%s/modman-details-log-XXXXXX", g_get_tmp_dir());
    {
        gint fd = g_mkstemp(h->log_path);
        ck_assert_int_ne(fd, -1);
        close(fd);
        g_remove(h->log_path);
    }
    ck_assert(g_setenv("MODMAN_BIN", h->fake_modman, TRUE));
    ck_assert(g_setenv("MODMAN_TEST_LOG", h->log_path, TRUE));

    h->app.capabilities_detected = TRUE;
    h->app.capabilities.has_modman = FALSE;
    h->app.capabilities.has_aufs = TRUE;
    h->app.capabilities.has_pkexec = TRUE;
    h->app.capabilities.layering_mode = CAP_LAYERING_AUFS;

    h->window = ui_build_window(&h->app);
    ck_assert_ptr_nonnull(h->window);
    g_signal_handlers_disconnect_matched(h->app.stack,
                                         G_SIGNAL_MATCH_DATA,
                                         0,
                                         0,
                                         NULL,
                                         NULL,
                                         &h->app);
    drain_events();
    h->app.capabilities.has_modman = TRUE;
}

static void details_harness_init(DetailsHarness *h, const gchar *modules, gint status_code)
{
    details_harness_init_with_format(h, modules, "squashfs zstd 18", status_code);
}

static void details_harness_free(DetailsHarness *h)
{
    if (h == NULL) {
        return;
    }

    if (h->window != NULL) {
        gtk_widget_destroy(h->window);
        h->window = NULL;
    }
    if (h->application != NULL) {
        g_object_unref(h->application);
        h->application = NULL;
    }

    g_unsetenv("MODMAN_BIN");
    g_unsetenv("MODMAN_TEST_LOG");
    if (h->fake_modman != NULL) {
        g_remove(h->fake_modman);
    }
    if (h->log_path != NULL) {
        g_remove(h->log_path);
    }
    g_free(h->fake_modman);
    g_free(h->log_path);
}

static void select_module_in_section(DetailsHarness *h, gint section, ModuleInfo *module)
{
    GtkWidget *row;

    ck_assert_ptr_nonnull(h);
    ck_assert_ptr_nonnull(module);
    ck_assert_int_ge(section, 0);
    ck_assert_int_lt(section, 4);
    ck_assert_ptr_nonnull(h->app.list_boxes[section]);

    gtk_stack_set_visible_child_name(h->app.stack,
                                     section == 0 ? "loaded" :
                                     section == 1 ? "inet" :
                                     section == 2 ? "local" : "system");
    drain_events();

    row = ui_build_module_row(module);
    g_object_set_data(G_OBJECT(row), "module-info", module);
    gtk_list_box_insert(h->app.list_boxes[section], row, -1);
    gtk_widget_show_all(row);
    gtk_list_box_select_row(h->app.list_boxes[section], GTK_LIST_BOX_ROW(row));
}

static gchar *read_log(DetailsHarness *h)
{
    gchar *contents = NULL;

    if (h->log_path == NULL || !g_file_get_contents(h->log_path, &contents, NULL, NULL)) {
        return g_strdup("");
    }

    return contents;
}

START_TEST(test_local_details_show_sorted_nested_modules)
{
    DetailsHarness h = {0};

    details_harness_init(&h, "beta alpha", 0);
    select_module_in_section(&h, 2, test_module_new("demo.pfs", "/tmp/demo.pfs", TRUE));

    ck_assert(wait_until_label_contains(h.app.details.modules, "Модули внутри:"));
    ck_assert_str_eq(gtk_label_get_text(h.app.details.modules), "Модули внутри:\nalpha\nbeta");

    details_harness_free(&h);
}
END_TEST

START_TEST(test_local_details_append_file_format_detail_to_size)
{
    const char *details[] = {
        "squashfs zstd 18",
        "squashfs xz",
        "squashfs gz",
        "erofs lz4",
    };

    for (guint i = 0U; i < G_N_ELEMENTS(details); i++) {
        DetailsHarness h = {0};
        ModuleInfo *module;

        details_harness_init_with_format(&h, "alpha", details[i], 0);
        module = test_module_new("demo.pfs", "/tmp/demo.pfs", TRUE);
        module->size_mb = 12.0;
        select_module_in_section(&h, 2, module);

        ck_assert(wait_until_label_contains(h.app.details.size, details[i]));
        ck_assert_ptr_nonnull(strstr(gtk_label_get_text(h.app.details.size), "12"));

        details_harness_free(&h);
    }
}
END_TEST

START_TEST(test_system_details_show_sorted_nested_modules)
{
    DetailsHarness h = {0};

    details_harness_init(&h, "beta alpha", 0);
    select_module_in_section(&h, 3, test_module_new("system.pfs", "/tmp/system.pfs", FALSE));

    ck_assert(wait_until_label_contains(h.app.details.modules, "Модули внутри:"));
    ck_assert_str_eq(gtk_label_get_text(h.app.details.modules), "Модули внутри:\nalpha\nbeta");

    details_harness_free(&h);
}
END_TEST

START_TEST(test_loaded_and_online_never_show_nested_modules_or_inspect_file_info)
{
    DetailsHarness h = {0};
    gchar *log_text;

    details_harness_init(&h, "beta alpha", 0);
    select_module_in_section(&h, 0, test_module_new("loaded.pfs", "/tmp/loaded.pfs", FALSE));
    drain_events();
    ck_assert_ptr_nonnull(h.app.details.modules);
    ck_assert(!gtk_widget_get_visible(GTK_WIDGET(h.app.details.modules)));

    select_module_in_section(&h, 1, test_module_new("online.pfs", "/tmp/online.pfs", FALSE));
    drain_events();
    ck_assert(!gtk_widget_get_visible(GTK_WIDGET(h.app.details.modules)));

    log_text = read_log(&h);
    ck_assert_msg(strstr(log_text, "-Qp") == NULL, "unexpected file-info inspection: %s", log_text);
    g_free(log_text);

    details_harness_free(&h);
}
END_TEST

START_TEST(test_empty_nested_modules_use_exact_placeholder_without_stale_list)
{
    DetailsHarness h = {0};

    details_harness_init(&h, "", 0);
    gtk_label_set_text(h.app.details.modules, "Модули внутри:\nstale");
    select_module_in_section(&h, 2, test_module_new("empty.pfs", "/tmp/empty.pfs", TRUE));

    ck_assert(wait_until_label_contains(h.app.details.modules, "Модули внутри:"));
    ck_assert_str_eq(gtk_label_get_text(h.app.details.modules), "Модули внутри: —");

    details_harness_free(&h);
}
END_TEST

START_TEST(test_file_info_failure_uses_neutral_inline_text_without_error_infobar)
{
    DetailsHarness h = {0};

    details_harness_init(&h, "", 1);
    gtk_widget_show(h.app.info_bar);
    select_module_in_section(&h, 2, test_module_new("broken.pfs", "/tmp/broken.pfs", TRUE));

    ck_assert(wait_until_label_contains(h.app.details.modules, "Не удалось прочитать состав модуля"));
    ck_assert_str_eq(gtk_label_get_text(h.app.details.modules), "Не удалось прочитать состав модуля");
    ck_assert(!gtk_widget_get_visible(h.app.info_bar));

    details_harness_free(&h);
}
END_TEST

static Suite *details_suite(void)
{
    Suite *suite = suite_create("details");
    TCase *tc = tcase_create("nested-modules");

    tcase_add_test(tc, test_local_details_show_sorted_nested_modules);
    tcase_add_test(tc, test_local_details_append_file_format_detail_to_size);
    tcase_add_test(tc, test_system_details_show_sorted_nested_modules);
    tcase_add_test(tc, test_loaded_and_online_never_show_nested_modules_or_inspect_file_info);
    tcase_add_test(tc, test_empty_nested_modules_use_exact_placeholder_without_stale_list);
    tcase_add_test(tc, test_file_info_failure_uses_neutral_inline_text_without_error_infobar);
    suite_add_tcase(suite, tc);

    return suite;
}

int main(void)
{
    int failed;
    Suite *suite;
    SRunner *runner;

    if (!ensure_gtk_ready()) {
        return 77;
    }

    suite = details_suite();
    runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return failed == 0 ? 0 : 1;
}
#else
#include <stdio.h>

int main(void)
{
    puts("SKIP: check or GTK headers unavailable");
    return 77;
}
#endif
