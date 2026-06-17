#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

#include "../include/ui.h"

typedef struct CancellableHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
    gchar *fake_modman;
    gchar *fake_bin_dir;
    gchar *old_path;
} CancellableHarness;

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
    gint64 deadline = g_get_monotonic_time() + (6 * G_TIME_SPAN_SECOND);

    while (g_get_monotonic_time() < deadline) {
        const char *text = gtk_label_get_text(label);
        if (text != NULL && strstr(text, needle) != NULL) {
            return TRUE;
        }
        g_main_context_iteration(NULL, TRUE);
    }

    return FALSE;
}

static gboolean send_escape_to_window(GtkWidget *window)
{
    GdkEventKey event = {0};
    gboolean handled = FALSE;

    event.type = GDK_KEY_PRESS;
    event.keyval = GDK_KEY_Escape;
    g_signal_emit_by_name(window, "key-press-event", &event, &handled);
    return handled;
}

static gchar *make_fake_modman_stale_script(void)
{
    gchar *path = g_strdup_printf("%s/modman-cancellable-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(path);
    const gchar *script =
        "#!/bin/sh\n"
        "case \" $* \" in\n"
        "*\" -Qp /tmp/slow.pfs\"*) sleep 1; printf 'file\\t/tmp/slow.pfs\\tslow\\tsquashfs\\tlocal\\t12M\\tzstd\\tslow\\t\\n'; exit 0 ;;\n"
        "*\" -Qp /tmp/fast.pfs\"*) printf 'file\\t/tmp/fast.pfs\\tfast\\tsquashfs\\tlocal\\t12M\\tzstd\\tfast\\t\\n'; exit 0 ;;\n"
        "*\" -Ss slow-first \"*) sleep 1; printf 'slow-first.pfs\\t12M\\t2026-01-01\\tSlow first\\tbase\\trepo\\n'; exit 0 ;;\n"
        "*\" -Ss fast-second \"*) printf 'fast-second.pfs\\t12M\\t2026-01-01\\tFast second\\tbase\\trepo\\n'; exit 0 ;;\n"
        "*\" -S /tmp/slow-action.pfs\"*) sleep 1; exit 0 ;;\n"
        "*\" -S /tmp/fast-action.pfs\"*) exit 0 ;;\n"
        "*\" -S /tmp/fail-action.pfs\"*) printf 'backend refused load\\n' >&2; exit 7 ;;\n"
        "*\" -R slow-unload.pfs\"*) sleep 1; exit 0 ;;\n"
        "*\" -R fast-unload.pfs\"*) exit 0 ;;\n"
        "*\" -R fail-unload.pfs\"*) printf 'backend refused unload\\n' >&2; exit 8 ;;\n"
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
    gchar *dir = g_dir_make_tmp("modman-pkexec-XXXXXX", NULL);
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

static ModuleInfo *test_module_new(const char *name, const char *path)
{
    ModuleInfo *module = g_new0(ModuleInfo, 1);

    module->name = g_strdup(name);
    module->path = g_strdup(path);
    module->desc = g_strdup("test module");
    module->version = g_strdup("1.0");
    module->is_local = TRUE;
    return module;
}

static ModuleInfo *test_loaded_module_new(const char *name)
{
    ModuleInfo *module = test_module_new(name, name);

    module->is_local = FALSE;
    module->layer = g_strdup("runtime");
    return module;
}

static void cancellable_harness_init(CancellableHarness *h)
{
    ck_assert_ptr_nonnull(h);
    ck_assert(ensure_gtk_ready());

    h->application = gtk_application_new("org.puppyrus.modman.tests.cancellable", G_APPLICATION_NON_UNIQUE);
    ck_assert_ptr_nonnull(h->application);
    ck_assert(g_application_register(G_APPLICATION(h->application), NULL, NULL));

    h->fake_modman = make_fake_modman_stale_script();
    ck_assert(g_setenv("MODMAN_BIN", h->fake_modman, TRUE));
    h->fake_bin_dir = make_fake_pkexec_dir();
    h->old_path = g_strdup(g_getenv("PATH"));
    {
        gchar *new_path = g_strdup_printf("%s:%s", h->fake_bin_dir, h->old_path != NULL ? h->old_path : "");
        ck_assert(g_setenv("PATH", new_path, TRUE));
        g_free(new_path);
    }

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
    gtk_stack_set_visible_child_name(h->app.stack, "local");
    drain_events();
}

static void cancellable_harness_free(CancellableHarness *h)
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
    if (h->old_path != NULL) {
        g_setenv("PATH", h->old_path, TRUE);
    } else {
        g_unsetenv("PATH");
    }
    if (h->fake_modman != NULL) {
        g_remove(h->fake_modman);
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
}

static GtkListBoxRow *insert_and_select(CancellableHarness *h, ModuleInfo *module)
{
    GtkWidget *row = ui_build_module_row(module);

    g_object_set_data(G_OBJECT(row), "module-info", module);
    gtk_list_box_insert(h->app.list_boxes[2], row, -1);
    gtk_widget_show_all(row);
    gtk_list_box_select_row(h->app.list_boxes[2], GTK_LIST_BOX_ROW(row));
    return GTK_LIST_BOX_ROW(row);
}

static GtkListBoxRow *insert_and_select_in_section(CancellableHarness *h, gint section_index, ModuleInfo *module)
{
    GtkWidget *row = ui_build_module_row(module);

    g_object_set_data(G_OBJECT(row), "module-info", module);
    gtk_list_box_insert(h->app.list_boxes[section_index], row, -1);
    gtk_widget_show_all(row);
    gtk_list_box_select_row(h->app.list_boxes[section_index], GTK_LIST_BOX_ROW(row));
    return GTK_LIST_BOX_ROW(row);
}

START_TEST(test_selection_change_cancels_previous_details_file_info_request)
{
    CancellableHarness h = {0};
    GCancellable *first_op;

    cancellable_harness_init(&h);

    insert_and_select(&h, test_module_new("slow.pfs", "/tmp/slow.pfs"));
    ck_assert_ptr_nonnull(h.app.details_file_info_op);
    first_op = g_object_ref(h.app.details_file_info_op);

    insert_and_select(&h, test_module_new("fast.pfs", "/tmp/fast.pfs"));
    ck_assert(g_cancellable_is_cancelled(first_op));
    ck_assert_ptr_nonnull(h.app.details_file_info_op);
    ck_assert_ptr_ne(h.app.details_file_info_op, first_op);

    g_object_unref(first_op);
    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_cancelling_load_shows_cancelled_and_blocks_stale_success)
{
    CancellableHarness h = {0};
    GtkListBoxRow *row;

    cancellable_harness_init(&h);

    row = insert_and_select(&h, test_module_new("slow-action.pfs", "/tmp/slow-action.pfs"));
    (void)row;
    gtk_button_clicked(h.app.btn_load);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Подключение…");

    ck_assert(send_escape_to_window(h.window));
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Отменено");
    ck_assert_ptr_null(h.app.progress_file_path);

    g_usleep(1500 * 1000);
    drain_events();
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Отменено");

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_successful_load_status_sequence_starts_connecting_and_finishes_ready)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);

    insert_and_select(&h, test_module_new("fast-action.pfs", "/tmp/fast-action.pfs"));
    gtk_button_clicked(h.app.btn_load);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Подключение…");
    ck_assert(wait_until_label_contains(h.app.status_label, "Готово"));

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_successful_unload_status_sequence_starts_disconnecting_and_finishes_ready)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();

    insert_and_select_in_section(&h, 0, test_loaded_module_new("fast-unload.pfs"));
    gtk_button_clicked(h.app.btn_unload);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Отключение…");
    ck_assert(wait_until_label_contains(h.app.status_label, "Готово"));

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_load_backend_failure_uses_neutral_error_status)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);

    insert_and_select(&h, test_module_new("fail-action.pfs", "/tmp/fail-action.pfs"));
    gtk_button_clicked(h.app.btn_load);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Подключение…");
    ck_assert(wait_until_label_contains(h.app.status_label, "Ошибка:"));
    ck_assert(strstr(gtk_label_get_text(h.app.status_label), "backend refused load") != NULL);

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_unload_backend_failure_uses_neutral_error_status)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();

    insert_and_select_in_section(&h, 0, test_loaded_module_new("fail-unload.pfs"));
    gtk_button_clicked(h.app.btn_unload);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Отключение…");
    ck_assert(wait_until_label_contains(h.app.status_label, "Ошибка:"));
    ck_assert(strstr(gtk_label_get_text(h.app.status_label), "backend refused unload") != NULL);

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_newer_load_operation_ignores_older_callback)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);

    insert_and_select(&h, test_module_new("slow-action.pfs", "/tmp/slow-action.pfs"));
    gtk_button_clicked(h.app.btn_load);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Подключение…");

    insert_and_select(&h, test_module_new("fast-action.pfs", "/tmp/fast-action.pfs"));
    gtk_button_clicked(h.app.btn_load);
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Подключение…");

    ck_assert(wait_until_label_contains(h.app.status_label, "Готово"));
    g_usleep(1500 * 1000);
    drain_events();
    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Готово");

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_late_details_file_info_callback_is_ignored_after_selection_change)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);

    insert_and_select(&h, test_module_new("slow.pfs", "/tmp/slow.pfs"));
    insert_and_select(&h, test_module_new("fast.pfs", "/tmp/fast.pfs"));

    ck_assert(wait_until_label_contains(h.app.details.modules, "fast"));
    g_usleep(1500 * 1000);
    drain_events();

    ck_assert_str_eq(gtk_label_get_text(h.app.details.modules), "Модули внутри:\nfast");

    cancellable_harness_free(&h);
}
END_TEST

