#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

#include "../include/ui.h"

void ui_set_toolbar_state(AppState *app, const char *section_id);

typedef struct ConfirmHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
    gchar *fake_modman;
    gchar *fake_bin_dir;
    gchar *old_path;
    gchar *log_path;
} ConfirmHarness;

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

static gchar *make_fake_modman_script(void)
{
    gchar *path = g_strdup_printf("%s/modman-confirm-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(path);
    const gchar *script =
        "#!/bin/sh\n"
        "if [ -n \"${MODMAN_TEST_LOG:-}\" ]; then printf '%s\\n' \"$*\" >> \"$MODMAN_TEST_LOG\"; fi\n"
        "case \" $* \" in\n"
        "*\" -R runtime-unload.pfs\"*) exit 0 ;;\n"
        "*) exit 0 ;;\n"
        "esac\n";

    ck_assert_int_ne(fd, -1);
    close(fd);
    ck_assert(g_file_set_contents(path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(path, 0700), 0);
    return path;
}

static gchar *make_fake_pkexec_dir(void)
{
    gchar *dir = g_dir_make_tmp("modman-confirm-pkexec-XXXXXX", NULL);
    gchar *path;
    const gchar *script =
        "#!/bin/sh\n"
        "exec \"$@\"\n";

    ck_assert_ptr_nonnull(dir);
    path = g_build_filename(dir, "pkexec", NULL);
    ck_assert(g_file_set_contents(path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(path, 0700), 0);
    g_free(path);
    return dir;
}

static ModuleInfo *test_loaded_module_new(const char *name, const char *layer)
{
    ModuleInfo *module = g_new0(ModuleInfo, 1);

    module->name = g_strdup(name);
    module->path = g_strdup(name);
    module->desc = g_strdup("test module");
    module->version = g_strdup("1.0");
    module->layer = g_strdup(layer);
    return module;
}

static void confirm_harness_init(ConfirmHarness *h)
{
    ck_assert_ptr_nonnull(h);
    ck_assert(ensure_gtk_ready());

    h->application = gtk_application_new("org.puppyrus.modman.tests.confirm", G_APPLICATION_NON_UNIQUE);
    ck_assert_ptr_nonnull(h->application);
    ck_assert(g_application_register(G_APPLICATION(h->application), NULL, NULL));

    h->fake_modman = make_fake_modman_script();
    h->log_path = g_strdup_printf("%s/modman-confirm-log-XXXXXX", g_get_tmp_dir());
    {
        gint fd = g_mkstemp(h->log_path);
        ck_assert_int_ne(fd, -1);
        close(fd);
        g_remove(h->log_path);
    }

    ck_assert(g_setenv("MODMAN_BIN", h->fake_modman, TRUE));
    ck_assert(g_setenv("MODMAN_TEST_LOG", h->log_path, TRUE));
    h->fake_bin_dir = make_fake_pkexec_dir();
    h->old_path = g_strdup(g_getenv("PATH"));
    {
        gchar *new_path = g_strdup_printf("%s:%s", h->fake_bin_dir, h->old_path != NULL ? h->old_path : "");
        ck_assert(g_setenv("PATH", new_path, TRUE));
        g_free(new_path);
    }

    h->app.capabilities_detected = TRUE;
    h->app.capabilities.has_modman = TRUE;
    h->app.capabilities.has_aufs = TRUE;
    h->app.capabilities.has_overlayfs = FALSE;
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
}

static void confirm_harness_free(ConfirmHarness *h)
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
    if (h->old_path != NULL) {
        g_setenv("PATH", h->old_path, TRUE);
    } else {
        g_unsetenv("PATH");
    }
    if (h->fake_modman != NULL) {
        g_remove(h->fake_modman);
    }
    if (h->log_path != NULL) {
        g_remove(h->log_path);
    }
    if (h->fake_bin_dir != NULL) {
        gchar *pkexec_path = g_build_filename(h->fake_bin_dir, "pkexec", NULL);
        g_remove(pkexec_path);
        g_rmdir(h->fake_bin_dir);
        g_free(pkexec_path);
    }
    g_free(h->fake_modman);
    g_free(h->fake_bin_dir);
    g_free(h->old_path);
    g_free(h->log_path);
}

static GtkListBoxRow *insert_and_select_in_section(ConfirmHarness *h,
                                                   gint section_index,
                                                   const char *section_id,
                                                   ModuleInfo *module)
{
    GtkWidget *row = ui_build_module_row(module);

    g_object_set_data(G_OBJECT(row), "module-info", module);
    g_object_set_data(G_OBJECT(row), "section-id", (gpointer)section_id);
    gtk_list_box_insert(h->app.list_boxes[section_index], row, -1);
    gtk_widget_show_all(row);
    gtk_list_box_select_row(h->app.list_boxes[section_index], GTK_LIST_BOX_ROW(row));
    drain_events();
    ui_set_toolbar_state(&h->app, section_id);
    return GTK_LIST_BOX_ROW(row);
}

START_TEST(test_system_unload_button_is_disabled_with_protection_tooltip)
{
    ConfirmHarness h = {0};
    gchar *tooltip;

    confirm_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "system");
    drain_events();

    insert_and_select_in_section(&h, 3, "system", test_loaded_module_new("base-system.pfs", "base"));

    ck_assert(!gtk_widget_get_sensitive(GTK_WIDGET(h.app.btn_unload)));
    tooltip = gtk_widget_get_tooltip_text(GTK_WIDGET(h.app.btn_unload));
    ck_assert_str_eq(tooltip, "Системный слой нельзя отключить из этого списка");
    g_free(tooltip);

    confirm_harness_free(&h);
}
END_TEST

START_TEST(test_loaded_after_unload_button_stays_available_and_calls_backend)
{
    ConfirmHarness h = {0};
    gchar *log = NULL;
    gsize log_len = 0;

    confirm_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();

    insert_and_select_in_section(&h, 0, "loaded", test_loaded_module_new("runtime-unload.pfs", "runtime"));

    ck_assert(gtk_widget_get_sensitive(GTK_WIDGET(h.app.btn_unload)));
    gtk_button_clicked(h.app.btn_unload);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Отключение…");
    ck_assert(wait_until_label_contains(h.app.status_label, "Готово"));
    ck_assert(g_file_get_contents(h.log_path, &log, &log_len, NULL));
    ck_assert(strstr(log, "--machine -R runtime-unload.pfs") != NULL);
    g_free(log);

    confirm_harness_free(&h);
}
END_TEST

START_TEST(test_missing_runtime_capability_tooltip_takes_precedence_over_system_protection)
{
    ConfirmHarness h = {0};
    gchar *tooltip;

    confirm_harness_init(&h);
    h.app.capabilities.has_aufs = FALSE;
    h.app.capabilities.has_overlayfs = FALSE;
    h.app.capabilities.layering_mode = CAP_LAYERING_NONE;
    gtk_stack_set_visible_child_name(h.app.stack, "system");
    drain_events();

    insert_and_select_in_section(&h, 3, "system", test_loaded_module_new("base-system.pfs", "base"));

    ck_assert(!gtk_widget_get_sensitive(GTK_WIDGET(h.app.btn_unload)));
    tooltip = gtk_widget_get_tooltip_text(GTK_WIDGET(h.app.btn_unload));
    ck_assert_str_eq(tooltip, "Attach/detach operations are unavailable: critical environment capabilities are missing.");
    ck_assert_str_ne(tooltip, "Системный слой нельзя отключить из этого списка");
    g_free(tooltip);

    confirm_harness_free(&h);
}
END_TEST

static Suite *confirm_suite(void)
{
    Suite *suite = suite_create("confirm");
    TCase *tc = tcase_create("system-protection");

    tcase_add_test(tc, test_system_unload_button_is_disabled_with_protection_tooltip);
    tcase_add_test(tc, test_loaded_after_unload_button_stays_available_and_calls_backend);
    tcase_add_test(tc, test_missing_runtime_capability_tooltip_takes_precedence_over_system_protection);
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

    suite = confirm_suite();
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
