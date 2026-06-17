#if __has_include(<check.h>) && __has_include(<gtk/gtk.h>) && __has_include(<atk/atk.h>)
#include <check.h>
#include <gtk/gtk.h>
#include <atk/atk.h>

#include <string.h>

#include "../include/ui.h"

GtkWidget *ui_build_module_row(ModuleInfo *module);

typedef struct A11yHarness {
    GtkApplication *application;
    AppState app;
    GtkWidget *window;
} A11yHarness;

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

static gboolean a11y_harness_init(A11yHarness *h)
{
    if (h == NULL) {
        return FALSE;
    }

    if (!ensure_gtk_ready()) {
        return FALSE;
    }

    h->application = gtk_application_new("org.puppyrus.modman.tests.a11y", G_APPLICATION_NON_UNIQUE);
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

static void a11y_harness_free(A11yHarness *h)
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
}

static const gchar *widget_accessible_name(GtkWidget *widget)
{
    AtkObject *accessible;

    ck_assert_ptr_nonnull(widget);
    accessible = gtk_widget_get_accessible(widget);
    ck_assert_ptr_nonnull(accessible);
    return atk_object_get_name(accessible);
}

static const gchar *widget_accessible_description(GtkWidget *widget)
{
    AtkObject *accessible;

    ck_assert_ptr_nonnull(widget);
    accessible = gtk_widget_get_accessible(widget);
    ck_assert_ptr_nonnull(accessible);
    return atk_object_get_description(accessible);
}

static AtkRole widget_accessible_role(GtkWidget *widget)
{
    AtkObject *accessible;

    ck_assert_ptr_nonnull(widget);
    accessible = gtk_widget_get_accessible(widget);
    ck_assert_ptr_nonnull(accessible);
    return atk_object_get_role(accessible);
}

static void assert_button_icon_name(GtkButton *button, const char *expected)
{
    GtkWidget *image;
    const gchar *icon_name = NULL;
    GtkIconSize icon_size = GTK_ICON_SIZE_INVALID;

    ck_assert_ptr_nonnull(button);
    ck_assert_ptr_nonnull(expected);

    image = gtk_button_get_image(button);
    ck_assert_ptr_nonnull(image);
    ck_assert(GTK_IS_IMAGE(image));
    gtk_image_get_icon_name(GTK_IMAGE(image), &icon_name, &icon_size);
    ck_assert_ptr_nonnull(icon_name);
    ck_assert_str_eq(icon_name, expected);
}

static void assert_image_menu_item_icon_name(GtkWidget *item, const char *expected)
{
    GtkWidget *image;
    const gchar *icon_name = NULL;
    GtkIconSize icon_size = GTK_ICON_SIZE_INVALID;

    ck_assert_ptr_nonnull(item);
    ck_assert(GTK_IS_IMAGE_MENU_ITEM(item));
    ck_assert_ptr_nonnull(expected);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    image = gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(item));
    G_GNUC_END_IGNORE_DEPRECATIONS
    ck_assert_ptr_nonnull(image);
    ck_assert(GTK_IS_IMAGE(image));
    gtk_image_get_icon_name(GTK_IMAGE(image), &icon_name, &icon_size);
    ck_assert_ptr_nonnull(icon_name);
    ck_assert_str_eq(icon_name, expected);
}

static void assert_widget_name_contains(GtkWidget *widget, const char *needle)
{
    const gchar *name = widget_accessible_name(widget);

    ck_assert_ptr_nonnull(name);
    ck_assert_ptr_nonnull(needle);
    ck_assert_ptr_nonnull(strstr(name, needle));
}

static void assert_widget_description_contains(GtkWidget *widget, const char *needle)
{
    const gchar *description = widget_accessible_description(widget);

    ck_assert_ptr_nonnull(description);
    ck_assert_ptr_nonnull(needle);
    ck_assert_ptr_nonnull(strstr(description, needle));
}

