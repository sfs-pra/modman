#include <check.h>
#include <gtk/gtk.h>
#include <string.h>
#include "../include/ui.h"

static AppState *make_stub_app(GtkWidget *window)
{
    AppState *app = g_new0(AppState, 1);
    app->window = window;
    return app;
}

START_TEST(test_dump_format_minimal)
{
    GtkWidget *win;
    GtkWidget *lbl;
    AppState *app;
    gchar *dump;

    gtk_init(NULL, NULL);
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    lbl = gtk_label_new("hello");
    gtk_widget_set_name(lbl, "test-label");
    gtk_container_add(GTK_CONTAINER(win), lbl);
    gtk_widget_show_all(win);

    app = make_stub_app(win);
    dump = ui_debug_dump_widget_tree(app);
    ck_assert_msg(dump != NULL, "dump is NULL");
    ck_assert_msg(strstr(dump, "GtkWindow") != NULL, "no GtkWindow in dump");
    ck_assert_msg(strstr(dump, "GtkLabel#test-label") != NULL, "no GtkLabel#test-label");
    ck_assert_msg(strstr(dump, "\"hello\"") != NULL, "no hello text");
    g_free(dump);
    g_free(app);
    gtk_widget_destroy(win);
}
END_TEST

START_TEST(test_dump_format_progress_fraction)
{
    GtkWidget *win;
    GtkWidget *pb;
    AppState *app;
    gchar *dump;

    gtk_init(NULL, NULL);
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    pb = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb), 0.42);
    gtk_container_add(GTK_CONTAINER(win), pb);
    gtk_widget_show_all(win);

    app = make_stub_app(win);
    dump = ui_debug_dump_widget_tree(app);
    ck_assert_msg(dump != NULL, "dump is NULL");
    ck_assert_msg(strstr(dump, "GtkProgressBar") != NULL, "no GtkProgressBar");
    ck_assert_msg(strstr(dump, "fraction=0.420") != NULL, "wrong fraction");
    g_free(dump);
    g_free(app);
    gtk_widget_destroy(win);
}
END_TEST

START_TEST(test_dump_format_listbox_selection_mode)
{
    GtkWidget *win;
    GtkWidget *lb;
    AppState *app;
    gchar *dump;

    gtk_init(NULL, NULL);
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    lb = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_MULTIPLE);
    gtk_container_add(GTK_CONTAINER(win), lb);
    gtk_widget_show_all(win);

    app = make_stub_app(win);
    dump = ui_debug_dump_widget_tree(app);
    ck_assert_msg(dump != NULL, "dump is NULL");
    ck_assert_msg(strstr(dump, "GtkListBox") != NULL, "no GtkListBox");
    ck_assert_msg(strstr(dump, "selection_mode=MULTIPLE") != NULL, "wrong selection mode");
    g_free(dump);
    g_free(app);
    gtk_widget_destroy(win);
}
END_TEST

Suite *ui_debug_suite(void)
{
    Suite *s = suite_create("ui_debug");
    TCase *tc = tcase_create("dump_format");
    tcase_add_test(tc, test_dump_format_minimal);
    tcase_add_test(tc, test_dump_format_progress_fraction);
    tcase_add_test(tc, test_dump_format_listbox_selection_mode);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = ui_debug_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
