#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>)
#include <check.h>
#include <gtk/gtk.h>

#include <string.h>

#include "../include/ui.h"

enum {
    TEST_OPERATION_STATUS_CONNECTING = 0,
    TEST_OPERATION_STATUS_DISCONNECTING = 1,
    TEST_OPERATION_STATUS_CANCELLED = 2,
    TEST_OPERATION_STATUS_ERROR = 3,
    TEST_OPERATION_STATUS_READY = 4
};

void app_state_set_operation_status(AppState *app, int status, const char *detail);

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

static void assert_label_text_is(GtkLabel *label, const char *expected)
{
    const char *actual;

    ck_assert_ptr_nonnull(label);
    actual = gtk_label_get_text(label);
    ck_assert_ptr_nonnull(actual);
    ck_assert_str_eq(actual, expected);
}

START_TEST(test_operation_status_helper_uses_neutral_russian_copy)
{
    AppState app = {0};
    GtkWidget *status_label;
    GtkWidget *info_bar;

    if (!ensure_gtk_ready()) {
        ck_abort_msg("GTK test environment unavailable");
    }

    status_label = gtk_label_new("");
    info_bar = gtk_info_bar_new();
    app.status_label = GTK_LABEL(status_label);
    app.info_bar = info_bar;

    app_state_set_operation_status(&app, TEST_OPERATION_STATUS_CONNECTING, NULL);
    assert_label_text_is(app.status_label, "Подключение…");

    app_state_set_operation_status(&app, TEST_OPERATION_STATUS_DISCONNECTING, NULL);
    assert_label_text_is(app.status_label, "Отключение…");

    app_state_set_operation_status(&app, TEST_OPERATION_STATUS_CANCELLED, NULL);
    assert_label_text_is(app.status_label, "Отменено");

    app_state_set_operation_status(&app, TEST_OPERATION_STATUS_ERROR, "backend refused");
    assert_label_text_is(app.status_label, "Ошибка: backend refused");

    app_state_set_operation_status(&app, TEST_OPERATION_STATUS_READY, NULL);
    assert_label_text_is(app.status_label, "Готово");

    gtk_widget_destroy(info_bar);
    gtk_widget_destroy(status_label);
}
END_TEST

START_TEST(test_error_info_uses_neutral_status_label_without_error_infobar)
{
    AppState app = {0};
    GtkWidget *status_label;
    GtkWidget *info_bar;

    if (!ensure_gtk_ready()) {
        ck_abort_msg("GTK test environment unavailable");
    }

    status_label = gtk_label_new("");
    info_bar = gtk_info_bar_new();
    gtk_widget_show(info_bar);
    app.status_label = GTK_LABEL(status_label);
    app.info_bar = info_bar;

    app_state_show_info(&app, GTK_MESSAGE_ERROR, "backend refused", NULL, NULL, FALSE);

    assert_label_text_is(app.status_label, "Ошибка: backend refused");
    ck_assert(!gtk_widget_get_visible(info_bar));

    gtk_widget_destroy(info_bar);
    gtk_widget_destroy(status_label);
}
END_TEST

static Suite *infobar_suite(void)
{
    Suite *suite = suite_create("infobar");
    TCase *tc = tcase_create("status");

    tcase_add_test(tc, test_operation_status_helper_uses_neutral_russian_copy);
    tcase_add_test(tc, test_error_info_uses_neutral_status_label_without_error_infobar);
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

    suite = infobar_suite();
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