static GtkWidget *find_widget_by_accessible_name(GtkWidget *root, const char *needle)
{
    GList *children;
    GList *iter;
    const gchar *name;

    if (root == NULL || needle == NULL) {
        return NULL;
    }

    name = atk_object_get_name(gtk_widget_get_accessible(root));
    if (name != NULL && strstr(name, needle) != NULL) {
        return root;
    }

    if (!GTK_IS_CONTAINER(root)) {
        return NULL;
    }

    children = gtk_container_get_children(GTK_CONTAINER(root));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *match = find_widget_by_accessible_name(GTK_WIDGET(iter->data), needle);

        if (match != NULL) {
            g_list_free(children);
            return match;
        }
    }

    g_list_free(children);
    return NULL;
}

static GtkListBoxRow *append_module_row(GtkListBox *list_box, ModuleInfo *module)
{
    GtkWidget *row;

    ck_assert_ptr_nonnull(list_box);
    ck_assert_ptr_nonnull(module);

    row = ui_build_module_row(module);
    ck_assert_ptr_nonnull(row);
    gtk_container_add(GTK_CONTAINER(list_box), row);
    gtk_widget_show_all(GTK_WIDGET(list_box));
    drain_events();
    return GTK_LIST_BOX_ROW(row);
}

static GtkWidget *find_visible_confirm_dialog(void)
{
    GList *toplevels = gtk_window_list_toplevels();
    GList *iter;
    GtkWidget *dialog = NULL;

    for (iter = toplevels; iter != NULL; iter = iter->next) {
        GtkWidget *widget = GTK_WIDGET(iter->data);

        if (GTK_IS_MESSAGE_DIALOG(widget) && gtk_widget_get_visible(widget)) {
            dialog = widget;
            break;
        }
    }

    g_list_free(toplevels);
    return dialog;
}

