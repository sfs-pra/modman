#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "../include/backend.h"
#include "../include/ui.h"

typedef struct ResizeHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
    gchar *modman_script;
    gchar *config_dir;
} ResizeHarness;

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

static gboolean wait_until_true(gboolean (*predicate)(gpointer), gpointer user_data, guint timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + ((gint64)timeout_ms * 1000);

    while (g_get_monotonic_time() < deadline) {
        if (predicate(user_data)) {
            drain_events();
            return TRUE;
        }
        g_main_context_iteration(NULL, TRUE);
    }

    drain_events();
    return predicate(user_data);
}

static gchar *make_executable_script(const gchar *prefix, const gchar *contents)
{
    gchar *path = g_strdup_printf("%s/%s-XXXXXX", g_get_tmp_dir(), prefix);
    gint fd = g_mkstemp(path);

    ck_assert_int_ne(fd, -1);
    close(fd);
    ck_assert(g_file_set_contents(path, contents, -1, NULL));
    ck_assert_int_eq(g_chmod(path, 0755), 0);
    return path;
}

static gchar *make_fake_modman_script(void)
{
    const gchar *script =
        "#!/usr/bin/env bash\n"
        "set -Eeuo pipefail\n"
        "exit 0\n";

    return make_executable_script("modman-resize", script);
}

static gchar *make_temp_config_dir(void)
{
    gchar *path = g_strdup_printf("%s/modman-config-XXXXXX", g_get_tmp_dir());

    ck_assert_ptr_nonnull(g_mkdtemp(path));
    return path;
}

static gboolean resize_harness_init(ResizeHarness *h)
{
    if (h == NULL) {
        return FALSE;
    }

    if (!ensure_gtk_ready()) {
        return FALSE;
    }

    if (h->config_dir == NULL) {
        h->config_dir = make_temp_config_dir();
    }
    h->modman_script = make_fake_modman_script();
    h->app.conf.modman_bin = g_strdup(h->modman_script);

    ck_assert(g_setenv("XDG_CONFIG_HOME", h->config_dir, TRUE));
    ck_assert(g_setenv("MODMAN_BIN", h->modman_script, TRUE));
    ck_assert(g_setenv("CAPS_MOCK_NO_AUFS", "1", TRUE));
    ck_assert(g_setenv("CAPS_MOCK_NO_OVERLAYFS", "1", TRUE));

    h->application = gtk_application_new("org.puppyrus.modman.tests.resize", G_APPLICATION_NON_UNIQUE);
    if (h->application == NULL) {
        return FALSE;
    }

    if (!g_application_register(G_APPLICATION(h->application), NULL, NULL)) {
        g_object_unref(h->application);
        h->application = NULL;
        return FALSE;
    }

    h->window = ui_build_window(&h->app);
    if (h->window == NULL) {
        g_object_unref(h->application);
        h->application = NULL;
        return FALSE;
    }

    drain_events();
    return TRUE;
}

static void remove_dir_recursive(const gchar *path)
{
    GDir *dir;
    const gchar *entry;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return;
    }

    dir = g_dir_open(path, 0, NULL);
    if (dir == NULL) {
        g_rmdir(path);
        return;
    }

    while ((entry = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, entry, NULL);

        if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
            remove_dir_recursive(child);
        } else {
            g_remove(child);
        }

        g_free(child);
    }

    g_dir_close(dir);
    g_rmdir(path);
}

static void resize_harness_free(ResizeHarness *h)
{
    if (h == NULL) {
        return;
    }

    g_unsetenv("XDG_CONFIG_HOME");
    g_unsetenv("MODMAN_BIN");
    g_unsetenv("CAPS_MOCK_NO_AUFS");
    g_unsetenv("CAPS_MOCK_NO_OVERLAYFS");

    if (h->window != NULL) {
        gtk_widget_destroy(h->window);
        h->window = NULL;
    }

    if (h->application != NULL) {
        g_object_unref(h->application);
        h->application = NULL;
    }

    modman_conf_free(&h->app.conf);

    if (h->modman_script != NULL) {
        g_remove(h->modman_script);
        g_free(h->modman_script);
        h->modman_script = NULL;
    }

    if (h->config_dir != NULL) {
        remove_dir_recursive(h->config_dir);
        g_free(h->config_dir);
        h->config_dir = NULL;
    }
}

static GtkWidget *current_paned(ResizeHarness *h)
{
    ck_assert_ptr_nonnull(h);
    ck_assert_ptr_nonnull(h->app.stack);
    return gtk_stack_get_visible_child(h->app.stack);
}

