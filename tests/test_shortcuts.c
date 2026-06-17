#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <string.h>
#include <unistd.h>

#include "../include/ui.h"

typedef struct SearchHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
    gchar *fake_modman;
    gchar *log_path;
} SearchHarness;

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

static gboolean wait_until_label_is(GtkLabel *label, const char *expected)
{
    gint64 deadline = g_get_monotonic_time() + (6 * G_TIME_SPAN_SECOND);

    while (g_get_monotonic_time() < deadline) {
        const char *text = gtk_label_get_text(label);
        if (g_strcmp0(text, expected) == 0) {
            return TRUE;
        }
        g_main_context_iteration(NULL, TRUE);
    }

    return FALSE;
}

static guint list_row_count(GtkListBox *list_box)
{
    GList *children;
    guint count;

    ck_assert_ptr_nonnull(list_box);
    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    count = (guint)g_list_length(children);
    g_list_free(children);
    return count;
}

static const char *list_placeholder_text(GtkListBox *list_box)
{
    GtkWidget *placeholder;

    ck_assert_ptr_nonnull(list_box);
    placeholder = g_object_get_data(G_OBJECT(list_box), "empty-placeholder");
    ck_assert_ptr_nonnull(placeholder);
    ck_assert(GTK_IS_LABEL(placeholder));
    return gtk_label_get_text(GTK_LABEL(placeholder));
}