static gboolean inspect_confirm_dialog_and_accept(gpointer user_data)
{
    GtkWidget *dialog = find_visible_confirm_dialog();
    GtkWidget *cancel_button;
    GtkWidget *accept_button;

    (void)user_data;

    if (dialog == NULL) {
        return G_SOURCE_CONTINUE;
    }

    ck_assert_int_eq((int)widget_accessible_role(dialog), (int)ATK_ROLE_ALERT);
    assert_widget_name_contains(dialog, "Отключить системный модуль");
    assert_widget_description_contains(dialog, "Проверка семантики предупреждения");

    cancel_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    accept_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    ck_assert_ptr_nonnull(cancel_button);
    ck_assert_ptr_nonnull(accept_button);

    assert_widget_name_contains(cancel_button, "Отмена опасного действия");
    assert_widget_description_contains(cancel_button, "не выполнять действие");
    assert_widget_name_contains(accept_button, "Отключить");
    assert_widget_description_contains(accept_button, "Подтвердить опасное действие");

    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

static void on_dummy_info_action(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
}

START_TEST(test_toolbar_sidebar_and_row_accessibility)
{
    A11yHarness h = {0};
    ModuleInfo module = {0};
    GtkWidget *about_button;
    GtkWidget *sidebar_loaded;
    GtkWidget *sidebar_inet;
    GtkWidget *sidebar_local;
    GtkWidget *sidebar_system;
    GtkListBoxRow *row;

    if (!a11y_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();

    module.name = "firefox-esr.pfs";
    module.version = "128.9.0";
    module.category = "browser";
    module.desc = "Браузер для повседневной работы";
    module.size_mb = 243.0;
    module.is_autoload = TRUE;
    row = append_module_row(h.app.list_boxes[0], &module);

    assert_widget_name_contains(GTK_WIDGET(h.app.btn_refresh), "Обновить список");
    assert_widget_description_contains(GTK_WIDGET(h.app.btn_refresh), "Перечитать список");
    assert_widget_name_contains(GTK_WIDGET(h.app.btn_load), "Подключить выбранный модуль");
    assert_widget_description_contains(GTK_WIDGET(h.app.btn_load), "текущую сессию");
    assert_widget_name_contains(GTK_WIDGET(h.app.btn_unload), "Отключить выбранный модуль");
    assert_widget_description_contains(GTK_WIDGET(h.app.btn_unload), "текущей сессии");
    assert_widget_name_contains(GTK_WIDGET(h.app.btn_clear_old), "Очистить старые файлы");
    assert_widget_description_contains(GTK_WIDGET(h.app.btn_clear_old), "устаревшие файлы");
    assert_widget_name_contains(GTK_WIDGET(h.app.search_entry), "Поиск модулей");
    assert_widget_description_contains(GTK_WIDGET(h.app.search_entry), "описанию или категории");

    about_button = find_widget_by_accessible_name(h.window, "О программе");
    ck_assert_ptr_nonnull(about_button);
    assert_widget_description_contains(about_button, "сведения о программе");

    sidebar_loaded = find_widget_by_accessible_name(h.window, "Подключенные");
    sidebar_inet = find_widget_by_accessible_name(h.window, "Интернет");
    sidebar_local = find_widget_by_accessible_name(h.window, "Локальные");
    sidebar_system = find_widget_by_accessible_name(h.window, "Система");
    ck_assert_ptr_nonnull(sidebar_loaded);
    ck_assert_ptr_nonnull(sidebar_inet);
    ck_assert_ptr_nonnull(sidebar_local);
    ck_assert_ptr_nonnull(sidebar_system);

    assert_widget_name_contains(sidebar_loaded, "1");
    assert_widget_description_contains(sidebar_loaded, "Сейчас в списке 1 модул");
    assert_widget_name_contains(sidebar_inet, "Интернет");
    assert_widget_name_contains(sidebar_local, "Локальные");
    assert_widget_name_contains(sidebar_system, "Система");

    ck_assert_int_eq((int)widget_accessible_role(GTK_WIDGET(row)), (int)ATK_ROLE_LIST_ITEM);
    assert_widget_name_contains(GTK_WIDGET(row), "firefox-esr.pfs");
    assert_widget_name_contains(GTK_WIDGET(row), "автозагрузка включена");
    assert_widget_description_contains(GTK_WIDGET(row), "Браузер для повседневной работы");
    assert_widget_description_contains(GTK_WIDGET(row), "browser");

    a11y_harness_free(&h);
}
END_TEST

START_TEST(test_details_widgets_have_accessible_names)
{
    A11yHarness h = {0};
    ModuleInfo module = {0};
    GtkListBoxRow *row;

    if (!a11y_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    gtk_stack_set_visible_child_name(h.app.stack, "loaded");
    drain_events();

    module.name = "kernel-tools.pfs";
    module.version = "2.1";
    module.category = "system";
    module.desc = "Набор системных утилит";
    module.path = "/var/lib/modman/modules/kernel-tools.pfs";
    module.size_mb = 64.0;
    module.is_autoload = TRUE;
    row = append_module_row(h.app.list_boxes[0], &module);
    gtk_list_box_select_row(h.app.list_boxes[0], row);
    drain_events();

    assert_widget_name_contains(GTK_WIDGET(h.app.details.name), "kernel-tools.pfs");
    assert_widget_description_contains(GTK_WIDGET(h.app.details.name), "Выбранный модуль");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.status), gtk_label_get_text(h.app.details.status));
    assert_widget_name_contains(GTK_WIDGET(h.app.details.version), "2.1");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.category), "system");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.size), gtk_label_get_text(h.app.details.size));
    assert_widget_name_contains(GTK_WIDGET(h.app.details.path), "/var/lib/modman/modules/kernel-tools.pfs");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.description), "Набор системных утилит");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.hooks_start), "—");
    assert_widget_name_contains(GTK_WIDGET(h.app.details.hooks_stop), "—");
    ck_assert_ptr_nonnull(h.app.progress_bar);
    assert_widget_name_contains(GTK_WIDGET(h.app.progress_bar), "Прогресс операции");

    {
        GtkWidget *details_panel = find_widget_by_accessible_name(h.window, "Панель подробностей");
        ck_assert_ptr_nonnull(details_panel);
        ck_assert_int_eq((int)widget_accessible_role(details_panel), (int)ATK_ROLE_PANEL);
        assert_widget_description_contains(details_panel, "свойства выбранного модуля");
    }

    a11y_harness_free(&h);
}
END_TEST