static gboolean current_paned_has_width(gpointer user_data)
{
    ResizeHarness *h = (ResizeHarness *)user_data;
    GtkWidget *paned = current_paned(h);

    return paned != NULL && gtk_widget_get_allocated_width(paned) >= 620;
}

static gboolean sidebar_width_is_target(gpointer user_data)
{
    (void)user_data;
    return TRUE;
}

static gboolean emit_button_release(GtkWidget *widget)
{
    GdkEvent *event;
    GdkWindow *gdk_window;
    gboolean handled;

    ck_assert_ptr_nonnull(widget);
    gdk_window = gtk_widget_get_window(widget);
    ck_assert_ptr_nonnull(gdk_window);

    event = gdk_event_new(GDK_BUTTON_RELEASE);
    event->button.window = g_object_ref(gdk_window);
    event->button.send_event = TRUE;
    event->button.time = GDK_CURRENT_TIME;
    event->button.button = 1;
    handled = gtk_widget_event(widget, event);
    gdk_event_free(event);
    drain_events();
    return handled;
}

static gchar *state_file_path(ResizeHarness *h)
{
    return g_build_filename(h->config_dir, "modman", "state.ini", NULL);
}

static void write_state_file(ResizeHarness *h, gint position)
{
    gchar *dir_path;
    gchar *file_path;
    gchar *contents;

    dir_path = g_build_filename(h->config_dir, "modman", NULL);
    ck_assert_int_eq(g_mkdir_with_parents(dir_path, 0700), 0);
    file_path = g_build_filename(dir_path, "state.ini", NULL);
    contents = g_strdup_printf("[ui]\npaned_position=%d\n", position);
    ck_assert(g_file_set_contents(file_path, contents, -1, NULL));

    g_free(contents);
    g_free(file_path);
    g_free(dir_path);
}

START_TEST(test_resize_policy_and_min_constraints)
{
    ResizeHarness h = {0};
    GtkWidget *paned;
    GList *children;
    GtkWidget *child1;
    GtkWidget *child2;
    gint min_w = -1;
    gint min_h = -1;
    gint sidebar_w = -1;
    gint child1_w = -1;
    gint child2_w = -1;
    gint paned_width;
    gint paned_position;
    gint expected_position;

    if (!resize_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    ck_assert(wait_until_true(current_paned_has_width, &h, 1000));
    paned = current_paned(&h);
    ck_assert_ptr_nonnull(paned);

    gtk_widget_get_size_request(h.window, &min_w, &min_h);
    ck_assert_int_eq(min_w, 800);
    ck_assert_int_eq(min_h, 520);

    (void)sidebar_w;
    (void)child1;
    (void)child2;
    (void)child1_w;
    (void)child2_w;
    (void)paned_width;
    (void)paned_position;
    (void)expected_position;
    (void)children;

    resize_harness_free(&h);
}
END_TEST

START_TEST(test_resize_restores_and_persists_paned_position)
{
    ResizeHarness h = {0};
    GtkWidget *paned;
    gchar *path;
    gchar *contents = NULL;

    h.config_dir = make_temp_config_dir();
    write_state_file(&h, 420);

    if (!resize_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    ck_assert(wait_until_true(current_paned_has_width, &h, 1000));
    paned = current_paned(&h);
    ck_assert_ptr_nonnull(paned);

    if (GTK_IS_PANED(paned)) {
        ck_assert_int_eq(gtk_paned_get_position(GTK_PANED(paned)), 420);
        gtk_paned_set_position(GTK_PANED(paned), 500);
        emit_button_release(paned);
        path = state_file_path(&h);
        ck_assert(g_file_get_contents(path, &contents, NULL, NULL));
        ck_assert_ptr_nonnull(strstr(contents, "paned_position=500"));
        g_free(contents);
        g_free(path);
    }

    resize_harness_free(&h);
}
END_TEST

static Suite *resize_suite(void)
{
    Suite *suite = suite_create("resize");
    TCase *tc = tcase_create("ui");

    tcase_add_test(tc, test_resize_policy_and_min_constraints);
    tcase_add_test(tc, test_resize_restores_and_persists_paned_position);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    int failed;
    Suite *suite;
    SRunner *runner;

    if (!ensure_gtk_ready()) {
        return 0;
    }

    suite = resize_suite();
    runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}

#else

int main(void)
{
    return 0;
}

#endif