static gchar *make_fake_modman_script(const gchar *log_path)
{
    gchar *path = g_strdup_printf("%s/modman-search-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(path);
    gchar *script;

    ck_assert_int_ne(fd, -1);
    close(fd);

    script = g_strdup_printf(
        "#!/usr/bin/env bash\n"
        "set -Eeuo pipefail\n"
        "printf '%%s\\n' \"$*\" >> '%s'\n"
        "if [[ \"$1\" == \"-Ss\" ]]; then\n"
        "  case \"${2-}\" in\n"
        "    alpha) printf 'alpha.pfs\\t12M\\t2026-01-01\\tAlpha module\\tbase\\trepo\\n' ; exit 0 ;;\n"
        "    none) exit 0 ;;\n"
        "    error) printf 'mirror refused query\\n' >&2; exit 7 ;;\n"
        "    *) printf 'generic.pfs\\t8M\\t2026-01-01\\tGeneric module\\tbase\\trepo\\n' ; exit 0 ;;\n"
        "  esac\n"
        "fi\n"
        "exit 0\n",
        log_path);

    ck_assert(g_file_set_contents(path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(path, 0700), 0);
    g_free(script);
    return path;
}

static GtkWidget *find_widget_by_name(GtkWidget *root, const char *name)
{
    GtkWidget *match = NULL;

    if (root == NULL || name == NULL) {
        return NULL;
    }

    if (g_strcmp0(gtk_widget_get_name(root), name) == 0) {
        return root;
    }

    if (GTK_IS_CONTAINER(root)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(root));

        for (GList *iter = children; iter != NULL && match == NULL; iter = iter->next) {
            match = find_widget_by_name(GTK_WIDGET(iter->data), name);
        }
        g_list_free(children);
    }

    return match;
}

static void search_harness_init(SearchHarness *h)
{
    ck_assert_ptr_nonnull(h);
    ck_assert(ensure_gtk_ready());

    h->log_path = g_strdup_printf("%s/modman-search-log-XXXXXX", g_get_tmp_dir());
    {
        gint fd = g_mkstemp(h->log_path);
        ck_assert_int_ne(fd, -1);
        close(fd);
    }
    h->fake_modman = make_fake_modman_script(h->log_path);
    ck_assert(g_setenv("MODMAN_BIN", h->fake_modman, TRUE));

    h->application = gtk_application_new("org.puppyrus.modman.tests.search", G_APPLICATION_NON_UNIQUE);
    ck_assert_ptr_nonnull(h->application);
    ck_assert(g_application_register(G_APPLICATION(h->application), NULL, NULL));

    h->app.capabilities_detected = TRUE;
    h->app.capabilities.has_modman = TRUE;
    h->app.capabilities.has_pfsinfo = TRUE;
    h->app.capabilities.has_aufs = TRUE;
    h->app.capabilities.has_pkexec = TRUE;
    h->app.capabilities.layering_mode = CAP_LAYERING_AUFS;
    h->app.conf.modman_bin = g_strdup(h->fake_modman);

    h->window = ui_build_window(&h->app);
    ck_assert_ptr_nonnull(h->window);
    drain_events();
    g_file_set_contents(h->log_path, "", 0, NULL);
}

static void search_harness_free(SearchHarness *h)
{
    if (h == NULL) {
        return;
    }
    if (h->window != NULL) {
        gtk_widget_destroy(h->window);
    }
    if (h->application != NULL) {
        g_object_unref(h->application);
    }
    if (h->fake_modman != NULL) {
        g_remove(h->fake_modman);
    }
    if (h->log_path != NULL) {
        g_remove(h->log_path);
    }
    g_unsetenv("MODMAN_BIN");
    g_free(h->app.conf.modman_bin);
    g_free(h->fake_modman);
    g_free(h->log_path);
}

static void switch_to_inet(SearchHarness *h)
{
    gtk_stack_set_visible_child_name(h->app.stack, "inet");
    drain_events();
}

static void set_search_text(SearchHarness *h, const char *text)
{
    gtk_entry_set_text(GTK_ENTRY(h->app.search_entry), text);
    drain_events();
}

static gchar *read_log(SearchHarness *h)
{
    gchar *content = NULL;
    gsize len = 0;

    ck_assert(g_file_get_contents(h->log_path, &content, &len, NULL));
    return content;
}

START_TEST(test_empty_online_query_prompts_without_backend_search)
{
    SearchHarness h = {0};
    gchar *log;

    search_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();
    g_file_set_contents(h.log_path, "", 0, NULL);
    switch_to_inet(&h);
    ck_assert(wait_until_label_is(h.app.status_label, "Готово"));
    g_file_set_contents(h.log_path, "", 0, NULL);

    set_search_text(&h, "   ");

    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Введите запрос");
    ck_assert_str_eq(list_placeholder_text(h.app.list_boxes[1]), "Введите запрос");
    log = read_log(&h);
    ck_assert_str_eq(log, "");
    g_free(log);

    search_harness_free(&h);
}
END_TEST

START_TEST(test_online_tab_loads_catalog_when_filter_is_empty)
{
    SearchHarness h = {0};
    gchar *log;

    search_harness_init(&h);
    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();
    g_file_set_contents(h.log_path, "", 0, NULL);
    switch_to_inet(&h);

    ck_assert(wait_until_label_is(h.app.status_label, "Готово"));
    ck_assert_int_eq(list_row_count(h.app.list_boxes[1]), 1);

    log = read_log(&h);
    ck_assert_str_eq(log, "-Ss \n");
    g_free(log);

    search_harness_free(&h);
}
END_TEST

START_TEST(test_debounce_pending_search_shows_searching_status)
{
    SearchHarness h = {0};

    search_harness_init(&h);
    switch_to_inet(&h);

    set_search_text(&h, "alpha");

    ck_assert_str_eq(gtk_label_get_text(h.app.status_label), "Поиск…");

    search_harness_free(&h);
}
END_TEST

START_TEST(test_empty_successful_search_results_show_not_found_and_clear_details)
{
    SearchHarness h = {0};
    GtkListBoxRow *row;

    search_harness_init(&h);
    switch_to_inet(&h);

    set_search_text(&h, "alpha");
    ck_assert(wait_until_label_is(h.app.status_label, "Готово"));
    ck_assert_int_eq(list_row_count(h.app.list_boxes[1]), 1);
    row = gtk_list_box_get_row_at_index(h.app.list_boxes[1], 0);
    gtk_list_box_select_row(h.app.list_boxes[1], row);
    drain_events();
    ck_assert_str_eq(gtk_label_get_text(h.app.details.name), "alpha.pfs");

    set_search_text(&h, "none");
    ck_assert(wait_until_label_is(h.app.status_label, "Ничего не найдено"));

    ck_assert_int_eq(list_row_count(h.app.list_boxes[1]), 0);
    ck_assert_str_eq(list_placeholder_text(h.app.list_boxes[1]), "Ничего не найдено");
    ck_assert_str_eq(gtk_label_get_text(h.app.details.name), "—");

    search_harness_free(&h);
}
END_TEST

START_TEST(test_backend_search_error_uses_neutral_search_error_status)
{
    SearchHarness h = {0};

    search_harness_init(&h);
    switch_to_inet(&h);

    set_search_text(&h, "error");

    ck_assert(wait_until_label_is(h.app.status_label, "Ошибка поиска: mirror refused query"));

    search_harness_free(&h);
}
END_TEST

START_TEST(test_online_query_is_cleared_across_tab_switches)
{
    SearchHarness h = {0};

    search_harness_init(&h);
    switch_to_inet(&h);
    set_search_text(&h, "alpha");
    gtk_stack_set_visible_child_name(h.app.stack, "local");
    drain_events();
    gtk_stack_set_visible_child_name(h.app.stack, "inet");
    drain_events();

    ck_assert_str_eq(gtk_entry_get_text(GTK_ENTRY(h.app.search_entry)), "");

    search_harness_free(&h);
}
END_TEST

START_TEST(test_footer_open_and_reveal_buttons_are_absent)
{
    SearchHarness h = {0};

    search_harness_init(&h);
    ck_assert_ptr_null(find_widget_by_name(h.window, "btn-open-local"));
    ck_assert_ptr_null(find_widget_by_name(h.window, "btn-reveal-local"));
    search_harness_free(&h);
}
END_TEST

static Suite *search_states_suite(void)
{
    Suite *suite = suite_create("search-states");
    TCase *tc = tcase_create("online");

    tcase_add_test(tc, test_empty_online_query_prompts_without_backend_search);
    tcase_add_test(tc, test_online_tab_loads_catalog_when_filter_is_empty);
    tcase_add_test(tc, test_debounce_pending_search_shows_searching_status);
    tcase_add_test(tc, test_empty_successful_search_results_show_not_found_and_clear_details);
    tcase_add_test(tc, test_backend_search_error_uses_neutral_search_error_status);
    tcase_add_test(tc, test_online_query_is_cleared_across_tab_switches);
    tcase_add_test(tc, test_footer_open_and_reveal_buttons_are_absent);
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

    suite = search_states_suite();
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