START_TEST(test_infobar_roles_and_messages_are_accessible)
{
    A11yHarness h = {0};

    if (!a11y_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    app_state_hide_info(&h.app);
    app_state_show_info(&h.app,
                        GTK_MESSAGE_INFO,
                        "Короткое сообщение",
                        "_Повторить",
                        G_CALLBACK(on_dummy_info_action),
                        TRUE);
    ck_assert_int_eq((int)widget_accessible_role(h.app.info_bar), (int)ATK_ROLE_STATUSBAR);
    assert_widget_name_contains(h.app.info_bar, "Сообщение состояния");
    assert_widget_description_contains(h.app.info_bar, "Короткое сообщение");
    ck_assert_ptr_nonnull(h.app.info_action);
    assert_widget_name_contains(h.app.info_action, "Повторить");

    app_state_hide_info(&h.app);
    app_state_show_info(&h.app, GTK_MESSAGE_WARNING, "Предупреждение проверки", NULL, NULL, FALSE);
    ck_assert_int_eq((int)widget_accessible_role(h.app.info_bar), (int)ATK_ROLE_ALERT);
    assert_widget_name_contains(h.app.info_bar, "Предупреждение");
    assert_widget_description_contains(h.app.info_bar, "Предупреждение проверки");

    app_state_hide_info(&h.app);
    app_state_show_info(&h.app, GTK_MESSAGE_ERROR, "Ошибка проверки", NULL, NULL, FALSE);
    ck_assert_int_eq((int)widget_accessible_role(h.app.info_bar), (int)ATK_ROLE_ALERT);
    assert_widget_name_contains(h.app.info_bar, "Ошибка");
    assert_widget_description_contains(h.app.info_bar, "Ошибка проверки");

    a11y_harness_free(&h);
}
END_TEST

START_TEST(test_action_icons_use_canonical_names)
{
    A11yHarness h = {0};

    if (!a11y_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    assert_button_icon_name(h.app.btn_load, "gtk-add");
    assert_button_icon_name(h.app.btn_unload, "gtk-remove");
    assert_button_icon_name(h.app.btn_refresh, "view-refresh");
    assert_image_menu_item_icon_name(h.app.mi_file_load, "gtk-open");

    a11y_harness_free(&h);
}
END_TEST

START_TEST(test_confirm_dialog_accessibility)
{
    A11yHarness h = {0};

    if (!a11y_harness_init(&h)) {
        ck_abort_msg("GTK test environment unavailable");
    }

    g_idle_add(inspect_confirm_dialog_and_accept, NULL);

    ck_assert(app_state_confirm_dangerous(&h.app,
                                          "⚠  Отключить системный модуль?",
                                          "Проверка семантики предупреждения.",
                                          "_Отключить"));

    a11y_harness_free(&h);
}
END_TEST

static Suite *a11y_suite(void)
{
    Suite *suite = suite_create("a11y");
    TCase *tc = tcase_create("ui");

    tcase_add_test(tc, test_toolbar_sidebar_and_row_accessibility);
    tcase_add_test(tc, test_details_widgets_have_accessible_names);
    tcase_add_test(tc, test_infobar_roles_and_messages_are_accessible);
    tcase_add_test(tc, test_action_icons_use_canonical_names);
    tcase_add_test(tc, test_confirm_dialog_accessibility);
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

    suite = a11y_suite();
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