START_TEST(test_late_search_callback_cannot_overwrite_newer_query_results)
{
    CancellableHarness h = {0};

    cancellable_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "inet");
    drain_events();

    gtk_entry_set_text(GTK_ENTRY(h.app.search_entry), "slow-first");
    drain_events();
    g_usleep(300 * 1000);
    drain_events();

    gtk_entry_set_text(GTK_ENTRY(h.app.search_entry), "fast-second");
    drain_events();
    g_usleep(300 * 1000);
    drain_events();

    ck_assert(wait_until_label_contains(h.app.details.name, "—"));
    ck_assert(wait_until_label_contains(h.app.status_label, "Готово"));
    ck_assert_ptr_nonnull(gtk_list_box_get_row_at_index(h.app.list_boxes[1], 0));
    {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(h.app.list_boxes[1], 0);
        ModuleInfo *module = g_object_get_data(G_OBJECT(row), "module-info");
        ck_assert_ptr_nonnull(module);
        ck_assert_str_eq(module->name, "fast-second.pfs");
    }

    g_usleep(1300 * 1000);
    drain_events();
    {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(h.app.list_boxes[1], 0);
        ModuleInfo *module = g_object_get_data(G_OBJECT(row), "module-info");
        ck_assert_ptr_nonnull(module);
        ck_assert_str_eq(module->name, "fast-second.pfs");
    }

    cancellable_harness_free(&h);
}
END_TEST

static Suite *cancellable_suite(void)
{
    Suite *suite = suite_create("cancellable");
    TCase *tc = tcase_create("details-file-info");

    tcase_add_test(tc, test_selection_change_cancels_previous_details_file_info_request);
    tcase_add_test(tc, test_late_details_file_info_callback_is_ignored_after_selection_change);
    tcase_add_test(tc, test_cancelling_load_shows_cancelled_and_blocks_stale_success);
    tcase_add_test(tc, test_successful_load_status_sequence_starts_connecting_and_finishes_ready);
    tcase_add_test(tc, test_successful_unload_status_sequence_starts_disconnecting_and_finishes_ready);
    tcase_add_test(tc, test_load_backend_failure_uses_neutral_error_status);
    tcase_add_test(tc, test_unload_backend_failure_uses_neutral_error_status);
    tcase_add_test(tc, test_newer_load_operation_ignores_older_callback);
    tcase_add_test(tc, test_late_search_callback_cannot_overwrite_newer_query_results);
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

    suite = cancellable_suite();
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
