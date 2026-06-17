#include "../include/ui.h"

#include "../include/backend.h"
#include "../include/capabilities.h"
#include "../include/errors.h"
#include "../include/i18n.h"
#include "../include/validators.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct UiAsyncContext {
    AppState *app;
    GCancellable *op;
    char *module_name;
    char *module_path;
    gchar **module_names;
    gchar **module_args;
    guint module_index;
    guint module_total;
    guint module_success;
    GString *module_failures;
    char *inet_modman_bin;
    gboolean is_unload;
    gboolean retry_after_confirm;
    gboolean cacheable_inet_query;
    gboolean open_after_download;
} UiAsyncContext;

typedef struct UiLayoutState {
    AppState *app;
    GtkWidget *sidebar;
    GPtrArray *paneds;
    gint current_position;
    gint last_paned_width;
    gdouble current_ratio;
    gboolean has_saved_position;
    gboolean initial_position_applied;
    gboolean applying_resize;
} UiLayoutState;

typedef struct ConfirmDangerousState {
    GMainLoop *loop;
    gboolean accepted;
} ConfirmDangerousState;

typedef struct UiBusyProcess {
    GPid pid;
    char *command;
    char *path;
} UiBusyProcess;

typedef struct UiOpenProcessContext {
    AppState *app;
} UiOpenProcessContext;

typedef enum UiLocalDeleteState {
    UI_LOCAL_DELETE_READY = 0,
    UI_LOCAL_DELETE_DISABLED_NOT_LOCAL,
    UI_LOCAL_DELETE_DISABLED_NO_SELECTION,
    UI_LOCAL_DELETE_DISABLED_MOUNTED_SELECTED,
    UI_LOCAL_DELETE_DISABLED_BACKEND_UNAVAILABLE,
} UiLocalDeleteState;

struct SectionSpec {
    const char *id;
    const char *title;
    const char *empty_title;
    const char *empty_text;
    const char *icon_name;
    const char *icon_fallback;
};

enum {
    SECTION_LOADED = 0,
    SECTION_INET,
    SECTION_LOCAL,
    SECTION_SYSTEM,
    SECTION_COUNT
};

typedef enum UiSortMode {
    UI_SORT_NAME = 0,
    UI_SORT_SIZE,
    UI_SORT_LAYER,
} UiSortMode;

typedef enum UiOperationStatus {
    UI_OPERATION_STATUS_CONNECTING = 0,
    UI_OPERATION_STATUS_DISCONNECTING,
    UI_OPERATION_STATUS_CANCELLED,
    UI_OPERATION_STATUS_ERROR,
    UI_OPERATION_STATUS_READY,
    UI_OPERATION_STATUS_OPENING,
} UiOperationStatus;

typedef enum UiActionIcon {
    UI_ACTION_ICON_CONNECT = 0,
    UI_ACTION_ICON_DISCONNECT,
    UI_ACTION_ICON_REFRESH,
    UI_ACTION_ICON_OPEN_LOCAL_FILE,
} UiActionIcon;

typedef struct SectionSpec SectionSpec;

#define DETAILS_PLACEHOLDER "—"
#define UI_HIGHLIGHT_ROW_LIMIT 200U
#define UI_INET_DB_FALLBACK_PATH "/var/cache/modman/db.txt"
#define UI_MODMAN_BIN_FALLBACK "/usr/bin/modman"
#define UI_INET_CACHE_MTIME_UNKNOWN G_MININT64
#define UI_DETAILS_PANE_MIN_WIDTH 300
#define UI_DETAILS_PANE_DEFAULT_WIDTH 340
#define UI_DETAILS_COMPACT_WRAP_CHARS 32
#define UI_DETAILS_COMPACT_WRAP_LINES 2
#define UI_APP_ICON_NAME "system-software-install"
#define UI_APP_ICON_FALLBACK "system-software-install-symbolic"
#define UI_MODMAN_OPEN_FALLBACK "modman-open"

static const char *ui_current_section_id(AppState *app);
static void on_module_action_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_batch_load_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void ui_batch_load_next(AppState *app, UiAsyncContext *ctx);
static gboolean app_state_open_initial_system_section_idle(gpointer user_data);
static GtkWidget *details_pane_build(AppState *app, const SectionSpec *section);
static void details_pane_bind_for_list(AppState *app, GtkListBox *list_box);
static void details_pane_update(AppState *app, const ModuleInfo *module);
static void on_details_deps_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_details_file_info_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_refresh_sync_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_list_row_selected(GtkListBox *list_box, GtkListBoxRow *row, AppState *app);
static void on_list_row_activated(GtkListBox *list_box, GtkListBoxRow *row, AppState *app);
static void on_btn_delete_clicked(GtkButton *button, gpointer user_data);
static void on_toolbar_search_changed(GtkSearchEntry *entry, gpointer user_data);
static void on_action_clicked_impl(AppState *app, gboolean is_unload);
static void on_sort_combo_changed(GtkComboBoxText *combo, gpointer user_data);
static void ui_sync_sort_combo_for_section(AppState *app, const char *section_id);
static void on_toolbar_file_load_clicked(GtkButton *button, gpointer user_data);
static gboolean ui_open_file_with_modman_open(AppState *app, const char *filename);
static void on_toolbar_open_local_clicked(GtkButton *button, gpointer user_data);
static void on_toolbar_reveal_local_clicked(GtkButton *button, gpointer user_data);
static void on_local_opener_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_toolbar_config_clicked(GtkButton *button, gpointer user_data);
static void on_remove_local_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_local_selection_changed(GtkListBox *box, gpointer user_data);
static gboolean on_module_list_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkMenu *ui_build_module_context_menu(AppState *app, GtkListBox *box, GtkListBoxRow *row, const char *section_id);
static GtkWidget *ui_make_menu_item_with_icon(const char *icon_name, const char *label_text);
static void on_context_menu_load_activate(GtkMenuItem *item, gpointer user_data);
static void on_context_menu_download_activate(GtkMenuItem *item, gpointer user_data);
static void on_context_menu_download_open_activate(GtkMenuItem *item, gpointer user_data);
static void on_context_menu_unload_activate(GtkMenuItem *item, gpointer user_data);
static void on_context_menu_copy_name_activate(GtkMenuItem *item, gpointer user_data);
static void on_context_menu_remove_activate(GtkMenuItem *item, gpointer user_data);
static void on_download_action_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_config_open_done(GObject *source_object, GAsyncResult *result, gpointer user_data);
static UiLocalDeleteState ui_local_delete_state(AppState *app, GList *selected_rows);
static void ui_apply_local_delete_button_state(AppState *app, GList *selected_rows);
static gboolean on_confirm_dialog_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void on_confirm_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static UiAsyncContext *ui_async_context_new(AppState *app);
static void ui_async_context_free(UiAsyncContext *ctx);
static char *ui_markup_from_text(const char *text);
static gint app_state_info_priority(GtkMessageType type);
static const char *ui_action_icon_name(UiActionIcon icon);
static const char *ui_operation_status_text(UiOperationStatus status);
static char *ui_operation_status_message_dup(UiOperationStatus status, const char *detail);
static char *ui_selected_local_file_path_dup(AppState *app, gboolean require_exists);
static char *ui_downloaded_module_path_dup(AppState *app, const char *module_name);
static gboolean ui_launch_local_opener_argv(AppState *app, const char *const *argv);

static void app_state_clear_info_timer(AppState *app);
static void app_state_progress_arm(AppState *app);
static void app_state_progress_disarm(AppState *app);
static gboolean app_state_progress_tick(gpointer user_data);
static void app_state_clear_info_action(AppState *app);

static char *ui_strip_ansi_sequences(const char *text);
static void ui_show_unload_failure_dialog(AppState *app, const UiAsyncContext *ctx, const GError *error);
static void ui_show_busy_process_dialog(AppState *app, const char *target);
static gboolean app_state_uses_overlay_session_model(const AppState *app);
static gboolean app_state_has_systemd(const AppState *app);
static gboolean app_state_can_run_backend(const AppState *app);
static gboolean app_state_can_modify_modules(const AppState *app);
static void app_state_apply_capability_gates(AppState *app);
static gboolean ui_module_is_erofs(const ModuleInfo *module);
static void ui_apply_row_erofs_availability(AppState *app, GtkListBoxRow *row, gboolean force_local_context);
static void ui_refresh_local_erofs_markers(AppState *app);
static void on_info_action_refresh(GtkWidget *widget, gpointer user_data);
static void on_info_action_authorize(GtkWidget *widget, gpointer user_data);
static const char *ui_inet_cache_db_path(const AppState *app);
static gint64 ui_inet_cache_current_db_mtime(const AppState *app);
static char *ui_inet_cache_effective_modman_bin_dup(const AppState *app);
static void ui_inet_cache_clear(AppState *app);
static gboolean app_error_is_permission(const GError *error);
static void app_state_load_current_section(AppState *app);
static void app_state_cancel_cancellable(GCancellable **slot);
static void app_state_cancel_current_op(AppState *app, gboolean show_cancelled);
static gboolean query_is_empty_after_trim(const char *query);
static void ui_set_inet_placeholder(AppState *app, const char *text);
static void ui_set_inet_search_state(AppState *app, const char *text);
static gboolean ui_section_items_changed(AppState *app, const char *section_id, GPtrArray *items);
static gboolean ui_text_matches_query(const char *text, const char *query);
static gboolean ui_module_matches_query(const ModuleInfo *module, const char *query);
static gboolean ui_filter_row_for_current_section(GtkListBoxRow *row, gpointer user_data);
static void ui_invalidate_section_filter(AppState *app, const char *section_id);
static gint section_index_from_id(const char *section_id);
GtkWidget *ui_build_module_row(ModuleInfo *module);
static GtkWidget *ui_build_module_row_for_section(AppState *app, ModuleInfo *module, const char *section_id);
static GtkWidget *ui_build_compact_module_row(ModuleInfo *module, const char *section_id);

static void ui_refresh_all_rows(AppState *app);
static char *ui_build_name_markup(const ModuleInfo *module, const char *query);
static char *ui_build_meta_text(const ModuleInfo *module);
static const char *ui_icon_name_or_fallback(const char *primary, const char *fallback);
static GtkWidget *ui_icon_image_or_fallback(const char *primary, const char *fallback);
static void ui_image_set_icon_name_or_fallback(GtkImage *image,
                                               const char *primary,
                                               const char *fallback,
                                               GtkIconSize size);
static void ui_update_row_markup(GtkListBoxRow *row, const char *query);
static void ui_apply_search_highlight_to_list(GtkListBox *list_box, const char *query);
static void ui_set_accessible_metadata(AtkObject *accessible, const char *name, const char *description);
static void ui_set_widget_accessible(GtkWidget *widget, const char *name, const char *description);
static void ui_set_widget_accessible_with_role(GtkWidget *widget,
                                               const char *name,
                                               const char *description,
                                               AtkRole role);
static char *ui_accessible_display_label(const char *text);
static char *ui_caption_to_accessible_prefix(const char *caption);
static void details_label_sync_accessibility(GtkLabel *label, const char *value);
static guint ui_list_box_row_count(GtkListBox *list_box);
static char *ui_section_accessible_name(AppState *app, const SectionSpec *section);
static char *ui_section_accessible_description(AppState *app, const SectionSpec *section);
static void ui_set_section_sidebar_name(AppState *app, GtkWidget *page, const SectionSpec *section);
static gboolean ui_widget_contains_label_text(GtkWidget *widget, const char *text);
static void ui_sync_sidebar_accessibility_recursive(GtkWidget *widget,
                                                    AppState *app,
                                                    const SectionSpec *section);
static void ui_sync_sidebar_accessibility(AppState *app);
static void on_section_list_children_changed(GtkContainer *container, GtkWidget *child, gpointer user_data);
static void ui_focus_search_entry(AppState *app);
static void ui_show_about_dialog(AppState *app);
static void ui_switch_to_section(AppState *app, gint section_index);
static void ui_reset_search_for_section_change(AppState *app);
static const char *ui_section_tab_tooltip(const SectionSpec *section);
static GtkWidget *ui_build_section_tab_button(AppState *app,
                                              const SectionSpec *section,
                                              guint index,
                                              GtkSizeGroup *size_group);
static void ui_sync_tab_buttons(AppState *app);
static void on_section_tab_toggled(GtkToggleButton *button, gpointer user_data);
static gboolean ui_handle_primary_shortcut(AppState *app);
static gboolean ui_handle_unload_shortcut(AppState *app);
static gboolean ui_handle_escape_shortcut(AppState *app);
static void on_btn_exit_clicked(GtkButton *button, gpointer user_data);
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static UiLayoutState *ui_layout_state_get(AppState *app);
static char *ui_layout_state_path(void);
static gboolean ui_layout_state_load_position(gint *out_position);
static void ui_layout_state_save_position(gint position);
static gint ui_layout_target_sidebar_width(gint window_width);
static void ui_layout_apply_sidebar_width(AppState *app, gint window_width);
static gint ui_layout_clamp_paned_position(gint paned_width, gint desired);
static void ui_layout_state_update_from_position(UiLayoutState *state, gint paned_width, gint position);
static void ui_layout_apply_position_to_paned(UiLayoutState *state, GtkPaned *paned, gint paned_width, gboolean allow_initial_default);
static void ui_layout_add_paned(AppState *app, GtkWidget *paned);
static void ui_layout_state_free(UiLayoutState *state);
static gboolean on_paned_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void on_paned_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static void details_label_apply_compact_wrap(GtkLabel *label);
static void ui_show_update_dialog(AppState *app);
static gboolean ui_open_updates_on_start_idle(gpointer user_data);
static void on_update_blacklist_selected_clicked(GtkButton *button, gpointer user_data);
static void on_update_blacklist_remove_clicked(GtkButton *button, gpointer user_data);
static void on_check_updates_menu_activate(GtkMenuItem *item, gpointer user_data);

static G_GNUC_UNUSED gboolean ui_name_ends_with_old(const char *name)
{
    gsize len;

    if (name == NULL) {
        return FALSE;
    }

    len = strlen(name);
    return len >= 4U &&
           name[len - 4] == '.' &&
           name[len - 3] == 'o' &&
           name[len - 2] == 'l' &&
           name[len - 1] == 'd';
}

static char *ui_layer_number_dup(const char *layer)
{
    const char *start;
    const char *end;

    if (layer == NULL || layer[0] == '\0') {
        return g_strdup("—");
    }

    start = layer;
    while (*start != '\0' && !g_ascii_isdigit((guchar)*start)) {
        start++;
    }

    if (*start == '\0') {
        return g_strdup("—");
    }

    end = start;
    while (*end != '\0' && g_ascii_isdigit((guchar)*end)) {
        end++;
    }

    return g_strndup(start, (gsize)(end - start));
}

static gint ui_layer_number_value(const char *layer)
{
    char *number_text = ui_layer_number_dup(layer);
    gint value = 0;

    if (number_text != NULL && number_text[0] != '\0' && g_ascii_isdigit((guchar)number_text[0])) {
        value = (gint)g_ascii_strtoll(number_text, NULL, 10);
    }
    g_free(number_text);
    return value;
}

static G_GNUC_UNUSED char *ui_module_stem_dup(const char *target)
{
    char *base;

    if (target == NULL || target[0] == '\0') {
        return NULL;
    }

    base = g_path_get_basename(target);
    if (base != NULL && g_str_has_suffix(base, ".pfs")) {
        base[strlen(base) - 4U] = '\0';
    }
    if (base != NULL && base[0] == '.') {
        memmove(base, base + 1, strlen(base));
    }
    return base;
}

static void ui_old_files_collect_from_backend(AppState *app, GPtrArray *old_files)
{
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    char *modman_bin;
    gchar **lines;
    GError *error = NULL;

    if (old_files == NULL) {
        return;
    }

    modman_bin = ui_inet_cache_effective_modman_bin_dup(app);
    const char *argv[] = {
        modman_bin,
        "--list-old",
        NULL
    };

    if (!backend_call_sync(argv, &stdout_text, &stderr_text, NULL, &error)) {
        g_clear_error(&error);
        g_free(stderr_text);
        g_free(stdout_text);
        g_free(modman_bin);
        return;
    }

    lines = g_strsplit(stdout_text != NULL ? stdout_text : "", "\n", -1);
    for (guint i = 0U; lines != NULL && lines[i] != NULL; i++) {
        if (lines[i][0] != '\0') {
            g_ptr_array_add(old_files, g_strdup(lines[i]));
        }
    }

    g_strfreev(lines);
    g_free(stderr_text);
    g_free(stdout_text);
    g_free(modman_bin);
}

static gboolean ui_old_files_remove_with_backend(AppState *app,
                                                 GPtrArray *old_files,
                                                 guint *deleted_count,
                                                 guint *failed_count,
                                                 GError **error)
{
    char *modman_bin;
    gchar **argv;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    gchar **lines;
    guint argc;
    gboolean ok;

    if (deleted_count != NULL) {
        *deleted_count = 0U;
    }
    if (failed_count != NULL) {
        *failed_count = 0U;
    }
    if (old_files == NULL || old_files->len == 0U) {
        return TRUE;
    }

    modman_bin = ui_inet_cache_effective_modman_bin_dup(app);
    argc = old_files->len + 3U;
    argv = g_new0(gchar *, argc + 1U);
    argv[0] = g_strdup("pkexec");
    argv[1] = g_strdup(modman_bin);
    argv[2] = g_strdup("--remove-old");
    for (guint i = 0U; i < old_files->len; i++) {
        argv[i + 3U] = g_strdup((const char *)g_ptr_array_index(old_files, i));
    }

    ok = backend_call_sync((const char *const *)argv, &stdout_text, &stderr_text, NULL, error);
    lines = g_strsplit(stdout_text != NULL ? stdout_text : "", "\n", -1);
    for (guint i = 0U; lines != NULL && lines[i] != NULL; i++) {
        if (g_str_has_prefix(lines[i], "removed\t")) {
            if (deleted_count != NULL) {
                (*deleted_count)++;
            }
        } else if (g_str_has_prefix(lines[i], "error\t")) {
            if (failed_count != NULL) {
                (*failed_count)++;
            }
        }
    }

    g_strfreev(lines);
    g_free(stderr_text);
    g_free(stdout_text);
    g_strfreev(argv);
    g_free(modman_bin);
    return ok;
}

static void ui_show_clear_old_error(AppState *app, const char *message)
{
    GtkWidget *dialog;

    if (app == NULL || message == NULL || message[0] == '\0') {
        return;
    }

    dialog = gtk_message_dialog_new(app->window != NULL ? GTK_WINDOW(app->window) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_CLOSE,
                                    "%s",
                                    message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void ui_status_show_plain(AppState *app, GtkMessageType type, const char *message, gboolean auto_hide_8s)
{
    gchar *markup;

    if (app == NULL || message == NULL || message[0] == '\0') {
        return;
    }

    markup = ui_markup_from_text(message);
    app_state_show_info(app, type, markup, NULL, NULL, auto_hide_8s);
    g_free(markup);
}

static void clear_old_files(AppState *app)
{
    GPtrArray *old_files = NULL;
    GPtrArray *checks = NULL;
    GPtrArray *selected_old_files = NULL;
    gchar *message = NULL;
    guint deleted_count = 0U;
    guint failed_count = 0U;
    guint i;
    GError *backend_error = NULL;

    if (app == NULL) {
        return;
    }

    app_state_hide_info(app);

    old_files = g_ptr_array_new_with_free_func(g_free);
    ui_old_files_collect_from_backend(app, old_files);

    if (old_files->len == 0U) {
        g_ptr_array_free(old_files, TRUE);
        return;
    }

    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Clean up old modules"),
                                                        app->window != NULL ? GTK_WINDOW(app->window) : NULL,
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        _("_Cancel"),
                                                        GTK_RESPONSE_CANCEL,
                                                        _("_Remove Selected"),
                                                        GTK_RESPONSE_ACCEPT,
                                                        NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *label = gtk_label_new(_("Select .pfs.old files to remove:"));
        GtkWidget *status_label = gtk_label_new("");
        GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *remove_button;
        gint response;

        checks = g_ptr_array_new();
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_margin_top(label, 12);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
        gtk_label_set_xalign(GTK_LABEL(status_label), 0.0f);
        gtk_label_set_line_wrap(GTK_LABEL(status_label), TRUE);
        gtk_label_set_selectable(GTK_LABEL(status_label), TRUE);
        gtk_widget_set_no_show_all(status_label, TRUE);
        gtk_widget_set_size_request(scroller, 560, 260);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroller), box);

        remove_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        if (remove_button != NULL && GTK_IS_BUTTON(remove_button)) {
            gtk_button_set_image(GTK_BUTTON(remove_button), ui_icon_image_or_fallback("edit-delete", "user-trash-symbolic"));
            gtk_button_set_always_show_image(GTK_BUTTON(remove_button), TRUE);
        }

        for (i = 0U; i < old_files->len; i++) {
            const char *old_path = old_files->pdata[i];
            char *old_name = g_path_get_basename(old_path);
            char *old_dir = g_path_get_dirname(old_path);
            char *label_text = g_strdup_printf("%s  %s", old_name != NULL ? old_name : old_path, old_dir != NULL ? old_dir : "");
            GtkWidget *check = gtk_check_button_new_with_label(label_text);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
            g_object_set_data(G_OBJECT(check), "old-file-path", (gpointer)old_path);
            gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
            g_ptr_array_add(checks, check);
            g_free(label_text);
            g_free(old_dir);
            g_free(old_name);
        }

        gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 8);
        gtk_box_pack_start(GTK_BOX(content), status_label, FALSE, FALSE, 6);
        gtk_widget_show_all(dialog);
        gtk_widget_hide(status_label);
        for (i = 0U; i < checks->len; i++) {
            GtkWidget *check = g_ptr_array_index(checks, i);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
        }

        while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_ACCEPT) {
            g_clear_error(&backend_error);
            if (selected_old_files != NULL) {
                g_ptr_array_unref(selected_old_files);
            }
            selected_old_files = g_ptr_array_new_with_free_func(g_free);
            deleted_count = 0U;
            failed_count = 0U;

            for (i = 0U; i < checks->len; i++) {
                GtkWidget *check = g_ptr_array_index(checks, i);
                const char *old_path = g_object_get_data(G_OBJECT(check), "old-file-path");

                if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)) && old_path != NULL) {
                    g_ptr_array_add(selected_old_files, g_strdup(old_path));
                }
            }

            if (selected_old_files->len == 0U) {
                gtk_label_set_text(GTK_LABEL(status_label), _("Select files to remove."));
                gtk_widget_show(status_label);
                continue;
            }

            if (!ui_old_files_remove_with_backend(app,
                                                  selected_old_files,
                                                  &deleted_count,
                                                  &failed_count,
                                                  &backend_error)) {
                message = ui_strip_ansi_sequences(backend_error != NULL ? backend_error->message : NULL);
                if (message == NULL || message[0] == '\0') {
                    g_free(message);
                    message = g_strdup(_("Failed to remove selected .pfs.old files."));
                }
                gtk_label_set_text(GTK_LABEL(status_label), message);
                gtk_widget_show(status_label);
                ui_status_show_plain(app, GTK_MESSAGE_WARNING, message, TRUE);
                g_clear_error(&backend_error);
                g_free(message);
                message = NULL;
                continue;
            }

            if (deleted_count > 0U) {
                message = deleted_count == 1U
                    ? g_strdup(_("Removed 1 file"))
                    : g_strdup_printf(_("Removed %u files"), deleted_count);
                for (i = 0U; i < checks->len; i++) {
                    GtkWidget *check = g_ptr_array_index(checks, i);
                    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
                        gtk_widget_set_sensitive(check, FALSE);
                        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
                    }
                }
                gtk_label_set_text(GTK_LABEL(status_label), message);
                gtk_widget_show(status_label);
                ui_status_show_plain(app, GTK_MESSAGE_INFO, message, TRUE);
                g_free(message);
                message = NULL;
            } else if (failed_count > 0U) {
                message = g_strdup_printf(_("Failed to remove %u files"), failed_count);
                gtk_label_set_text(GTK_LABEL(status_label), message);
                gtk_widget_show(status_label);
                ui_status_show_plain(app, GTK_MESSAGE_WARNING, message, TRUE);
                g_free(message);
                message = NULL;
            } else {
                gtk_label_set_text(GTK_LABEL(status_label), _("No files were removed."));
                gtk_widget_show(status_label);
                ui_status_show_plain(app, GTK_MESSAGE_INFO, _("No files were removed."), TRUE);
            }
        }
        gtk_widget_destroy(dialog);
    }

    if (checks != NULL) {
        g_ptr_array_unref(checks);
    }
    if (selected_old_files != NULL) {
        g_ptr_array_unref(selected_old_files);
    }

    g_ptr_array_free(old_files, TRUE);
}

static char *ui_markup_from_text(const char *text)
{
    return g_markup_escape_text(text != NULL ? text : _("Operation completed."), -1);
}

static char *ui_trimmed_query_dup(const char *query)
{
    char *copy;

    if (query == NULL) {
        return NULL;
    }

    copy = g_strdup(query);
    if (copy == NULL) {
        return NULL;
    }

    g_strstrip(copy);
    if (copy[0] == '\0') {
        g_free(copy);
        return NULL;
    }

    return copy;
}

static void ui_append_escaped_slice(GString *builder,
                                    const char *text,
                                    guint start_offset,
                                    guint end_offset)
{
    char *slice;
    char *escaped;

    if (builder == NULL || text == NULL || end_offset <= start_offset) {
        return;
    }

    slice = g_strndup(text + start_offset, end_offset - start_offset);
    escaped = g_markup_escape_text(slice, -1);
    g_string_append(builder, escaped);
    g_free(escaped);
    g_free(slice);
}

static char *ui_casefold_text_with_byte_map(const char *text, GArray **out_map)
{
    const char *cursor;
    GString *folded;
    GArray *byte_map;

    if (out_map == NULL) {
        return NULL;
    }

    *out_map = NULL;

    if (text == NULL) {
        text = "";
    }

    if (!g_utf8_validate(text, -1, NULL)) {
        return NULL;
    }

    folded = g_string_new(NULL);
    byte_map = g_array_new(FALSE, FALSE, sizeof(guint));
    cursor = text;

    while (*cursor != '\0') {
        const char *next = g_utf8_next_char(cursor);
        gsize chunk_len = (gsize)(next - cursor);
        char *folded_chunk = g_utf8_casefold(cursor, (gssize)chunk_len);
        guint original_offset = (guint)(cursor - text);
        guint i;

        g_string_append(folded, folded_chunk);
        for (i = 0U; folded_chunk[i] != '\0'; i++) {
            g_array_append_val(byte_map, original_offset);
        }

        g_free(folded_chunk);
        cursor = next;
    }

    {
        guint final_offset = (guint)(cursor - text);
        g_array_append_val(byte_map, final_offset);
    }

    *out_map = byte_map;
    return g_string_free(folded, FALSE);
}

char *ui_build_highlight_markup(const char *text, const char *query)
{
    const char *plain_text = text != NULL ? text : "";
    char *trimmed_query = NULL;
    char *casefold_text = NULL;
    char *casefold_query = NULL;
    GArray *byte_map = NULL;
    GString *markup = NULL;
    guint text_offset = 0U;
    char *result;

    trimmed_query = ui_trimmed_query_dup(query);
    if (trimmed_query == NULL || !g_utf8_validate(plain_text, -1, NULL) || !g_utf8_validate(trimmed_query, -1, NULL)) {
        g_free(trimmed_query);
        return g_markup_escape_text(plain_text, -1);
    }

    casefold_text = ui_casefold_text_with_byte_map(plain_text, &byte_map);
    casefold_query = g_utf8_casefold(trimmed_query, -1);
    markup = g_string_new(NULL);

    if (casefold_text == NULL || byte_map == NULL || casefold_query == NULL || casefold_query[0] == '\0') {
        result = g_markup_escape_text(plain_text, -1);
        goto cleanup;
    }

    {
        gsize needle_len = strlen(casefold_query);
        const char *search_cursor = casefold_text;
        guint search_offset = 0U;

        while (TRUE) {
            const char *match = strstr(search_cursor, casefold_query);
            guint match_index;
            guint original_start;
            guint original_end;
            char *match_text;
            char *escaped_match;

            if (match == NULL) {
                break;
            }

            match_index = search_offset + (guint)(match - search_cursor);
            original_start = g_array_index(byte_map, guint, match_index);
            original_end = g_array_index(byte_map, guint, match_index + needle_len);

            ui_append_escaped_slice(markup, plain_text, text_offset, original_start);

            match_text = g_strndup(plain_text + original_start, original_end - original_start);
            escaped_match = g_markup_escape_text(match_text, -1);
            g_string_append_printf(markup,
                                   "<span weight='bold' underline='single'>%s</span>",
                                   escaped_match);
            g_free(escaped_match);
            g_free(match_text);

            text_offset = original_end;
            search_offset = match_index + (guint)needle_len;
            search_cursor = casefold_text + search_offset;
        }
    }

    ui_append_escaped_slice(markup, plain_text, text_offset, (guint)strlen(plain_text));
    result = g_string_free(markup, FALSE);
    markup = NULL;

cleanup:
    if (markup != NULL) {
        g_string_free(markup, TRUE);
    }
    if (byte_map != NULL) {
        g_array_free(byte_map, TRUE);
    }
    g_free(casefold_text);
    g_free(casefold_query);
    g_free(trimmed_query);
    return result;
}

static void ui_set_accessible_metadata(AtkObject *accessible, const char *name, const char *description)
{
    if (accessible == NULL) {
        return;
    }

    if (name != NULL) {
        atk_object_set_name(accessible, name);
    }

    if (description != NULL) {
        atk_object_set_description(accessible, description);
    }
}

static void ui_set_widget_accessible(GtkWidget *widget, const char *name, const char *description)
{
    if (widget == NULL) {
        return;
    }

    ui_set_accessible_metadata(gtk_widget_get_accessible(widget), name, description);
}

static void ui_set_widget_accessible_with_role(GtkWidget *widget,
                                               const char *name,
                                               const char *description,
                                               AtkRole role)
{
    AtkObject *accessible;

    if (widget == NULL) {
        return;
    }

    accessible = gtk_widget_get_accessible(widget);
    if (accessible == NULL) {
        return;
    }

    atk_object_set_role(accessible, role);
    ui_set_accessible_metadata(accessible, name, description);
}

static char *ui_accessible_display_label(const char *text)
{
    GString *builder;
    gsize i;

    if (text == NULL) {
        return g_strdup("");
    }

    builder = g_string_new(NULL);
    for (i = 0U; text[i] != '\0'; i++) {
        if (text[i] != '_') {
            g_string_append_c(builder, text[i]);
        }
    }

    return g_string_free(builder, FALSE);
}

static UiLayoutState *ui_layout_state_get(AppState *app)
{
    if (app == NULL || app->window == NULL) {
        return NULL;
    }

    return g_object_get_data(G_OBJECT(app->window), "ui-layout-state");
}

static char *ui_layout_state_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "modman", "state.ini", NULL);
}

static gboolean ui_layout_state_load_position(gint *out_position)
{
    GKeyFile *key_file;
    char *path;
    GError *error = NULL;
    gint value;

    if (out_position == NULL) {
        return FALSE;
    }

    path = ui_layout_state_path();
    if (path == NULL || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_free(path);
        return FALSE;
    }

    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_unref(key_file);
        g_free(path);
        return FALSE;
    }

    value = g_key_file_get_integer(key_file, "ui", "horizontal_paned_position", &error);
    g_key_file_unref(key_file);
    g_free(path);

    if (error != NULL) {
        g_clear_error(&error);
        return FALSE;
    }

    if (value <= 0) {
        return FALSE;
    }

    *out_position = value;
    return TRUE;
}

static void ui_layout_state_save_position(gint position)
{
    GKeyFile *key_file;
    char *config_dir;
    char *path;
    char *contents;

    if (position <= 0) {
        return;
    }

    config_dir = g_build_filename(g_get_user_config_dir(), "modman", NULL);
    if (config_dir == NULL) {
        return;
    }

    if (g_mkdir_with_parents(config_dir, 0700) != 0) {
        g_free(config_dir);
        return;
    }

    path = g_build_filename(config_dir, "state.ini", NULL);
    g_free(config_dir);
    if (path == NULL) {
        return;
    }

    key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "ui", "horizontal_paned_position", position);
    contents = g_key_file_to_data(key_file, NULL, NULL);
    g_file_set_contents(path, contents, -1, NULL);

    g_free(contents);
    g_key_file_unref(key_file);
    g_free(path);
}

static gint ui_layout_target_sidebar_width(gint window_width)
{
    return window_width < 1100 ? 140 : 180;
}

static void ui_layout_apply_sidebar_width(AppState *app, gint window_width)
{
    if (app == NULL || app->sidebar == NULL) {
        return;
    }

    gtk_widget_set_size_request(GTK_WIDGET(app->sidebar),
                                ui_layout_target_sidebar_width(window_width),
                                -1);
}

static gint ui_layout_clamp_paned_position(gint paned_width, gint desired)
{
    const gint min_list_width = 360;
    const gint min_details_width = UI_DETAILS_PANE_MIN_WIDTH;
    gint max_position;

    if (paned_width <= 0) {
        return desired;
    }

    max_position = paned_width - min_details_width;
    if (max_position < min_list_width) {
        gint midpoint = paned_width / 2;

        if (midpoint < min_list_width) {
            midpoint = min_list_width;
        }

        if (midpoint > max_position && max_position > 0) {
            midpoint = max_position;
        }

        return midpoint;
    }

    return CLAMP(desired, min_list_width, max_position);
}

static void ui_layout_state_update_from_position(UiLayoutState *state, gint paned_width, gint position)
{
    if (state == NULL || paned_width <= 0) {
        return;
    }

    state->current_position = ui_layout_clamp_paned_position(paned_width, position);
    state->current_ratio = (gdouble)state->current_position / (gdouble)MAX(paned_width, 1);
    state->last_paned_width = paned_width;
    state->initial_position_applied = TRUE;
}

static void ui_layout_apply_position_to_paned(UiLayoutState *state,
                                              GtkPaned *paned,
                                              gint paned_width,
                                              gboolean allow_initial_default)
{
    gint desired;

    if (state == NULL || paned == NULL || paned_width <= 0) {
        return;
    }

    if (!state->initial_position_applied) {
        if (state->has_saved_position && state->current_position > 0) {
            desired = state->current_position;
        } else if (allow_initial_default) {
            desired = paned_width - UI_DETAILS_PANE_DEFAULT_WIDTH;
        } else {
            return;
        }
    } else if (state->current_ratio > 0.0) {
        desired = (gint)((gdouble)paned_width * state->current_ratio + 0.5);
    } else if (state->current_position > 0) {
        desired = state->current_position;
    } else {
        desired = paned_width - UI_DETAILS_PANE_DEFAULT_WIDTH;
    }

    desired = ui_layout_clamp_paned_position(paned_width, desired);

    state->applying_resize = TRUE;
    gtk_paned_set_position(paned, desired);
    state->applying_resize = FALSE;
    ui_layout_state_update_from_position(state, paned_width, desired);
}

static G_GNUC_UNUSED void ui_layout_add_paned(AppState *app, GtkWidget *paned)
{
    UiLayoutState *state = ui_layout_state_get(app);

    if (state == NULL || paned == NULL) {
        return;
    }

    g_ptr_array_add(state->paneds, paned);
}

static void ui_layout_state_free(UiLayoutState *state)
{
    if (state == NULL) {
        return;
    }

    if (state->paneds != NULL) {
        g_ptr_array_unref(state->paneds);
    }

    g_free(state);
}

static G_GNUC_UNUSED gboolean on_paned_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    UiLayoutState *state = (UiLayoutState *)user_data;
    gint paned_width;
    gint position;
    gint clamped;

    (void)event;

    if (state == NULL || !GTK_IS_PANED(widget)) {
        return FALSE;
    }

    paned_width = gtk_widget_get_allocated_width(widget);
    position = gtk_paned_get_position(GTK_PANED(widget));
    clamped = ui_layout_clamp_paned_position(paned_width, position);

    if (clamped != position) {
        clamped = ui_layout_clamp_paned_position(paned_width, paned_width / 2);
        gtk_paned_set_position(GTK_PANED(widget), clamped);
    }

    ui_layout_state_update_from_position(state, paned_width, clamped);
    ui_layout_state_save_position(state->current_position);
    return FALSE;
}

static G_GNUC_UNUSED void on_paned_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
    UiLayoutState *state = (UiLayoutState *)user_data;
    gint width;

    if (state == NULL || !GTK_IS_PANED(widget) || allocation == NULL || state->applying_resize) {
        return;
    }

    width = allocation->width;
    if (!gtk_widget_get_visible(widget) || width < 620) {
        return;
    }

    if (!state->initial_position_applied || width != state->last_paned_width) {
        ui_layout_apply_position_to_paned(state, GTK_PANED(widget), width, TRUE);
    }
}

static void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)widget;

    if (app == NULL || allocation == NULL) {
        return;
    }

    ui_layout_apply_sidebar_width(app, allocation->width);
}

static gint app_state_info_priority(GtkMessageType type)
{
    switch (type) {
    case GTK_MESSAGE_ERROR:
        return 3;
    case GTK_MESSAGE_WARNING:
        return 2;
    case GTK_MESSAGE_INFO:
        return 1;
    default:
        return 1;
    }
}

static const char *ui_action_icon_name(UiActionIcon icon)
{
    switch (icon) {
    case UI_ACTION_ICON_CONNECT:
        return "gtk-add";
    case UI_ACTION_ICON_DISCONNECT:
        return "gtk-remove";
    case UI_ACTION_ICON_REFRESH:
        return "view-refresh";
    case UI_ACTION_ICON_OPEN_LOCAL_FILE:
        return "gtk-open";
    default:
        return "image-missing";
    }
}

static const char *ui_operation_status_text(UiOperationStatus status)
{
    switch (status) {
    case UI_OPERATION_STATUS_CONNECTING:
        return _("Подключение…");
    case UI_OPERATION_STATUS_DISCONNECTING:
        return _("Отключение…");
    case UI_OPERATION_STATUS_CANCELLED:
        return _("Отменено");
    case UI_OPERATION_STATUS_ERROR:
        return _("Ошибка: ");
    case UI_OPERATION_STATUS_READY:
        return _("Готово");
    case UI_OPERATION_STATUS_OPENING:
        return _("Открытие файла…");
    default:
        return _("Готово");
    }
}

static char *ui_operation_status_message_dup(UiOperationStatus status, const char *detail)
{
    const char *base = ui_operation_status_text(status);

    if (status == UI_OPERATION_STATUS_ERROR) {
        if (detail != NULL && detail[0] != '\0') {
            return g_strdup_printf("%s%s", base, detail);
        }
        return g_strdup(base);
    }

    return g_strdup(base);
}

void app_state_set_operation_status(AppState *app, int status, const char *detail)
{
    char *message;

    if (app == NULL || app->status_label == NULL) {
        return;
    }

    message = ui_operation_status_message_dup((UiOperationStatus)status, detail);
    gtk_label_set_text(app->status_label, message != NULL ? message : "");
    g_free(message);

    app_state_clear_info_timer(app);
    app_state_clear_info_action(app);
    app->info_priority = app_state_info_priority(GTK_MESSAGE_INFO);
    app->info_message_type = GTK_MESSAGE_INFO;

    if (app->info_bar != NULL && gtk_widget_get_visible(app->info_bar)) {
        gtk_widget_hide(app->info_bar);
    }
}

static void app_state_clear_info_timer(AppState *app)
{
    if (app == NULL || app->info_hide_timer == 0U) {
        return;
    }

    g_source_remove(app->info_hide_timer);
    app->info_hide_timer = 0U;
}

static gboolean app_state_progress_tick(gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    char *content = NULL;
    gsize len = 0;
    char *name = NULL;
    char *stage = NULL;
    char *pct = NULL;
    gdouble fraction = 0.0;
    gboolean has_fraction = FALSE;

    if (app == NULL || app->progress_file_path == NULL || app->status_label == NULL) {
        return G_SOURCE_CONTINUE;
    }

    if (!g_file_get_contents(app->progress_file_path, &content, &len, NULL)) {
        return G_SOURCE_CONTINUE;
    }

    {
        char **lines = g_strsplit(content, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "name=")) {
                g_free(name);
                name = g_strdup(lines[i] + 5);
            } else if (g_str_has_prefix(lines[i], "stage=")) {
                g_free(stage);
                stage = g_strdup(lines[i] + 6);
            } else if (g_str_has_prefix(lines[i], "pct=")) {
                g_free(pct);
                pct = g_strdup(lines[i] + 4);
            }
        }
        g_strfreev(lines);
    }
    g_free(content);

    if (name != NULL && pct != NULL) {
        const char *verb = (stage != NULL && g_strcmp0(stage, "download") == 0) ? _("Downloading") : _("Processing");
        char *msg = g_strdup_printf("%s %s: %s%%", verb, name, pct);
        gtk_label_set_text(app->status_label, msg);
        g_free(msg);
    }

    if (app->progress_bar != NULL && pct != NULL) {
        char *end = NULL;
        gdouble pct_value = g_ascii_strtod(pct, &end);

        if (end != pct) {
            fraction = CLAMP(pct_value / 100.0, 0.0, 1.0);
            has_fraction = TRUE;
        }
    }

    if (app->progress_bar != NULL && has_fraction) {
        gtk_progress_bar_set_fraction(app->progress_bar, fraction);
    }

    g_free(name);
    g_free(stage);
    g_free(pct);
    return G_SOURCE_CONTINUE;
}

static void app_state_progress_arm(AppState *app)
{
    char *path;

    if (app == NULL) {
        return;
    }

    if (app->progress_timer_id != 0U) {
        g_source_remove(app->progress_timer_id);
        app->progress_timer_id = 0U;
    }
    if (app->progress_file_path != NULL) {
        g_unlink(app->progress_file_path);
        g_free(app->progress_file_path);
        app->progress_file_path = NULL;
    }

    path = g_strdup_printf("/tmp/modman-gui-progress-%u-%u",
                           (unsigned)getpid(),
                           g_random_int());
    g_file_set_contents(path, "", 0, NULL);
    g_setenv("MODMAN_PROGRESS_FILE", path, TRUE);
    app->progress_file_path = path;

    if (app->progress_bar != NULL) {
        gtk_progress_bar_set_fraction(app->progress_bar, 0.0);
        gtk_widget_show(GTK_WIDGET(app->progress_bar));
    }

    app->progress_timer_id = g_timeout_add(250, app_state_progress_tick, app);
}

static void app_state_progress_disarm(AppState *app)
{
    if (app == NULL) {
        return;
    }

    if (app->progress_timer_id != 0U) {
        g_source_remove(app->progress_timer_id);
        app->progress_timer_id = 0U;
    }
    if (app->progress_file_path != NULL) {
        g_unlink(app->progress_file_path);
        g_free(app->progress_file_path);
        app->progress_file_path = NULL;
    }

    if (app->progress_bar != NULL) {
        gtk_progress_bar_set_fraction(app->progress_bar, 0.0);
        gtk_widget_hide(GTK_WIDGET(app->progress_bar));
    }

    g_unsetenv("MODMAN_PROGRESS_FILE");
}

static void app_state_clear_info_action(AppState *app)
{
    if (app == NULL) {
        return;
    }

    app->info_action_cb = NULL;

    if (app->info_action == NULL) {
        return;
    }

    gtk_widget_destroy(app->info_action);
    app->info_action = NULL;
}

static char *ui_strip_ansi_sequences(const char *text)
{
    GString *clean;
    const char *p;

    if (text == NULL) {
        return g_strdup("");
    }

    clean = g_string_new(NULL);
    p = text;
    while (*p != '\0') {
        if (g_str_has_prefix(p, "\342\220\233")) {
            p += 3;
            if (*p == '[') {
                p++;
                while (*p != '\0' && !((*p >= '@' && *p <= '~'))) {
                    p++;
                }
                if (*p != '\0') {
                    p++;
                }
            }
            continue;
        }

        if ((guchar)*p == 0x1b) {
            p++;
            if (*p == '[') {
                p++;
                while (*p != '\0' && !((*p >= '@' && *p <= '~'))) {
                    p++;
                }
                if (*p != '\0') {
                    p++;
                }
                continue;
            }
            if (*p == ']') {
                p++;
                while (*p != '\0' && *p != '\a') {
                    if ((guchar)*p == 0x1b && *(p + 1) == '\\') {
                        p += 2;
                        break;
                    }
                    p++;
                }
                if (*p == '\a') {
                    p++;
                }
                continue;
            }
            if (*p != '\0') {
                p++;
            }
            continue;
        }

        g_string_append_c(clean, *p);
        p++;
    }

    return g_string_free(clean, FALSE);
}

static void ui_busy_process_free(gpointer data)
{
    UiBusyProcess *process = (UiBusyProcess *)data;

    if (process == NULL) {
        return;
    }

    g_free(process->command);
    g_free(process->path);
    g_free(process);
}

static char *ui_mountpoint_for_module_target(const char *target)
{
    char *mounts = NULL;
    char *target_base = NULL;
    char *target_real = NULL;
    char *target_stem = NULL;
    char *result = NULL;
    gchar **lines;

    if (target == NULL || target[0] == '\0') {
        return NULL;
    }

    if (g_file_test(target, G_FILE_TEST_IS_DIR)) {
        return g_strdup(target);
    }

    target_real = realpath(target, NULL);
    target_base = g_path_get_basename(target_real != NULL ? target_real : target);
    if (target_base == NULL || target_base[0] == '\0') {
        free(target_real);
        g_free(target_base);
        return NULL;
    }

    target_stem = g_strdup(target_base);
    if (g_str_has_suffix(target_stem, ".pfs")) {
        target_stem[strlen(target_stem) - 4U] = '\0';
    }

    if (!g_file_get_contents("/proc/mounts", &mounts, NULL, NULL)) {
        free(target_real);
        g_free(target_base);
        return NULL;
    }

    lines = g_strsplit(mounts, "\n", -1);
    for (guint i = 0; lines[i] != NULL && result == NULL; i++) {
        gchar **fields;

        if (strstr(lines[i], target_base) == NULL &&
            (target_stem == NULL || target_stem[0] == '\0' || strstr(lines[i], target_stem) == NULL)) {
            continue;
        }

        fields = g_strsplit(lines[i], " ", 4);
        if (fields[1] != NULL && fields[1][0] != '\0') {
            result = g_strdup(fields[1]);
        }
        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(mounts);
    if (result == NULL && target_stem != NULL && target_stem[0] != '\0') {
        char *mnt_candidate = g_strdup_printf("/mnt/.%s", target_stem);
        if (g_file_test(mnt_candidate, G_FILE_TEST_IS_DIR)) {
            result = mnt_candidate;
        } else {
            g_free(mnt_candidate);
        }
    }
    free(target_real);
    g_free(target_stem);
    g_free(target_base);
    return result;
}

static gboolean ui_process_array_has_pid(GPtrArray *processes, GPid pid)
{
    if (processes == NULL) {
        return FALSE;
    }

    for (guint i = 0; i < processes->len; i++) {
        UiBusyProcess *process = g_ptr_array_index(processes, i);
        if (process != NULL && process->pid == pid) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean ui_mountpoint_has_executable_for_path(const char *mountpoint, const char *path)
{
    char *candidate;
    gboolean exists;

    if (mountpoint == NULL || mountpoint[0] == '\0' || path == NULL || path[0] != '/') {
        return FALSE;
    }

    candidate = g_build_filename(mountpoint, path + 1, NULL);
    exists = candidate != NULL && g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE);
    g_free(candidate);
    return exists;
}

static gboolean ui_klsof_candidate_path_is_allowed(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return FALSE;
    }

    if (g_str_has_prefix(path, "/proc") ||
        g_str_has_prefix(path, "/sys") ||
        g_str_has_prefix(path, "/run") ||
        g_str_has_prefix(path, "/tmp") ||
        g_str_has_prefix(path, "/dev") ||
        g_str_has_prefix(path, "/home") ||
        strstr(path, ".so.") != NULL ||
        g_str_has_suffix(path, ".so")) {
        return FALSE;
    }

    return g_file_test(path, G_FILE_TEST_IS_EXECUTABLE) &&
           !g_file_test(path, G_FILE_TEST_IS_DIR);
}

static void ui_busy_processes_add_klsof_candidate(GPtrArray *processes, GPid pid, const char *path)
{
    UiBusyProcess *process;
    char *command;

    if (processes == NULL || pid <= 0 || path == NULL || path[0] == '\0' || ui_process_array_has_pid(processes, pid)) {
        return;
    }

    command = g_path_get_basename(path);
    process = g_new0(UiBusyProcess, 1);
    process->pid = pid;
    process->command = command != NULL && command[0] != '\0' ? command : g_strdup_printf("PID %d", (int)pid);
    process->path = g_strdup(path);
    g_ptr_array_add(processes, process);
    if (process->command != command) {
        g_free(command);
    }
}

static GPtrArray *ui_busy_processes_find(const char *mountpoint)
{
    GPtrArray *processes;
    GSubprocess *process;
    GError *error = NULL;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    gchar **lines;

    processes = g_ptr_array_new_with_free_func(ui_busy_process_free);
    if (mountpoint == NULL || mountpoint[0] == '\0') {
        return processes;
    }

    process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                               &error,
                               "lsof",
                               "/",
                               NULL);
    if (process == NULL) {
        g_clear_error(&error);
        return processes;
    }

    if (!g_subprocess_communicate_utf8(process, NULL, NULL, &stdout_text, &stderr_text, &error)) {
        g_clear_error(&error);
        g_free(stderr_text);
        g_object_unref(process);
        return processes;
    }

    lines = g_strsplit(stdout_text != NULL ? stdout_text : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar **fields;
        char *endptr = NULL;
        long pid_long;
        const char *pid_text = NULL;
        const char *path = NULL;
        guint visible_field = 0U;

        fields = g_strsplit_set(lines[i], " \t", 0);
        for (guint field_index = 0; fields[field_index] != NULL; field_index++) {
            if (fields[field_index][0] == '\0') {
                continue;
            }
            visible_field++;
            if (visible_field == 2U) {
                pid_text = fields[field_index];
            } else if (visible_field == 9U) {
                path = fields[field_index];
                break;
            }
        }

        if (pid_text == NULL || path == NULL) {
            g_strfreev(fields);
            continue;
        }

        pid_long = strtol(pid_text, &endptr, 10);
        if (pid_text[0] != '\0' && endptr != NULL && *endptr == '\0' && pid_long > 0 &&
            ui_klsof_candidate_path_is_allowed(path) &&
            ui_mountpoint_has_executable_for_path(mountpoint, path)) {
            ui_busy_processes_add_klsof_candidate(processes, (GPid)pid_long, path);
        }

        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(stderr_text);
    g_free(stdout_text);
    g_object_unref(process);
    return processes;
}

static void ui_show_busy_process_dialog(AppState *app, const char *target)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GPtrArray *processes;
    GPtrArray *checks = NULL;
    char *mountpoint;
    gint response;

    mountpoint = ui_mountpoint_for_module_target(target);
    processes = ui_busy_processes_find(mountpoint);

    dialog = gtk_dialog_new_with_buttons(_("Busy processes"),
                                         app != NULL && app->window != NULL ? GTK_WINDOW(app->window) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         _("_Close"),
                                         GTK_RESPONSE_CLOSE,
                                         NULL);
    {
        GtkWidget *btn_kill_sel = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Terminate Selected"), GTK_RESPONSE_ACCEPT);
        gtk_button_set_image(GTK_BUTTON(btn_kill_sel), ui_icon_image_or_fallback("process-stop", "process-stop-symbolic"));
        gtk_button_set_always_show_image(GTK_BUTTON(btn_kill_sel), TRUE);
    }
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    if (processes->len == 0U) {
        GtkWidget *label = gtk_label_new(mountpoint == NULL
            ? _("Could not determine the module mount point or find processes by name. Close programs using module files and retry unloading.")
            : _("No busy processes found. Close programs that may have opened module files and retry unloading."));
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_widget_set_margin_top(label, 12);
        gtk_widget_set_margin_bottom(label, 12);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, FALSE);
    } else {
        GtkWidget *label = gtk_label_new(_("Select programs to terminate:"));
        GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

        checks = g_ptr_array_new();
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_margin_top(label, 12);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
        gtk_widget_set_size_request(scroller, 560, 260);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroller), box);

        for (guint i = 0; i < processes->len; i++) {
            UiBusyProcess *process = g_ptr_array_index(processes, i);
            char *label_text = g_strdup_printf("%s (PID %d)\n%s", process->command, (int)process->pid, process->path);
            GtkWidget *check = gtk_check_button_new_with_label(label_text);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);
            g_object_set_data(G_OBJECT(check), "busy-process", process);
            gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
            g_ptr_array_add(checks, check);
            g_free(label_text);
        }

        gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 8);
    }

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT && checks != NULL) {
        for (guint i = 0; i < checks->len; i++) {
            GtkWidget *check = g_ptr_array_index(checks, i);
            UiBusyProcess *process = g_object_get_data(G_OBJECT(check), "busy-process");

            if (process != NULL && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
                kill(process->pid, SIGKILL);
            }
        }
    }

    gtk_widget_destroy(dialog);
    if (checks != NULL) {
        g_ptr_array_unref(checks);
    }
    g_ptr_array_unref(processes);
    g_free(mountpoint);
}

static void ui_show_unload_failure_dialog(AppState *app, const UiAsyncContext *ctx, const GError *error)
{
    GtkWidget *dialog;
    char *title;
    char *secondary;
    char *clean_error;
    const char *module_name;
    const char *target;
    gint response;

    if (app == NULL) {
        return;
    }

    app_state_hide_info(app);

    module_name = ctx != NULL && ctx->module_name != NULL && ctx->module_name[0] != '\0'
        ? ctx->module_name
        : _("unnamed");
    target = ctx != NULL && ctx->module_path != NULL && ctx->module_path[0] != '\0'
        ? ctx->module_path
        : module_name;
    clean_error = ui_strip_ansi_sequences(error != NULL ? error->message : NULL);
    title = g_strdup_printf(_("Failed to detach module “%s”"), module_name);
    secondary = g_strdup_printf("%s\n\n%s",
                                _("Module files may be busy in running programs. Close those programs or terminate them here, then retry unloading."),
                                clean_error != NULL && clean_error[0] != '\0'
                                    ? clean_error
                                    : _("The backend did not return a detailed error message."));

    dialog = gtk_message_dialog_new(app->window != NULL ? GTK_WINDOW(app->window) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_NONE,
                                    "%s",
                                    title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    {
        GtkWidget *btn_dlg_close = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Close"), GTK_RESPONSE_CLOSE);
        gtk_button_set_image(GTK_BUTTON(btn_dlg_close), ui_icon_image_or_fallback("window-close", "window-close-symbolic"));
        gtk_button_set_always_show_image(GTK_BUTTON(btn_dlg_close), TRUE);
    }
    {
        GtkWidget *btn_dlg_kill = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Terminate Processes…"), GTK_RESPONSE_ACCEPT);
        gtk_button_set_image(GTK_BUTTON(btn_dlg_kill), ui_icon_image_or_fallback("process-stop", "process-stop-symbolic"));
        gtk_button_set_always_show_image(GTK_BUTTON(btn_dlg_kill), TRUE);
    }

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        ui_show_busy_process_dialog(app, target);
    }

    app_state_set_operation_status(app, UI_OPERATION_STATUS_ERROR, clean_error);

    g_free(clean_error);
    g_free(title);
    g_free(secondary);
}

static void ui_show_load_failure_dialog(AppState *app, const UiAsyncContext *ctx, const GError *error)
{
    GtkWidget *dialog;
    char *title;
    char *secondary;
    char *clean_error;
    const char *module_name;
    const char *target;

    if (app == NULL) {
        return;
    }

    app_state_hide_info(app);

    module_name = ctx != NULL && ctx->module_name != NULL && ctx->module_name[0] != '\0'
        ? ctx->module_name
        : _("unnamed");
    target = ctx != NULL && ctx->module_path != NULL && ctx->module_path[0] != '\0'
        ? ctx->module_path
        : module_name;
    clean_error = ui_strip_ansi_sequences(error != NULL ? error->message : NULL);
    title = g_strdup_printf(_("Failed to attach module “%s”"), module_name);
    secondary = g_strdup_printf(_("Target: %s\\n\\n%s"),
                                target,
                                clean_error != NULL && clean_error[0] != '\0'
                                    ? clean_error
                                    : _("The backend did not return a detailed error message."));

    dialog = gtk_message_dialog_new(app->window != NULL ? GTK_WINDOW(app->window) : NULL,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    "%s",
                                    title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    app_state_set_operation_status(app, UI_OPERATION_STATUS_ERROR, clean_error);

    g_free(clean_error);
    g_free(title);
    g_free(secondary);
}

static gboolean app_state_uses_overlay_session_model(const AppState *app)
{
    return app != NULL && app->capabilities.layering_mode == CAP_LAYERING_OVERLAY;
}

static G_GNUC_UNUSED gboolean app_state_has_systemd(const AppState *app)
{
    return app != NULL && app->capabilities.has_systemd;
}

static gboolean app_state_can_run_backend(const AppState *app)
{
    return app != NULL && app->capabilities.has_modman;
}

static gboolean app_state_can_modify_modules(const AppState *app)
{
    return app != NULL && capabilities_supports_runtime_ops(&app->capabilities);
}

static void app_state_apply_capability_gates(AppState *app)
{
    GString *warnings;
    char *markup;

    if (app == NULL || app->info_bar == NULL) {
        return;
    }

    app->read_only_mode = !app_state_can_modify_modules(app);

    warnings = g_string_new(NULL);

    if (!app->capabilities.has_modman) {
        g_string_append(warnings, _("• modman was not found in PATH or MODMAN_BIN. Install modman.\\n"));
    }
    if (!app->capabilities.has_pfsinfo) {
        g_string_append(warnings, _("• runtime metadata helper was not found — showing safe mode only.\\n"));
    }
    if (!app->capabilities.has_aufs && !app->capabilities.has_overlayfs) {
    g_string_append(warnings, _("• AUFS and OverlayFS were not detected — attach/detach operations are disabled.\\n"));
    }

    if (warnings->len > 0U) {
        markup = g_strdup_printf(_("<b>Limited mode</b>\\n%s"), warnings->str);
        app_state_show_info(app,
                            app->capabilities.has_modman ? GTK_MESSAGE_WARNING : GTK_MESSAGE_ERROR,
                            markup,
                            NULL,
                            NULL,
                            FALSE);
        g_free(markup);
    }

    if (!app->capabilities.has_pkexec) {
        app_state_show_info(app,
                            GTK_MESSAGE_WARNING,
                            _("pkexec was not found — privileged actions are unavailable."),
                            NULL,
                            NULL,
                            FALSE);
    }

    if (!app->capabilities.has_aufs && !app->capabilities.has_overlayfs) {
        app_state_show_info(app,
                            GTK_MESSAGE_ERROR,
                            _("AUFS and OverlayFS were not detected — attaching and detaching modules is unavailable."),
                            NULL,
                            NULL,
                            FALSE);
    }

    if (!app->capabilities.has_systemd) {
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            _("systemd was not detected: module autoload management is disabled."),
                            NULL,
                            NULL,
                            TRUE);
    }

    if (app_state_uses_overlay_session_model(app)) {
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            _("OverlayFS: attached modules are visible only to new processes."),
                            NULL,
                            NULL,
                            TRUE);
    }

    g_string_free(warnings, TRUE);
}

void app_state_hide_info(AppState *app)
{
    if (app == NULL || app->info_bar == NULL) {
        return;
    }

    app_state_clear_info_timer(app);
    app_state_clear_info_action(app);
    app->info_priority = 0;
    app->info_message_type = GTK_MESSAGE_OTHER;
    gtk_widget_hide(app->info_bar);
}

void app_state_show_info(AppState *app,
                         GtkMessageType type,
                         const char *markup,
                         const char *action_label,
                         GCallback action_cb,
                         gboolean auto_hide_8s)
{
    const char *prefix;
    char *combined;

    (void)action_label;
    (void)action_cb;
    (void)auto_hide_8s;

    if (app == NULL || app->status_label == NULL) {
        return;
    }

    /* Single-row status policy: every message goes to the bottom status_label,
     * top info-bar is never shown. Type-prefix preserves severity hint.
     * Action_label/action_cb are deliberately dropped — re-introduce as an
     * inline status_box button when policy allows. */
    if (type == GTK_MESSAGE_ERROR) {
        app_state_set_operation_status(app, UI_OPERATION_STATUS_ERROR, markup);
        return;
    }

    switch (type) {
        case GTK_MESSAGE_WARNING:
            prefix = "<span foreground=\"#ed6c02\">\u26a0</span> ";
            break;
        case GTK_MESSAGE_QUESTION:
            prefix = "<span foreground=\"#1976d2\">?</span> ";
            break;
        case GTK_MESSAGE_INFO:
        case GTK_MESSAGE_OTHER:
        default:
            prefix = "";
            break;
    }

    app_state_clear_info_timer(app);
    app_state_clear_info_action(app);
    app->info_priority = app_state_info_priority(type);
    app->info_message_type = type;

    if (markup != NULL && markup[0] != '\0') {
        combined = g_strdup_printf("%s%s", prefix, markup);
        gtk_label_set_markup(app->status_label, combined);
        g_free(combined);
    } else if (prefix[0] != '\0') {
        gtk_label_set_markup(app->status_label, prefix);
    } else {
        gtk_label_set_text(app->status_label, "");
    }

    if (app->info_bar != NULL && gtk_widget_get_visible(app->info_bar)) {
        gtk_widget_hide(app->info_bar);
    }
}

static gboolean app_state_open_initial_system_section_idle(gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    if (app == NULL || app->stack == NULL) {
        return G_SOURCE_REMOVE;
    }

    if (app->initial_query != NULL && app->initial_query[0] != '\0') {
        gtk_stack_set_visible_child_name(app->stack, "inet");
        if (app->search_entry != NULL) {
            g_signal_handlers_block_by_func(app->search_entry, G_CALLBACK(on_toolbar_search_changed), app);
            gtk_entry_set_text(GTK_ENTRY(app->search_entry), app->initial_query);
            g_signal_handlers_unblock_by_func(app->search_entry, G_CALLBACK(on_toolbar_search_changed), app);
        }
        g_free(app->pending_query);
        app->pending_query = g_strdup(app->initial_query);
        ui_focus_search_entry(app);
        g_clear_pointer(&app->initial_query, g_free);
        return G_SOURCE_REMOVE;
    }

    if (g_strcmp0(ui_current_section_id(app), "inet") == 0) {
        app_state_load_current_section(app);
    } else {
        gtk_stack_set_visible_child_name(app->stack, "inet");
    }
    ui_focus_search_entry(app);
    return G_SOURCE_REMOVE;
}

static void app_state_start_op(AppState *app)
{
    if (app == NULL) {
        return;
    }

    app_state_cancel_current_op(app, FALSE);

    app->current_op = g_cancellable_new();
}

static void app_state_cancel_cancellable(GCancellable **slot)
{
    if (slot == NULL || *slot == NULL) {
        return;
    }

    g_cancellable_cancel(*slot);
    g_object_unref(*slot);
    *slot = NULL;
}

static void app_state_cancel_current_op(AppState *app, gboolean show_cancelled)
{
    gboolean had_op;

    if (app == NULL) {
        return;
    }

    had_op = app->current_op != NULL;
    app_state_cancel_cancellable(&app->current_op);
    app_state_progress_disarm(app);

    if (had_op && show_cancelled) {
        app_state_set_operation_status(app, UI_OPERATION_STATUS_CANCELLED, NULL);
    }
}

static void on_info_action_refresh(GtkWidget *widget, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)widget;

    app_state_load_current_section(app);
}

static void on_info_action_authorize(GtkWidget *widget, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)widget;

    if (app == NULL) {
        return;
    }

    app_state_load_current_section(app);
}

static gboolean app_error_is_permission(const GError *error)
{
    gchar *lower_message;
    gboolean is_permission;

    if (error == NULL) {
        return FALSE;
    }

    if (error->domain == MODMAN_GUI_ERROR_DOMAIN && error->code == MODMAN_GUI_ERR_PERMISSION) {
        return TRUE;
    }

    if (error->message == NULL) {
        return FALSE;
    }

    /* Match only specific privilege-related phrases, NOT bare tokens like
     * "root", "pkexec", or "sudo". The previous loose match produced false
     * positives because backend always prefixes failures with "pkexec ..."
     * (the cmd_str), causing every transient failure (network blip, mirror
 * timeout, missing helper dependencies, etc.) to be misclassified as a
 * permission issue and surfaced as "Требуются root-права для выполнения операции". */
    lower_message = g_ascii_strdown(error->message, -1);
    is_permission = strstr(lower_message, "permission denied") != NULL ||
                    strstr(lower_message, "must be superuser") != NULL ||
                    strstr(lower_message, "not authorized") != NULL ||
                    strstr(lower_message, "operation not permitted") != NULL ||
                    strstr(lower_message, "authentication is required") != NULL ||
                    strstr(lower_message, "authentication failed") != NULL ||
                    strstr(lower_message, "отказано в доступе") != NULL ||
                    strstr(lower_message, "недостаточно прав") != NULL ||
                    strstr(lower_message, "требуются права root") != NULL ||
                    strstr(lower_message, "требуются root-права") != NULL ||
                    strstr(lower_message, "необходимы права root") != NULL;
    g_free(lower_message);
    return is_permission;
}

static gboolean app_state_is_stale_op(AppState *app, GCancellable *op)
{
    if (app == NULL || op == NULL) {
        return TRUE;
    }

    if (app->current_op != op) {
        return TRUE;
    }

    return g_cancellable_is_cancelled(op);
}

static UiAsyncContext *ui_async_context_new(AppState *app)
{
    UiAsyncContext *ctx;

    if (app == NULL || app->current_op == NULL) {
        return NULL;
    }

    ctx = g_new0(UiAsyncContext, 1);
    ctx->app = app;
    ctx->op = g_object_ref(app->current_op);
    return ctx;
}

static UiAsyncContext *ui_async_context_new_for_op(AppState *app, GCancellable *op)
{
    UiAsyncContext *ctx;

    if (app == NULL || op == NULL) {
        return NULL;
    }

    ctx = g_new0(UiAsyncContext, 1);
    ctx->app = app;
    ctx->op = g_object_ref(op);
    return ctx;
}

static void ui_async_context_free(UiAsyncContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->op != NULL) {
        g_object_unref(ctx->op);
    }

    g_free(ctx->module_name);
    g_free(ctx->module_path);
    g_strfreev(ctx->module_names);
    g_strfreev(ctx->module_args);
    if (ctx->module_failures != NULL) {
        g_string_free(ctx->module_failures, TRUE);
    }
    g_free(ctx->inet_modman_bin);

    g_free(ctx);
}

static void on_confirm_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    ConfirmDangerousState *state = (ConfirmDangerousState *)user_data;

    (void)dialog;

    if (state == NULL) {
        return;
    }

    state->accepted = (response_id == GTK_RESPONSE_ACCEPT);
    if (state->loop != NULL && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

static gboolean on_confirm_dialog_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)user_data;

    if (widget == NULL || event == NULL) {
        return FALSE;
    }

    switch (event->keyval) {
    case GDK_KEY_Escape:
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
        gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

gboolean app_state_confirm_dangerous(AppState *app,
                                     const char *title,
                                     const char *body,
                                     const char *danger_action_label)
{
    GtkWidget *dialog;
    GtkWidget *cancel_button;
    GtkWidget *accept_button;
    const char *dialog_title;
    const char *dialog_body;
    const char *action_label;
    ConfirmDangerousState state = {0};

    if (app == NULL || app->window == NULL) {
        return FALSE;
    }

    dialog_title = (title != NULL && title[0] != '\0')
        ? title
        : _("⚠  Confirm dangerous action?");
    dialog_body = (body != NULL && body[0] != '\0')
        ? body
        : _("This operation may affect the running system. Continue?");
    action_label = (danger_action_label != NULL && danger_action_label[0] != '\0')
        ? danger_action_label
        : _("_Continue");

    dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_NONE,
                                    "%s",
                                    dialog_title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), dialog_title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", dialog_body);
    ui_set_widget_accessible_with_role(dialog,
                                       dialog_title,
                                       dialog_body,
                                       ATK_ROLE_ALERT);

    cancel_button = gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_button_set_image(GTK_BUTTON(cancel_button), ui_icon_image_or_fallback("window-close", "window-close-symbolic"));
    gtk_button_set_always_show_image(GTK_BUTTON(cancel_button), TRUE);
    accept_button = gtk_dialog_add_button(GTK_DIALOG(dialog), action_label, GTK_RESPONSE_ACCEPT);
    gtk_button_set_image(GTK_BUTTON(accept_button), ui_icon_image_or_fallback("dialog-ok", "dialog-ok-symbolic"));
    gtk_button_set_always_show_image(GTK_BUTTON(accept_button), TRUE);
    gtk_button_set_use_underline(GTK_BUTTON(accept_button), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(accept_button), "destructive-action");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

    ui_set_widget_accessible(cancel_button,
                             _("Cancel dangerous action"),
                             _("Close the warning and do not perform the action."));
    {
        char *action_name = ui_accessible_display_label(action_label);
        char *accept_title = g_strdup_printf("%s: %s",
                                             action_name[0] != '\0' ? action_name : _("Confirm action"),
                                             dialog_title);
        char *accept_desc = g_strdup_printf(_("Confirm the dangerous action. %s"), dialog_body);

        ui_set_widget_accessible(accept_button, accept_title, accept_desc);
        g_free(accept_desc);
        g_free(accept_title);
        g_free(action_name);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(on_confirm_dialog_response), &state);
    g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_confirm_dialog_key_press), NULL);

    gtk_widget_show_all(dialog);
    gtk_widget_set_can_default(cancel_button, TRUE);
    gtk_widget_grab_default(cancel_button);
    gtk_widget_grab_focus(cancel_button);

    state.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(state.loop);
    g_main_loop_unref(state.loop);
    state.loop = NULL;

    gtk_widget_destroy(dialog);
    return state.accepted;
}

static void ui_module_info_free(ModuleInfo *module)
{
    if (module == NULL) {
        return;
    }

    g_free(module->name);
    g_free(module->layer);
    g_free(module->path);
    g_free(module->desc);
    g_free(module->category);
    g_free(module->version);
    g_free(module->repo);
    g_free(module->mount_mode);
    g_free(module->mount_in_ram);
    g_free(module);
}

static ModuleInfo *ui_module_info_dup(const ModuleInfo *module)
{
    ModuleInfo *copy;

    if (module == NULL) {
        return NULL;
    }

    copy = g_new0(ModuleInfo, 1);
    copy->name = g_strdup(module->name);
    copy->layer = g_strdup(module->layer);
    copy->path = g_strdup(module->path);
    copy->desc = g_strdup(module->desc);
    copy->category = g_strdup(module->category);
    copy->version = g_strdup(module->version);
    copy->repo = g_strdup(module->repo);
    copy->size_mb = module->size_mb;
    copy->is_local = module->is_local;
    copy->is_autoload = module->is_autoload;
    copy->has_update = module->has_update;
    copy->has_error = module->has_error;
    copy->mount_mode = g_strdup(module->mount_mode);
    copy->mount_in_ram = g_strdup(module->mount_in_ram);

    return copy;
}

static GPtrArray *ui_module_info_array_dup(const GPtrArray *items)
{
    GPtrArray *dup_items;
    guint i;

    dup_items = g_ptr_array_new_with_free_func((GDestroyNotify)ui_module_info_free);
    if (items == NULL) {
        return dup_items;
    }

    for (i = 0; i < items->len; i++) {
        ModuleInfo *module = (ModuleInfo *)g_ptr_array_index((GPtrArray *)items, i);
        ModuleInfo *copy;

        if (module == NULL) {
            continue;
        }

        copy = ui_module_info_dup(module);

        if (copy == NULL) {
            g_ptr_array_unref(dup_items);
            return NULL;
        }

        g_ptr_array_add(dup_items, copy);
    }

    return dup_items;
}

static const char *ui_inet_cache_db_path(const AppState *app)
{
    if (app != NULL && app->conf.cache_file != NULL && app->conf.cache_file[0] != '\0') {
        return app->conf.cache_file;
    }

    return UI_INET_DB_FALLBACK_PATH;
}

static gint64 ui_inet_cache_current_db_mtime(const AppState *app)
{
    GStatBuf st;
    const char *cache_path = ui_inet_cache_db_path(app);

    if (cache_path == NULL || cache_path[0] == '\0') {
        return UI_INET_CACHE_MTIME_UNKNOWN;
    }

    if (g_stat(cache_path, &st) != 0) {
        return UI_INET_CACHE_MTIME_UNKNOWN;
    }

    return (gint64)st.st_mtime;
}

static char *ui_inet_cache_effective_modman_bin_dup(const AppState *app)
{
    const char *env_modman_bin = g_getenv("MODMAN_BIN");

    if (env_modman_bin != NULL && env_modman_bin[0] != '\0') {
        return g_strdup(env_modman_bin);
    }

    if (app != NULL && app->conf.modman_bin != NULL && app->conf.modman_bin[0] != '\0') {
        return g_strdup(app->conf.modman_bin);
    }

    return g_strdup(UI_MODMAN_BIN_FALLBACK);
}

static void ui_inet_cache_clear(AppState *app)
{
    if (app == NULL) {
        return;
    }

    if (app->inet_cache != NULL) {
        g_ptr_array_unref(app->inet_cache);
        app->inet_cache = NULL;
    }

    g_clear_pointer(&app->inet_cache_modman_bin, g_free);
    app->inet_cache_db_mtime = UI_INET_CACHE_MTIME_UNKNOWN;
}

static GtkListBoxRow *ui_module_to_row(AppState *app,
                                       const char *section_id,
                                       ModuleInfo *module)
{
    GtkWidget *row;

    if (module == NULL) {
        return NULL;
    }

    row = ui_build_module_row_for_section(app, module, section_id);
    if (row == NULL) {
        return NULL;
    }

    g_object_set_data_full(G_OBJECT(row),
                           "module-info",
                           module,
                           (GDestroyNotify)ui_module_info_free);
    g_object_set_data_full(G_OBJECT(row),
                           "section-id",
                           g_strdup(section_id != NULL ? section_id : ""),
                           g_free);

    return GTK_LIST_BOX_ROW(row);
}

static void ui_render_items_to_section(AppState *app,
                                       const char *section_id,
                                       GPtrArray *items)
{
    gint section_idx;
    GtkListBox *list_box;
    GList *children;
    GList *iter;
    guint i;

    if (app == NULL || section_id == NULL || items == NULL) {
        return;
    }

    section_idx = section_index_from_id(section_id);
    if (section_idx < 0 || section_idx >= SECTION_COUNT) {
        return;
    }

    list_box = app->list_boxes[section_idx];
    if (list_box == NULL) {
        return;
    }

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for (iter = children; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    g_ptr_array_set_free_func(items, NULL);
    for (i = 0; i < items->len; i++) {
        ModuleInfo *module = (ModuleInfo *)g_ptr_array_index(items, i);
        GtkListBoxRow *row = ui_module_to_row(app, section_id, module);

        if (row != NULL) {
            gtk_list_box_insert(list_box, GTK_WIDGET(row), -1);
        } else {
            ui_module_info_free(module);
        }
    }

    gtk_widget_show_all(GTK_WIDGET(list_box));
    gtk_list_box_invalidate_sort(list_box);
    ui_invalidate_section_filter(app, section_id);

    if (g_strcmp0(section_id, "local") == 0) {
        ui_refresh_local_erofs_markers(app);
    }
}

static void ui_set_inet_placeholder(AppState *app, const char *text)
{
    GtkWidget *placeholder;

    if (app == NULL || app->list_boxes[SECTION_INET] == NULL) {
        return;
    }

    placeholder = g_object_get_data(G_OBJECT(app->list_boxes[SECTION_INET]), "empty-placeholder");
    if (placeholder != NULL && GTK_IS_LABEL(placeholder)) {
        gtk_label_set_text(GTK_LABEL(placeholder), text != NULL ? text : "");
    }
}

static void ui_set_inet_search_state(AppState *app, const char *text)
{
    if (app == NULL) {
        return;
    }

    if (app->status_label != NULL) {
        gtk_label_set_text(app->status_label, text != NULL ? text : "");
    }
    ui_set_inet_placeholder(app, text);
}

static gboolean ui_section_items_changed(AppState *app, const char *section_id, GPtrArray *items)
{
    gint section_idx;
    GtkListBox *list_box;
    GList *children;
    GList *iter;
    guint row_index = 0U;
    gboolean changed = FALSE;

    if (app == NULL || section_id == NULL || items == NULL) {
        return TRUE;
    }

    section_idx = section_index_from_id(section_id);
    if (section_idx < 0 || section_idx >= SECTION_COUNT) {
        return TRUE;
    }

    list_box = app->list_boxes[section_idx];
    if (list_box == NULL) {
        return TRUE;
    }

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    if ((guint)g_list_length(children) != items->len) {
        g_list_free(children);
        return TRUE;
    }

    for (iter = children; iter != NULL; iter = iter->next) {
        ModuleInfo *current_module;
        ModuleInfo *new_module;

        if (!GTK_IS_LIST_BOX_ROW(iter->data) || row_index >= items->len) {
            changed = TRUE;
            break;
        }

        current_module = g_object_get_data(G_OBJECT(iter->data), "module-info");
        new_module = g_ptr_array_index(items, row_index);
        if (g_strcmp0(current_module != NULL ? current_module->name : NULL,
                      new_module != NULL ? new_module->name : NULL) != 0) {
            changed = TRUE;
            break;
        }

        row_index++;
    }

    g_list_free(children);
    return changed;
}

static void on_list_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GPtrArray *items = NULL;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    items = backend_list_loaded_finish(NULL, result, &error);

    if (app_state_is_stale_op(app, ctx->op)) {
        if (error != NULL) {
            g_error_free(error);
        }
        if (items != NULL) {
            g_ptr_array_unref(items);
        }
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        if (error->domain == MODMAN_GUI_ERROR_DOMAIN &&
            error->code == MODMAN_GUI_ERR_CANCELLED) {
            app_state_set_operation_status(app, UI_OPERATION_STATUS_CANCELLED, NULL);
        } else {
            gchar *markup = ui_markup_from_text(error->message);

            app_state_show_info(app,
                                app_error_is_permission(error) ? GTK_MESSAGE_WARNING : GTK_MESSAGE_ERROR,
                                markup,
                                app_error_is_permission(error) ? _("_Authorize") : _("_Retry"),
                                app_error_is_permission(error)
                                    ? G_CALLBACK(on_info_action_authorize)
                                    : G_CALLBACK(on_info_action_refresh),
                                FALSE);
            g_free(markup);
        }
        g_error_free(error);
    } else {
        ui_refresh_local_erofs_markers(app);
    }

    if (items != NULL) {
        const char *section_id = ui_current_section_id(app);

        ui_render_items_to_section(app, section_id, items);
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            ui_operation_status_text(UI_OPERATION_STATUS_READY),
                            NULL,
                            NULL,
                            TRUE);
        g_ptr_array_unref(items);
    }

    ui_async_context_free(ctx);
}

static void on_local_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GPtrArray *items = NULL;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    items = backend_list_local_finish(NULL, result, &error);

    if (app_state_is_stale_op(app, ctx->op)) {
        if (error != NULL) {
            g_error_free(error);
        }
        if (items != NULL) {
            g_ptr_array_unref(items);
        }
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        gchar *markup = ui_markup_from_text(error->message);

        app_state_show_info(app,
                            GTK_MESSAGE_ERROR,
                            markup,
                            _("_Retry"),
                            G_CALLBACK(on_info_action_refresh),
                            FALSE);
        g_free(markup);
        g_error_free(error);
    }

    if (items != NULL) {
        ui_render_items_to_section(app, "local", items);
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            ui_operation_status_text(UI_OPERATION_STATUS_READY),
                            NULL,
                            NULL,
                            TRUE);
        g_ptr_array_unref(items);
    }

    ui_async_context_free(ctx);
}

static void on_search_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GPtrArray *items = NULL;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    items = backend_search_finish(NULL, result, &error);

    if (app_state_is_stale_op(app, ctx->op)) {
        if (error != NULL) {
            g_error_free(error);
        }
        if (items != NULL) {
            g_ptr_array_unref(items);
        }
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            char *message = g_strdup_printf(_("Ошибка поиска: %s"),
                                            error->message != NULL ? error->message : _("unknown error"));
            ui_set_inet_search_state(app, message);
            g_free(message);
        }
        app->inet_refresh_pending_completion = FALSE;
        g_clear_error(&error);
    }

    if (items != NULL) {
        gboolean changed;

        if (error == NULL && ctx->cacheable_inet_query) {
            GPtrArray *cache_items = ui_module_info_array_dup(items);

            ui_inet_cache_clear(app);
            if (cache_items != NULL) {
                app->inet_cache = cache_items;
                app->inet_cache_db_mtime = ui_inet_cache_current_db_mtime(app);
                app->inet_cache_modman_bin = g_strdup(ctx->inet_modman_bin != NULL ? ctx->inet_modman_bin : "");
            }
        }

        changed = ui_section_items_changed(app, "inet", items);
        if (changed) {
            ui_render_items_to_section(app, "inet", items);
        }
        if (error == NULL) {
            ui_apply_search_highlight_to_list(app->list_boxes[SECTION_INET], app->pending_query);
            if (items->len == 0U) {
                ui_set_inet_search_state(app, _("Ничего не найдено"));
            } else {
                app_state_set_operation_status(app, UI_OPERATION_STATUS_READY, NULL);
                ui_set_inet_placeholder(app, _("Ничего не найдено"));
            }
            app->inet_refresh_pending_completion = FALSE;
            app->inet_auto_sync_attempted = FALSE;
        }
        g_ptr_array_unref(items);
    }

    ui_async_context_free(ctx);
}

static gboolean query_is_empty_after_trim(const char *query)
{
    char *copy;
    gboolean is_empty;

    if (query == NULL) {
        return TRUE;
    }

    copy = g_strdup(query);
    if (copy == NULL) {
        return TRUE;
    }

    g_strstrip(copy);
    is_empty = (copy[0] == '\0');
    g_free(copy);
    return is_empty;
}

static gboolean ui_text_matches_query(const char *text, const char *query)
{
    char *trimmed_query;
    char *casefold_text;
    char *casefold_query;
    gboolean matches;

    trimmed_query = ui_trimmed_query_dup(query);
    if (trimmed_query == NULL) {
        return TRUE;
    }

    if (text == NULL || text[0] == '\0') {
        g_free(trimmed_query);
        return FALSE;
    }

    casefold_text = g_utf8_validate(text, -1, NULL)
        ? g_utf8_casefold(text, -1)
        : g_ascii_strdown(text, -1);
    casefold_query = g_utf8_validate(trimmed_query, -1, NULL)
        ? g_utf8_casefold(trimmed_query, -1)
        : g_ascii_strdown(trimmed_query, -1);
    matches = casefold_text != NULL &&
              casefold_query != NULL &&
              strstr(casefold_text, casefold_query) != NULL;

    g_free(casefold_query);
    g_free(casefold_text);
    g_free(trimmed_query);
    return matches;
}

static gboolean ui_module_matches_query(const ModuleInfo *module, const char *query)
{
    if (module == NULL || query_is_empty_after_trim(query)) {
        return TRUE;
    }

    return ui_text_matches_query(module->name, query) ||
           ui_text_matches_query(module->desc, query) ||
           ui_text_matches_query(module->category, query);
}

static gboolean ui_filter_row_for_current_section(GtkListBoxRow *row, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *current_section_id;
    const char *row_section_id;
    ModuleInfo *module;

    if (row == NULL || app == NULL || query_is_empty_after_trim(app->pending_query)) {
        return TRUE;
    }

    current_section_id = ui_current_section_id(app);
    row_section_id = g_object_get_data(G_OBJECT(row), "section-id");
    if (current_section_id == NULL || row_section_id == NULL ||
        g_strcmp0(current_section_id, row_section_id) != 0) {
        return TRUE;
    }

    module = g_object_get_data(G_OBJECT(row), "module-info");
    return ui_module_matches_query(module, app->pending_query);
}

static gchar **ui_selected_module_values_dup(AppState *app, guint *out_count, gboolean use_paths)
{
    const char *section_id;
    gint index;
    GList *selected_rows;
    GList *iter;
    GPtrArray *names;

    if (out_count != NULL) {
        *out_count = 0U;
    }

    if (app == NULL) {
        return NULL;
    }

    section_id = ui_current_section_id(app);
    index = section_index_from_id(section_id);
    if (index < 0 || index >= SECTION_COUNT || app->list_boxes[index] == NULL) {
        return NULL;
    }

    names = g_ptr_array_new_with_free_func(g_free);
    selected_rows = gtk_list_box_get_selected_rows(app->list_boxes[index]);
    for (iter = selected_rows; iter != NULL; iter = iter->next) {
        ModuleInfo *module = g_object_get_data(G_OBJECT(iter->data), "module-info");
        const char *value = NULL;

        if (module != NULL) {
            value = use_paths && module->path != NULL && module->path[0] != '\0'
                ? module->path
                : module->name;
        }

        if (value != NULL && value[0] != '\0') {
            g_ptr_array_add(names, g_strdup(value));
        }
    }
    g_list_free(selected_rows);

    if (out_count != NULL) {
        *out_count = names->len;
    }

    g_ptr_array_add(names, NULL);
    return (gchar **)g_ptr_array_free(names, FALSE);
}

static void ui_invalidate_section_filter(AppState *app, const char *section_id)
{
    gint index;
    GtkListBox *list_box;

    if (app == NULL) {
        return;
    }

    index = section_index_from_id(section_id != NULL ? section_id : ui_current_section_id(app));
    if (index < 0 || index >= SECTION_COUNT) {
        return;
    }

    list_box = app->list_boxes[index];
    if (list_box == NULL) {
        return;
    }

    gtk_list_box_invalidate_filter(list_box);
    ui_apply_search_highlight_to_list(list_box, app->pending_query);
}

static gboolean on_search_debounce_fire(gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    UiAsyncContext *ctx;

    if (app == NULL) {
        return G_SOURCE_REMOVE;
    }

    app->search_debounce_id = 0;

    if (g_strcmp0(ui_current_section_id(app), "inet") != 0) {
        return G_SOURCE_REMOVE;
    }

    if (query_is_empty_after_trim(app->pending_query)) {
        return G_SOURCE_REMOVE;
    }

    app_state_start_op(app);
    ctx = ui_async_context_new(app);
    if (ctx != NULL) {
        backend_search_async(NULL, app->pending_query, app->current_op, on_search_done, ctx);
    }

    return G_SOURCE_REMOVE;
}

static void on_module_action_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GError *error = NULL;
    gboolean ok;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    if (ctx->is_unload) {
        ok = backend_unload_finish(NULL, result, &error);
    } else {
        ok = backend_load_finish(NULL, result, &error);
    }

    if (app_state_is_stale_op(app, ctx->op)) {
        if (error != NULL) {
            g_error_free(error);
        }
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        if (error->domain == MODMAN_GUI_ERROR_DOMAIN &&
            error->code == MODMAN_GUI_ERR_CANCELLED) {
            app_state_set_operation_status(app, UI_OPERATION_STATUS_CANCELLED, NULL);
        } else {
            if (ctx->is_unload &&
                !ctx->retry_after_confirm &&
                error->domain == MODMAN_GUI_ERROR_DOMAIN &&
                error->code == MODMAN_GUI_ERR_DEP_CONFLICT) {
                char *body = g_strdup_printf(_("Another attached module depends on “%s”.\\n\\nDetaching it may break dependent components. Continue?"),
                                             ctx->module_name != NULL ? ctx->module_name : _("unnamed"));

                if (app_state_confirm_dangerous(app,
                                                _("⚠  Detach dependent module?"),
                                                body,
                                                _("_Detach"))) {
                    ModuleInfo module = {0};
                    UiAsyncContext *retry_ctx;

                    app_state_start_op(app);
                    retry_ctx = ui_async_context_new(app);
                    if (retry_ctx != NULL) {
                        retry_ctx->module_name = g_strdup(ctx->module_name);
                        retry_ctx->module_path = g_strdup(ctx->module_path);
                        retry_ctx->is_unload = TRUE;
                        retry_ctx->retry_after_confirm = TRUE;
                        module.name = retry_ctx->module_name;
                        backend_unload_async(NULL,
                                             &module,
                                             app->current_op,
                                             on_module_action_done,
                                             retry_ctx);
                    }
                }

                g_free(body);
            } else {
                char *clean_error = ui_strip_ansi_sequences(error->message);

                app_state_set_operation_status(app, UI_OPERATION_STATUS_ERROR, clean_error);
                g_free(clean_error);
            }
        }

        g_error_free(error);
        ui_async_context_free(ctx);
        app_state_progress_disarm(app);
        return;
    }

    if (!ok) {
        app_state_set_operation_status(app,
                                       UI_OPERATION_STATUS_ERROR,
                                       ctx->is_unload
                                           ? _("The backend completed detach without a result.")
                                           : _("The operation completed without a result."));
        ui_async_context_free(ctx);
        app_state_progress_disarm(app);
        return;
    }

    app_state_set_operation_status(app, UI_OPERATION_STATUS_READY, NULL);

    ui_async_context_free(ctx);

    app_state_progress_disarm(app);
    app->suppress_next_refresh_info = TRUE;
    app_state_load_current_section(app);
}

static void on_download_action_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GError *error = NULL;
    gboolean ok;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    ok = backend_download_finish(NULL, result, &error);

    if (app_state_is_stale_op(app, ctx->op)) {
        g_clear_error(&error);
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        if (error->domain == MODMAN_GUI_ERROR_DOMAIN &&
            error->code == MODMAN_GUI_ERR_CANCELLED) {
            app_state_set_operation_status(app, UI_OPERATION_STATUS_CANCELLED, NULL);
        } else {
            app_state_set_operation_status(app, UI_OPERATION_STATUS_ERROR, error->message);
        }
        g_error_free(error);
        ui_async_context_free(ctx);
        app_state_progress_disarm(app);
        return;
    }

    if (!ok) {
        app_state_show_info(app,
                            GTK_MESSAGE_ERROR,
                            _("The download completed without a result."),
                            NULL,
                            NULL,
                            FALSE);
        ui_async_context_free(ctx);
        app_state_progress_disarm(app);
        return;
    }

    app_state_set_operation_status(app, UI_OPERATION_STATUS_READY, NULL);

    if (ctx->open_after_download && ctx->module_name != NULL) {
        char *downloaded_path = ui_downloaded_module_path_dup(app, ctx->module_name);
        if (downloaded_path != NULL) {
            ui_open_file_with_modman_open(app, downloaded_path);
            g_free(downloaded_path);
        } else {
            app_state_set_operation_status(app,
                                           UI_OPERATION_STATUS_ERROR,
                                           _("Downloaded module path was not found in the local module list."));
        }
    }

    ui_async_context_free(ctx);
    app_state_progress_disarm(app);
    app->suppress_next_refresh_info = TRUE;
    app_state_load_current_section(app);
}

static void ui_batch_load_finish(AppState *app, UiAsyncContext *ctx)
{
    char *message;
    char *markup;

    if (app == NULL || ctx == NULL) {
        ui_async_context_free(ctx);
        return;
    }

    if (ctx->module_failures != NULL && ctx->module_failures->len > 0U) {
        message = g_strdup_printf(_("Loaded: %u of %u. Errors: %s"),
                                  ctx->module_success,
                                  ctx->module_total,
                                  ctx->module_failures->str);
    } else {
        message = g_strdup(ui_operation_status_text(UI_OPERATION_STATUS_READY));
    }

    markup = ui_markup_from_text(message);
    app_state_show_info(app, GTK_MESSAGE_INFO, markup, NULL, NULL, TRUE);
    g_free(markup);
    g_free(message);

    ui_async_context_free(ctx);
    app_state_progress_disarm(app);
    app->suppress_next_refresh_info = TRUE;
    app_state_load_current_section(app);
}

static void ui_batch_load_next(AppState *app, UiAsyncContext *ctx)
{
    ModuleInfo module = {0};
    const char *name;

    if (app == NULL || ctx == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    if (ctx->module_index >= ctx->module_total) {
        ui_batch_load_finish(app, ctx);
        return;
    }

    name = ctx->module_names[ctx->module_index];
    module.name = ctx->module_args != NULL && ctx->module_args[ctx->module_index] != NULL
        ? ctx->module_args[ctx->module_index]
        : (char *)name;
    app_state_set_operation_status(app, UI_OPERATION_STATUS_CONNECTING, NULL);

    backend_load_async(NULL, &module, app->current_op, on_batch_load_done, ctx);
}

static void on_batch_load_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app = ctx != NULL ? ctx->app : NULL;
    GError *error = NULL;
    gboolean ok;
    const char *name;

    (void)source_object;

    if (app == NULL || ctx == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    ok = backend_load_finish(NULL, result, &error);
    name = ctx->module_names != NULL ? ctx->module_names[ctx->module_index] : NULL;

    if (ok && error == NULL) {
        ctx->module_success++;
    } else if (error != NULL && !(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
                                  error->code == MODMAN_GUI_ERR_CANCELLED)) {
        if (ctx->module_failures == NULL) {
            ctx->module_failures = g_string_new(NULL);
        }
        if (ctx->module_failures->len > 0U) {
            g_string_append(ctx->module_failures, "; ");
        }
        g_string_append_printf(ctx->module_failures,
                               "%s: %s",
                               name != NULL ? name : _("unnamed"),
                               error->message != NULL ? error->message : _("error"));
    }

    if (error != NULL) {
        g_error_free(error);
    }

    ctx->module_index++;
    ui_batch_load_next(app, ctx);
}

static void on_remove_local_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    GPtrArray *results = NULL;
    GError *error = NULL;
    gboolean ok;
    GString *removed;
    GString *refused;
    GString *failed;
    GString *status_msg;
    char *message;
    gchar *markup;
    GtkMessageType message_type;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    if (app == NULL || app_state_is_stale_op(app, ctx->op)) {
        ui_async_context_free(ctx);
        return;
    }

    ok = backend_remove_local_finish(NULL, result, &results, &error);

    if (app_state_is_stale_op(app, ctx->op)) {
        if (error != NULL) {
            g_error_free(error);
        }
        backend_remove_local_results_free(results);
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            markup = ui_markup_from_text(error->message);
            app_state_show_info(app,
                                GTK_MESSAGE_ERROR,
                                markup,
                                _("_Retry"),
                                G_CALLBACK(on_info_action_refresh),
                                FALSE);
            g_free(markup);
        }

        g_error_free(error);
        backend_remove_local_results_free(results);
        ui_async_context_free(ctx);
        app_state_progress_disarm(app);
        return;
    }

    removed = g_string_new(NULL);
    refused = g_string_new(NULL);
    failed = g_string_new(NULL);

    if (results != NULL) {
        guint i;

        for (i = 0U; i < results->len; i++) {
            BackendRemoveLocalResult *item = g_ptr_array_index(results, i);

            if (item == NULL) {
                continue;
            }

            switch (item->status) {
            case BACKEND_REMOVE_LOCAL_REMOVED:
                if (removed->len > 0U) {
                    g_string_append(removed, ", ");
                }
                g_string_append(removed, item->name != NULL ? item->name : "?");
                break;
            case BACKEND_REMOVE_LOCAL_REFUSED_MOUNTED:
                if (refused->len > 0U) {
                    g_string_append(refused, ", ");
                }
                g_string_append(refused, item->name != NULL ? item->name : "?");
                break;
            default:
                if (failed->len > 0U) {
                    g_string_append(failed, ", ");
                }
                g_string_append(failed, item->name != NULL ? item->name : "?");
                break;
            }
        }
    }

    backend_remove_local_results_free(results);

    status_msg = g_string_new(NULL);
    if (removed->len > 0U) {
        g_string_append_printf(status_msg, _("Removed: %s"), removed->str);
    }
    if (refused->len > 0U) {
        if (status_msg->len > 0U) {
            g_string_append(status_msg, ". ");
        }
        g_string_append_printf(status_msg, _("Refused (mounted): %s"), refused->str);
    }
    if (failed->len > 0U) {
        if (status_msg->len > 0U) {
            g_string_append(status_msg, ". ");
        }
        g_string_append_printf(status_msg, _("Error: %s"), failed->str);
    }
    if (status_msg->len == 0U) {
        g_string_append(status_msg,
                        ok ? _("Removal completed.")
                           : _("Removal made no changes."));
    }

    message = g_string_free(status_msg, FALSE);
    markup = ui_markup_from_text(message);
    message_type = (refused->len > 0U || failed->len > 0U || !ok)
        ? GTK_MESSAGE_WARNING
        : GTK_MESSAGE_INFO;
    app_state_show_info(app, message_type, markup, NULL, NULL, TRUE);
    g_free(markup);
    g_free(message);

    g_string_free(removed, TRUE);
    g_string_free(refused, TRUE);
    g_string_free(failed, TRUE);

    ui_async_context_free(ctx);
    app_state_progress_disarm(app);

    app->last_clicked_local_row = NULL;
    app->suppress_next_refresh_info = TRUE;
    app_state_load_current_section(app);
}

static const SectionSpec SECTION_SPECS[SECTION_COUNT] = {
    { "loaded", N_("Connected"), N_("Connected modules"), N_("Modules attached after system startup will appear here."), "emblem-default", "emblem-default-symbolic" },
    { "inet", N_("Online"), N_("Module catalog"), N_("Search results and available updates will appear here."), "network-workgroup", "network-workgroup-symbolic" },
    { "local", N_("Local"), N_("Local storage"), N_("Local modules will be shown here after the catalog is reloaded."), "folder", "folder-symbolic" },
    { "system", N_("System"), N_("System modules"), N_("Base modules attached by initrd will appear here."), "computer", "computer-symbolic" }
};

static gint section_index_from_id(const char *section_id)
{
    guint i;

    if (section_id == NULL) {
        return -1;
    }

    for (i = 0; i < SECTION_COUNT; i++) {
        if (g_strcmp0(SECTION_SPECS[i].id, section_id) == 0) {
            return (gint)i;
        }
    }

    return -1;
}

static const SectionSpec *section_spec_from_id(const char *section_id)
{
    gint index = section_index_from_id(section_id);

    if (index < 0) {
        return NULL;
    }

    return &SECTION_SPECS[index];
}

static const char *ui_section_tab_tooltip(const SectionSpec *section)
{
    if (section == NULL || section->id == NULL) {
        return NULL;
    }

    if (g_strcmp0(section->id, "loaded") == 0) {
        return _("Attached after system startup.");
    }

    if (g_strcmp0(section->id, "inet") == 0) {
        return _("Online repository.");
    }

    if (g_strcmp0(section->id, "local") == 0) {
        return _("Local modules.");
    }

    if (g_strcmp0(section->id, "system") == 0) {
        return _("Attached before system startup.");
    }

    return _(section->empty_text);
}

static GtkWidget *ui_build_section_tab_button(AppState *app,
                                              const SectionSpec *section,
                                              guint index,
                                              GtkSizeGroup *size_group)
{
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
    const char *tooltip;
    char *accessible_name;

    if (app == NULL || section == NULL) {
        return gtk_toggle_button_new();
    }

    button = gtk_toggle_button_new();
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    image = ui_icon_image_or_fallback(section->icon_name, section->icon_fallback);
    label = gtk_label_new(_(section->title));
    tooltip = ui_section_tab_tooltip(section);

    gtk_widget_set_name(button, section->id);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_margin_top(button, 0);
    gtk_widget_set_margin_bottom(button, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(box, 0);
    gtk_widget_set_margin_bottom(box, 0);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    if (size_group != NULL) {
        gtk_size_group_add_widget(size_group, button);
    }

    if (tooltip != NULL) {
        gtk_widget_set_tooltip_text(button, tooltip);
    }

    accessible_name = g_strdup_printf(_("Tab %s"), _(section->title));
    ui_set_widget_accessible(button, accessible_name, tooltip);
    g_free(accessible_name);

    g_object_set_data(G_OBJECT(button), "section-id", (gpointer)section->id);
    g_signal_connect(button, "toggled", G_CALLBACK(on_section_tab_toggled), app);

    app->tab_buttons[index] = button;
    return button;
}

static void ui_sync_tab_buttons(AppState *app)
{
    const char *section_id;
    guint i;

    if (app == NULL) {
        return;
    }

    section_id = ui_current_section_id(app);
    for (i = 0; i < SECTION_COUNT; i++) {
        GtkWidget *button = app->tab_buttons[i];

        if (button == NULL || !GTK_IS_TOGGLE_BUTTON(button)) {
            continue;
        }

        g_signal_handlers_block_by_func(button, G_CALLBACK(on_section_tab_toggled), app);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                                     g_strcmp0(section_id, SECTION_SPECS[i].id) == 0);
        g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_section_tab_toggled), app);
    }
}

static void on_section_tab_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *section_id;

    if (app == NULL || app->stack == NULL || button == NULL) {
        return;
    }

    if (!gtk_toggle_button_get_active(button)) {
        section_id = g_object_get_data(G_OBJECT(button), "section-id");
        if (section_id != NULL && g_strcmp0(section_id, ui_current_section_id(app)) == 0) {
            g_signal_handlers_block_by_func(button, G_CALLBACK(on_section_tab_toggled), app);
            gtk_toggle_button_set_active(button, TRUE);
            g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_section_tab_toggled), app);
        }
        return;
    }

    section_id = g_object_get_data(G_OBJECT(button), "section-id");
    if (section_id == NULL || g_strcmp0(section_id, ui_current_section_id(app)) == 0) {
        ui_sync_tab_buttons(app);
        return;
    }

    gtk_stack_set_visible_child_name(app->stack, section_id);
}

static const char *ui_current_section_id(AppState *app)
{
    GtkWidget *visible_child;

    if (app == NULL || app->stack == NULL) {
        return NULL;
    }

    visible_child = gtk_stack_get_visible_child(app->stack);
    if (visible_child == NULL) {
        return NULL;
    }

    return g_object_get_data(G_OBJECT(visible_child), "section-id");
}

static gboolean module_is_loaded(const ModuleInfo *module)
{
    if (module == NULL || module->is_local) {
        return FALSE;
    }

    if (module->layer == NULL || module->layer[0] == '\0') {
        return FALSE;
    }

    return !g_ascii_isdigit((guchar)module->layer[0]);
}

static char *ui_dup_resolved_source_path(const char *source_path)
{
    char *resolved = NULL;

    if (source_path == NULL || source_path[0] == '\0') {
        return NULL;
    }

    resolved = realpath(source_path, NULL);
    if (resolved != NULL && resolved[0] != '\0') {
        return resolved;
    }

    return g_strdup(source_path);
}

static gboolean ui_selected_sources_match_loaded_items(GHashTable *selected_sources, GPtrArray *items)
{
    guint i;

    if (selected_sources == NULL || items == NULL) {
        return FALSE;
    }

    for (i = 0U; i < items->len; i++) {
        ModuleInfo *module = g_ptr_array_index(items, i);
        char *loaded_source;
        gboolean matched;

        if (!module_is_loaded(module) || module == NULL || module->path == NULL || module->path[0] == '\0') {
            continue;
        }

        loaded_source = ui_dup_resolved_source_path(module->path);
        if (loaded_source == NULL || loaded_source[0] == '\0') {
            g_free(loaded_source);
            continue;
        }

        matched = g_hash_table_contains(selected_sources, loaded_source);
        g_free(loaded_source);

        if (matched) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean ui_local_selection_has_mounted_modules(GList *selected_rows, gboolean *query_failed_out)
{
    static const char *loaded_scopes[] = { "after", "before" };
    GHashTable *selected_sources;
    gboolean has_match = FALSE;
    gboolean query_failed = FALSE;
    guint i;

    if (query_failed_out != NULL) {
        *query_failed_out = FALSE;
    }

    if (selected_rows == NULL) {
        return FALSE;
    }

    selected_sources = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (GList *iter = selected_rows; iter != NULL; iter = iter->next) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(iter->data);
        ModuleInfo *module = row != NULL ? g_object_get_data(G_OBJECT(row), "module-info") : NULL;
        char *resolved_source;

        if (module == NULL || module->path == NULL || module->path[0] == '\0') {
            continue;
        }

        resolved_source = ui_dup_resolved_source_path(module->path);
        if (resolved_source != NULL && resolved_source[0] != '\0') {
            g_hash_table_add(selected_sources, resolved_source);
        } else {
            g_free(resolved_source);
        }
    }

    if (g_hash_table_size(selected_sources) == 0U) {
        g_hash_table_unref(selected_sources);
        return FALSE;
    }

    for (i = 0U; i < G_N_ELEMENTS(loaded_scopes); i++) {
        GPtrArray *loaded_items = NULL;
        GError *error = NULL;

        loaded_items = backend_list_loaded_sync(loaded_scopes[i], &error);
        if (error != NULL) {
            g_error_free(error);
            query_failed = TRUE;
            continue;
        }

        has_match = ui_selected_sources_match_loaded_items(selected_sources, loaded_items);
        if (loaded_items != NULL) {
            g_ptr_array_unref(loaded_items);
        }

        if (has_match) {
            break;
        }
    }

    g_hash_table_unref(selected_sources);

    if (query_failed_out != NULL) {
        *query_failed_out = query_failed;
    }

    return has_match;
}

static const char *ui_local_delete_tooltip(UiLocalDeleteState state)
{
    switch (state) {
    case UI_LOCAL_DELETE_DISABLED_NOT_LOCAL:
        return _("File removal is available only on the Local tab.");
    case UI_LOCAL_DELETE_DISABLED_NO_SELECTION:
        return _("Select one or more local modules to remove.");
    case UI_LOCAL_DELETE_DISABLED_MOUNTED_SELECTED:
        return _("Mounted modules cannot be removed: detach selected files first.");
    case UI_LOCAL_DELETE_DISABLED_BACKEND_UNAVAILABLE:
        return _("File removal is unavailable: the modman backend is currently unavailable.");
    case UI_LOCAL_DELETE_READY:
    default:
        return _("Remove selected .pfs files from the download directory");
    }
}

static const char *ui_unload_button_tooltip(gboolean no_modman,
                                            gboolean no_layering,
                                            gboolean can_modify,
                                            gboolean is_system_section,
                                            gboolean has_selection)
{
    if (no_modman) {
        return _("modman was not found: attach/detach operations are unavailable.");
    }

    if (no_layering || !can_modify) {
        return _("Attach/detach operations are unavailable: critical environment capabilities are missing.");
    }

    if (is_system_section) {
        return _("Системный слой нельзя отключить из этого списка");
    }

    if (!has_selection) {
        return _("Select one or more connected modules to detach.");
    }

    return _("Detach selected modules.");
}

static UiLocalDeleteState ui_local_delete_state(AppState *app, GList *selected_rows)
{
    gboolean query_failed = FALSE;

    if (app == NULL || app->list_boxes[SECTION_LOCAL] == NULL) {
        return UI_LOCAL_DELETE_DISABLED_NOT_LOCAL;
    }

    if (g_strcmp0(ui_current_section_id(app), "local") != 0) {
        return UI_LOCAL_DELETE_DISABLED_NOT_LOCAL;
    }

    if (!app_state_can_run_backend(app)) {
        return UI_LOCAL_DELETE_DISABLED_BACKEND_UNAVAILABLE;
    }

    if (selected_rows == NULL) {
        return UI_LOCAL_DELETE_DISABLED_NO_SELECTION;
    }

    if (ui_local_selection_has_mounted_modules(selected_rows, &query_failed)) {
        return UI_LOCAL_DELETE_DISABLED_MOUNTED_SELECTED;
    }

    if (query_failed) {
        return UI_LOCAL_DELETE_DISABLED_BACKEND_UNAVAILABLE;
    }

    return UI_LOCAL_DELETE_READY;
}

static UiLocalDeleteState ui_local_delete_button_state(AppState *app, GList *selected_rows)
{
    if (app == NULL || app->list_boxes[SECTION_LOCAL] == NULL) {
        return UI_LOCAL_DELETE_DISABLED_NOT_LOCAL;
    }

    if (g_strcmp0(ui_current_section_id(app), "local") != 0) {
        return UI_LOCAL_DELETE_DISABLED_NOT_LOCAL;
    }

    if (!app_state_can_run_backend(app)) {
        return UI_LOCAL_DELETE_DISABLED_BACKEND_UNAVAILABLE;
    }

    if (selected_rows == NULL) {
        return UI_LOCAL_DELETE_DISABLED_NO_SELECTION;
    }

    return UI_LOCAL_DELETE_READY;
}

static void ui_apply_local_delete_button_state(AppState *app, GList *selected_rows)
{
    UiLocalDeleteState state;

    if (app == NULL || app->btn_delete == NULL) {
        return;
    }

    state = ui_local_delete_button_state(app, selected_rows);
    gtk_widget_set_sensitive(GTK_WIDGET(app->btn_delete), state == UI_LOCAL_DELETE_READY);
    gtk_widget_set_tooltip_text(GTK_WIDGET(app->btn_delete), ui_local_delete_tooltip(state));
}

static const char *ui_status_icon_name(const ModuleInfo *module)
{
    if (module == NULL) {
        return "package-x-generic";
    }

    if (module->has_error) {
        return "dialog-error";
    }

    if (module->has_update) {
        return "software-update-available";
    }

    if (module->is_autoload) {
        return "emblem-favorite";
    }

    if (module_is_loaded(module)) {
        return "emblem-default";
    }

    if (module->is_local) {
        return "folder-download";
    }

    return "package-x-generic";
}

static const char *ui_status_icon_fallback_name(const ModuleInfo *module)
{
    if (module == NULL) {
        return "package-x-generic-symbolic";
    }

    if (module->has_error) {
        return "dialog-error-symbolic";
    }

    if (module->has_update) {
        return "software-update-available-symbolic";
    }

    if (module->is_autoload) {
        return "emblem-favorite-symbolic";
    }

    if (module_is_loaded(module)) {
        return "emblem-default-symbolic";
    }

    if (module->is_local) {
        return "folder-download-symbolic";
    }

    return "package-x-generic-symbolic";
}

static const char *ui_icon_name_or_fallback(const char *primary, const char *fallback)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();

    if ((primary == NULL || primary[0] == '\0') && (fallback == NULL || fallback[0] == '\0')) {
        return NULL;
    }

    if (primary != NULL && primary[0] != '\0' && theme != NULL && gtk_icon_theme_has_icon(theme, primary)) {
        return primary;
    }

    if (fallback != NULL && fallback[0] != '\0' && theme != NULL && gtk_icon_theme_has_icon(theme, fallback)) {
        return fallback;
    }

    if (primary != NULL && primary[0] != '\0') {
        return primary;
    }

    return fallback;
}

static GtkWidget *ui_icon_image_or_fallback(const char *primary, const char *fallback)
{
    const char *icon_name = ui_icon_name_or_fallback(primary, fallback);

    if (icon_name == NULL || icon_name[0] == '\0') {
        return gtk_image_new();
    }

    return gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
}

static void ui_image_set_icon_name_or_fallback(GtkImage *image,
                                               const char *primary,
                                               const char *fallback,
                                               GtkIconSize size)
{
    const char *icon_name;

    if (image == NULL) {
        return;
    }

    icon_name = ui_icon_name_or_fallback(primary, fallback);
    if (icon_name == NULL || icon_name[0] == '\0') {
        gtk_image_clear(image);
        return;
    }

    gtk_image_set_from_icon_name(image, icon_name, size);
}

static char *ui_loaded_mode_status_text_dup(const ModuleInfo *module)
{
    if (module == NULL || module->mount_in_ram == NULL) {
        return NULL;
    }
    if (g_strcmp0(module->mount_in_ram, "yes") == 0) {
        return g_strdup(_("Loaded (RAM)"));
    }
    return NULL;
}

static const char *ui_status_text(const ModuleInfo *module)
{
    static char ui_status_text_buf[256];
    char *mode_text;

    if (module == NULL) {
        return _("regular module");
    }

    if (module->has_error) {
        return _("last operation failed");
    }

    if (module->has_update) {
        return _("update available");
    }

    if (module->is_autoload) {
        return _("autoload enabled");
    }

    if (module_is_loaded(module)) {
        mode_text = ui_loaded_mode_status_text_dup(module);
        if (mode_text != NULL) {
            g_strlcpy(ui_status_text_buf, mode_text, sizeof(ui_status_text_buf));
            g_free(mode_text);
            return ui_status_text_buf;
        }
        return _("module loaded");
    }

    if (module->is_local) {
        return _("local only");
    }

    return _("regular module");
}

static const char *ui_details_status_text(AppState *app, const ModuleInfo *module)
{
    const char *section_id;

    if (module == NULL) {
        return NULL;
    }

    if (module->has_error || module->has_update) {
        return ui_status_text(module);
    }

    section_id = ui_current_section_id(app);
    if (g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) {
        return ui_status_text(module);
    }
    if (g_strcmp0(section_id, "inet") == 0) {
        return _("module from online catalog");
    }
    if (g_strcmp0(section_id, "local") == 0) {
        return _("local module");
    }

    return ui_status_text(module);
}

static void ui_status_widget_apply(GtkWidget *image, const ModuleInfo *module)
{
    char *accessible_name;
    char *accessible_desc;

    if (image == NULL) {
        return;
    }

    ui_image_set_icon_name_or_fallback(GTK_IMAGE(image),
                                       ui_status_icon_name(module),
                                       ui_status_icon_fallback_name(module),
                                       GTK_ICON_SIZE_MENU);
    accessible_name = g_strdup_printf(_("Status: %s"), ui_status_text(module));
    accessible_desc = g_strdup_printf(_("Selected module status indicator: %s"), ui_status_text(module));
    ui_set_widget_accessible(image, accessible_name, accessible_desc);
    g_free(accessible_desc);
    g_free(accessible_name);
}

static const char *ui_loaded_mode_icon_name(const ModuleInfo *module)
{
    if (module == NULL || module->mount_in_ram == NULL) {
        return NULL;
    }
    if (g_strcmp0(module->mount_in_ram, "yes") == 0) {
        return "drive-removable-media";
    }
    return NULL;
}

static void ui_section_status_widget_apply(AppState *app,
                                           GtkWidget *image,
                                           const ModuleInfo *module,
                                           const char *section_id)
{
    const SectionSpec *section;
    const char *status_text;
    const char *icon_name;
    const char *icon_fallback;
    char *accessible_name;
    char *accessible_desc;

    if (image == NULL) {
        return;
    }

    if (module == NULL || module->has_error || module->has_update) {
        ui_status_widget_apply(image, module);
        return;
    }

    section = section_spec_from_id(section_id != NULL ? section_id : ui_current_section_id(app));
    if (section == NULL) {
        ui_status_widget_apply(image, module);
        return;
    }

    icon_name = section->icon_name;
    icon_fallback = section->icon_fallback;

    if (g_strcmp0(section_id != NULL ? section_id : ui_current_section_id(app), "loaded") == 0 ||
        g_strcmp0(section_id != NULL ? section_id : ui_current_section_id(app), "system") == 0) {
        const char *mode_icon = ui_loaded_mode_icon_name(module);
        if (mode_icon != NULL) {
            icon_name = mode_icon;
            icon_fallback = mode_icon;
        }
    }

    status_text = ui_details_status_text(app, module);
    ui_image_set_icon_name_or_fallback(GTK_IMAGE(image),
                                       icon_name,
                                       icon_fallback,
                                       GTK_ICON_SIZE_MENU);
    accessible_name = g_strdup_printf(_("Status: %s"), status_text != NULL ? status_text : _(section->title));
    accessible_desc = g_strdup_printf(_("Module section indicator: %s"), _(section->title));
    ui_set_widget_accessible(image, accessible_name, accessible_desc);
    g_free(accessible_desc);
    g_free(accessible_name);
}

static char *ui_format_size_or_layer_for_section(const ModuleInfo *module, const char *section_id)
{
    gboolean prefer_layer = (g_strcmp0(section_id, "loaded") == 0 ||
                             g_strcmp0(section_id, "system") == 0);

    if (prefer_layer && module != NULL &&
        module->layer != NULL && module->layer[0] != '\0') {
        return g_strdup(module->layer);
    }

    if (module != NULL && module->size_mb > 0.0) {
        guint64 bytes = (guint64)((module->size_mb * 1024.0 * 1024.0) + 0.5);
        return g_format_size(bytes);
    }

    if (module != NULL && module->layer != NULL && module->layer[0] != '\0') {
        return g_strdup(module->layer);
    }

    return g_strdup("—");
}

static G_GNUC_UNUSED char *ui_format_size_or_layer(const ModuleInfo *module)
{
    return ui_format_size_or_layer_for_section(module, NULL);
}

static char *ui_build_name_markup(const ModuleInfo *module, const char *query)
{
    char *highlighted;
    char *markup;

    highlighted = ui_build_highlight_markup(module != NULL && module->name != NULL ? module->name : _("Unnamed module"),
                                            query);
    markup = g_strdup_printf("<span weight='bold'>%s</span>", highlighted);
    g_free(highlighted);
    return markup;
}

static char *ui_build_meta_text_for_section(const ModuleInfo *module, const char *section_id)
{
    char *size_text;
    const char *primary_text;
    char *plain_text;

    size_text = ui_format_size_or_layer_for_section(module, section_id);

    if (g_strcmp0(section_id, "inet") == 0) {
        primary_text = (module != NULL && module->desc != NULL && module->desc[0] != '\0')
                           ? module->desc
                           : _("no description");
    } else {
        primary_text = (module != NULL && module->path != NULL && module->path[0] != '\0')
                           ? module->path
                           : "—";
    }

    if ((g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) &&
        module != NULL && module->layer != NULL && module->layer[0] != '\0') {
        plain_text = g_strdup(primary_text);
        g_free(size_text);
        return plain_text;
    }

    if (g_strcmp0(section_id, "inet") == 0 || g_strcmp0(section_id, "local") == 0) {
        plain_text = g_strdup_printf("%s · %s", size_text, primary_text);
    } else {
        plain_text = g_strdup_printf("%s · %s", primary_text, size_text);
    }

    g_free(size_text);
    return plain_text;
}

static G_GNUC_UNUSED char *ui_build_meta_text(const ModuleInfo *module)
{
    return ui_build_meta_text_for_section(module, NULL);
}

static char *ui_build_meta_markup_for_section(const ModuleInfo *module,
                                              const char *section_id,
                                              const char *query)
{
    char *plain_text;
    char *highlighted;
    char *markup;

    plain_text = ui_build_meta_text_for_section(module, section_id);
    highlighted = ui_build_highlight_markup(plain_text, query);
    markup = g_strdup_printf("<span alpha='60%%' size='smaller'>%s</span>", highlighted);

    g_free(plain_text);
    g_free(highlighted);
    return markup;
}

static void ui_update_row_markup(GtkListBoxRow *row, const char *query)
{
    ModuleInfo *module;
    GtkLabel *name_label;
    GtkLabel *meta_label;
    const char *section_id;
    char *name_markup;
    char *meta_markup;
    gboolean is_compact;

    if (row == NULL) {
        return;
    }

    module = g_object_get_data(G_OBJECT(row), "module-info");
    name_label = g_object_get_data(G_OBJECT(row), "row-name-label");
    meta_label = g_object_get_data(G_OBJECT(row), "row-meta-label");
    section_id = g_object_get_data(G_OBJECT(row), "section-id");
    is_compact = g_object_get_data(G_OBJECT(row), "row-is-compact") != NULL;

    if (name_label == NULL && meta_label == NULL) {
        return;
    }

    if (is_compact) {
        /* Compact row: name is plain text — do NOT apply bold markup.
         * Update meta_label with section-specific text WITHOUT size prefix
         * (size is already shown in the dedicated size_label on the right). */
        if (meta_label != NULL) {
            const char *plain_meta = NULL;
            char *escaped;
            char *meta_compact_markup;

            if (g_strcmp0(section_id, "inet") == 0) {
                plain_meta = (module != NULL && module->desc != NULL && module->desc[0] != '\0')
                             ? module->desc : "";
            } else if (g_strcmp0(section_id, "local") == 0) {
                plain_meta = (module != NULL && module->path != NULL && module->path[0] != '\0')
                             ? module->path : "";
            } else {
                plain_meta = "";
            }
            escaped = g_markup_escape_text(plain_meta, -1);
            meta_compact_markup = g_strdup_printf("<span alpha='60%%' size='small'>%s</span>", escaped);
            gtk_label_set_markup(meta_label, meta_compact_markup);
            g_free(escaped);
            g_free(meta_compact_markup);
        }
        return;
    }

    if (name_label == NULL || meta_label == NULL) {
        return;
    }

    name_markup = ui_build_name_markup(module, query);
    meta_markup = ui_build_meta_markup_for_section(module, section_id, query);
    gtk_label_set_markup(name_label, name_markup);
    gtk_label_set_markup(meta_label, meta_markup);
    g_free(meta_markup);
    g_free(name_markup);
}

static void ui_apply_search_highlight_to_list(GtkListBox *list_box, const char *query)
{
    GList *children;
    GList *iter;
    guint visible_rows = 0U;
    guint visible_index = 0U;
    gboolean has_query;

    if (list_box == NULL) {
        return;
    }

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (GTK_IS_LIST_BOX_ROW(iter->data) && gtk_widget_get_visible(GTK_WIDGET(iter->data))) {
            visible_rows++;
        }
    }

    has_query = !query_is_empty_after_trim(query);

    for (iter = children; iter != NULL; iter = iter->next) {
        GtkListBoxRow *row;
        const char *row_query = query;

        if (!GTK_IS_LIST_BOX_ROW(iter->data) || !gtk_widget_get_visible(GTK_WIDGET(iter->data))) {
            continue;
        }

        row = GTK_LIST_BOX_ROW(iter->data);
        if (has_query && visible_rows > UI_HIGHLIGHT_ROW_LIMIT && visible_index >= UI_HIGHLIGHT_ROW_LIMIT) {
            row_query = NULL;
        }

        ui_update_row_markup(row, row_query);
        visible_index++;
    }

    g_list_free(children);
}

static GtkWidget *ui_build_status_widget(AppState *app, const ModuleInfo *module, const char *section_id)
{
    GtkWidget *image = gtk_image_new();

    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(image, GTK_ALIGN_START);
    ui_section_status_widget_apply(app, image, module, section_id);
    return image;
}

static gint compare_status_priority(GtkListBoxRow *row_a,
                                    GtkListBoxRow *row_b,
                                    gpointer user_data)
{
    const ModuleInfo *module_a;
    const ModuleInfo *module_b;
    AppState *app = (AppState *)user_data;
    UiSortMode sort_mode = app != NULL ? (UiSortMode)app->sort_mode : UI_SORT_NAME;

    module_a = g_object_get_data(G_OBJECT(row_a), "module-info");
    module_b = g_object_get_data(G_OBJECT(row_b), "module-info");

    if (sort_mode == UI_SORT_NAME) {
        return g_strcmp0(module_a != NULL ? module_a->name : "",
                         module_b != NULL ? module_b->name : "");
    }

    if (sort_mode == UI_SORT_SIZE) {
        double size_a = module_a != NULL ? module_a->size_mb : 0.0;
        double size_b = module_b != NULL ? module_b->size_mb : 0.0;

        if (size_a < size_b) {
            return 1;
        }
        if (size_a > size_b) {
            return -1;
        }
        return g_strcmp0(module_a != NULL ? module_a->name : "",
                         module_b != NULL ? module_b->name : "");
    }

    if (sort_mode == UI_SORT_LAYER) {
        gint layer_a = ui_layer_number_value(module_a != NULL ? module_a->layer : NULL);
        gint layer_b = ui_layer_number_value(module_b != NULL ? module_b->layer : NULL);
        gint layer_cmp = layer_a - layer_b;
        if (layer_cmp != 0) {
            return layer_cmp;
        }
        return g_strcmp0(module_a != NULL ? module_a->name : "",
                         module_b != NULL ? module_b->name : "");
    }

    return g_strcmp0(module_a != NULL ? module_a->name : "",
                     module_b != NULL ? module_b->name : "");
}

static const char *details_value_or_placeholder(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return DETAILS_PLACEHOLDER;
    }

    return value;
}

static char *ui_caption_to_accessible_prefix(const char *caption)
{
    char *prefix;
    gsize len;

    prefix = g_strdup(caption != NULL ? caption : _("Value"));
    if (prefix == NULL) {
        return NULL;
    }

    g_strstrip(prefix);
    len = strlen(prefix);
    while (len > 0U && (prefix[len - 1] == ':' || g_ascii_isspace((guchar)prefix[len - 1]))) {
        prefix[len - 1] = '\0';
        len--;
    }

    return prefix;
}

static void details_label_sync_accessibility(GtkLabel *label, const char *value)
{
    const char *prefix;
    const char *resolved_value;
    char *name;
    char *description;

    if (label == NULL) {
        return;
    }

    prefix = g_object_get_data(G_OBJECT(label), "details-accessible-prefix");
    resolved_value = details_value_or_placeholder(value);

    if (prefix != NULL && prefix[0] != '\0') {
        name = g_strdup_printf("%s: %s", prefix, resolved_value);
        description = g_strdup_printf(_("Field “%s” value: %s"), prefix, resolved_value);
    } else {
        name = g_strdup(resolved_value);
        description = g_strdup_printf(_("Text value: %s"), resolved_value);
    }

    ui_set_widget_accessible(GTK_WIDGET(label), name, description);
    g_free(description);
    g_free(name);
}

static void details_label_set_plain(GtkLabel *label, const char *value)
{
    if (label == NULL) {
        return;
    }

    gtk_label_set_text(label, details_value_or_placeholder(value));
    details_label_sync_accessibility(label, value);
}

static void details_label_set_name(GtkLabel *label, const char *value)
{
    char *escaped;
    char *markup;

    if (label == NULL) {
        return;
    }

    escaped = g_markup_escape_text(details_value_or_placeholder(value), -1);
    markup = g_strdup_printf("<b>%s</b>", escaped);
    gtk_label_set_markup(label, markup);
    details_label_sync_accessibility(label, value);
    g_free(markup);
    g_free(escaped);
}

static void details_label_apply_compact_wrap(GtkLabel *label)
{
    if (label == NULL) {
        return;
    }

    gtk_label_set_line_wrap(label, TRUE);
    gtk_label_set_line_wrap_mode(label, PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(label, UI_DETAILS_COMPACT_WRAP_CHARS);
    gtk_label_set_max_width_chars(label, UI_DETAILS_COMPACT_WRAP_CHARS);
    gtk_label_set_lines(label, UI_DETAILS_COMPACT_WRAP_LINES);
}

static GtkWidget *details_pane_append_value_row(GtkWidget *container,
                                                const char *icon_name,
                                                const char *caption,
                                                gboolean selectable,
                                                gboolean wrap,
                                                PangoEllipsizeMode ellipsize,
                                                GtkLabel **out_label,
                                                gint column,
                                                gint row,
                                                gint width)
{
    GtkWidget *field_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *caption_label = gtk_label_new(caption);
    GtkWidget *value_label = gtk_label_new(DETAILS_PLACEHOLDER);
    GtkWidget *attach_widget;
    char *prefix = ui_caption_to_accessible_prefix(caption);
    char *caption_desc = NULL;

    gtk_label_set_xalign(GTK_LABEL(caption_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(value_label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(value_label), selectable);
    gtk_label_set_line_wrap(GTK_LABEL(value_label), wrap);
    gtk_label_set_ellipsize(GTK_LABEL(value_label), ellipsize);

    if (wrap) {
        gtk_label_set_max_width_chars(GTK_LABEL(value_label), 40);
    }

    if (prefix != NULL) {
        caption_desc = g_strdup_printf(_("Field \u201c%s\u201d caption in the details pane."), prefix);
        ui_set_widget_accessible(caption_label, prefix, caption_desc);
        g_object_set_data_full(G_OBJECT(value_label), "details-accessible-prefix", prefix, g_free);
        details_label_sync_accessibility(GTK_LABEL(value_label), NULL);
    } else {
        details_label_sync_accessibility(GTK_LABEL(value_label), NULL);
    }

    g_free(caption_desc);

    gtk_box_pack_start(GTK_BOX(field_box), caption_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(field_box), value_label, FALSE, FALSE, 0);
    gtk_widget_set_halign(field_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(field_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(field_box, TRUE);

    if (icon_name != NULL) {
        GtkWidget *icon_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        gtk_widget_set_halign(icon, GTK_ALIGN_START);
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(icon_row), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(icon_row), field_box, TRUE, TRUE, 0);
        gtk_widget_set_halign(icon_row, GTK_ALIGN_FILL);
        gtk_widget_set_valign(icon_row, GTK_ALIGN_START);
        gtk_widget_set_hexpand(icon_row, TRUE);
        attach_widget = icon_row;
    } else {
        attach_widget = field_box;
    }

    if (GTK_IS_GRID(container)) {
        gtk_grid_attach(GTK_GRID(container), attach_widget, column, row, width, 1);
    } else {
        gtk_box_pack_start(GTK_BOX(container), attach_widget, FALSE, FALSE, 0);
    }

    if (out_label != NULL) {
        *out_label = GTK_LABEL(value_label);
    }

    return field_box;
}

static char *details_size_text(const ModuleInfo *module)
{
    guint64 bytes;

    if (module == NULL || module->size_mb <= 0.0) {
        return g_strdup(DETAILS_PLACEHOLDER);
    }

    bytes = (guint64)((module->size_mb * 1024.0 * 1024.0) + 0.5);
    return g_format_size(bytes);
}

static char *details_size_text_for_section(AppState *app, const ModuleInfo *module)
{
    (void)app;
    return details_size_text(module);
}

static char *details_size_text_with_format_detail(AppState *app,
                                                  const ModuleInfo *module,
                                                  const BackendOpenFileInfo *info)
{
    char *size_text;
    char *result;
    const char *format_detail;

    size_text = details_size_text_for_section(app, module);
    format_detail = info != NULL && info->format_detail != NULL && info->format_detail[0] != '\0'
        ? info->format_detail
        : NULL;

    if (format_detail == NULL) {
        return size_text;
    }

    if (size_text == NULL || g_strcmp0(size_text, DETAILS_PLACEHOLDER) == 0) {
        if (info != NULL && info->size != NULL && info->size[0] != '\0') {
            result = g_strdup_printf("%s · %s", info->size, format_detail);
        } else {
            result = g_strdup(format_detail);
        }
    } else {
        result = g_strdup_printf("%s · %s", size_text, format_detail);
    }

    g_free(size_text);
    return result;
}

static gint details_module_name_compare(gconstpointer a, gconstpointer b)
{
    const char *name_a = *(const char * const *)a;
    const char *name_b = *(const char * const *)b;

    return g_utf8_collate(name_a != NULL ? name_a : "",
                          name_b != NULL ? name_b : "");
}

static char *details_nested_modules_text_dup(const char *modules)
{
    GPtrArray *items;
    gchar **lines;
    char *result;

    if (modules == NULL || modules[0] == '\0') {
        return g_strdup(_("Модули внутри: —"));
    }

    items = g_ptr_array_new_with_free_func(g_free);
    lines = g_strsplit(modules, "\n", -1);
    for (guint i = 0U; lines != NULL && lines[i] != NULL; i++) {
        char *item = g_strdup(lines[i]);

        g_strstrip(item);
        if (item[0] == '\0') {
            g_free(item);
            continue;
        }
        g_ptr_array_add(items, item);
    }
    g_strfreev(lines);

    if (items->len == 0U) {
        g_ptr_array_unref(items);
        return g_strdup(_("Модули внутри: —"));
    }

    g_ptr_array_sort(items, details_module_name_compare);
    {
        GString *builder = g_string_new(_("Модули внутри:"));

        for (guint i = 0U; i < items->len; i++) {
            g_string_append_c(builder, '\n');
            g_string_append(builder, (const char *)g_ptr_array_index(items, i));
        }
        result = g_string_free(builder, FALSE);
    }

    g_ptr_array_unref(items);
    return result;
}

static void details_modules_label_set(AppState *app, const char *text, gboolean visible)
{
    if (app == NULL || app->details.modules == NULL) {
        return;
    }

    gtk_label_set_text(app->details.modules, text != NULL ? text : "");
    details_label_sync_accessibility(app->details.modules, text);
    if (visible) {
        gtk_widget_show(GTK_WIDGET(app->details.modules));
    } else {
        gtk_widget_hide(GTK_WIDGET(app->details.modules));
    }
}

static gboolean details_section_allows_file_info(const char *section_id)
{
    return g_strcmp0(section_id, "local") == 0 ||
           g_strcmp0(section_id, "system") == 0;
}

static gboolean details_module_has_pfs_path(const ModuleInfo *module)
{
    const char *path;

    if (module == NULL) {
        return FALSE;
    }

    path = module->path != NULL && module->path[0] != '\0'
        ? module->path
        : module->name;

    return path != NULL && g_path_is_absolute(path) && g_str_has_suffix(path, ".pfs");
}

static void details_file_info_clear_current(AppState *app, GCancellable *op)
{
    if (app == NULL || app->details_file_info_op == NULL) {
        return;
    }

    if (op == NULL || app->details_file_info_op == op) {
        g_clear_object(&app->details_file_info_op);
    }
}

static void details_file_info_start(AppState *app, const ModuleInfo *module)
{
    const char *section_id;
    const char *path;
    UiAsyncContext *ctx;

    if (app == NULL) {
        return;
    }

    app_state_cancel_cancellable(&app->details_file_info_op);
    app->details_file_info_seq++;

    section_id = ui_current_section_id(app);
    if (!details_section_allows_file_info(section_id) ||
        !details_module_has_pfs_path(module)) {
        details_modules_label_set(app, NULL, FALSE);
        return;
    }

    if (!app_state_can_run_backend(app)) {
        details_modules_label_set(app, _("Не удалось прочитать состав модуля"), TRUE);
        return;
    }

    path = module->path != NULL && module->path[0] != '\0'
        ? module->path
        : module->name;
    app->details_file_info_op = g_cancellable_new();
    ctx = ui_async_context_new_for_op(app, app->details_file_info_op);
    if (ctx == NULL) {
        app_state_cancel_cancellable(&app->details_file_info_op);
        details_modules_label_set(app, _("Не удалось прочитать состав модуля"), TRUE);
        return;
    }

    ctx->module_index = app->details_file_info_seq;
    ctx->module_path = g_strdup(path);
    details_modules_label_set(app, _("Модули внутри: —"), TRUE);
    backend_open_file_info_async(path,
                                 app->details_file_info_op,
                                 on_details_file_info_done,
                                 ctx);
}

static void on_details_file_info_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    BackendOpenFileInfo *info = NULL;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    info = backend_open_file_info_finish(result, &error);

    if (app == NULL || ctx->module_index != app->details_file_info_seq ||
        app->details_file_info_op != ctx->op ||
        g_cancellable_is_cancelled(ctx->op)) {
        backend_open_file_info_free(info);
        g_clear_error(&error);
        ui_async_context_free(ctx);
        return;
    }

    if (error != NULL || info == NULL) {
        details_modules_label_set(app, _("Не удалось прочитать состав модуля"), TRUE);
        if (app->info_bar != NULL && gtk_widget_get_visible(app->info_bar)) {
            gtk_widget_hide(app->info_bar);
        }
    } else {
        char *modules_text = details_nested_modules_text_dup(info->modules);
        if (g_strcmp0(ui_current_section_id(app), "local") == 0) {
            char *size_text = details_size_text_with_format_detail(app, app->selected_module, info);
            details_label_set_plain(app->details.size, size_text);
            g_free(size_text);
        }
        details_modules_label_set(app, modules_text, TRUE);
        g_free(modules_text);
    }

    backend_open_file_info_free(info);
    g_clear_error(&error);
    details_file_info_clear_current(app, ctx->op);
    ui_async_context_free(ctx);
}

static void on_details_deps_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    UiAsyncContext *ctx = (UiAsyncContext *)user_data;
    AppState *app;
    gchar **deps = NULL;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    app = ctx->app;
    deps = backend_get_deps_finish(result, &error);

    if (app != NULL && app->details.depends != NULL &&
        ctx->module_index == app->details_deps_seq) {
        if (error != NULL) {
            details_label_set_plain(app->details.depends, "—");
        } else if (deps == NULL || deps[0] == NULL) {
            details_label_set_plain(app->details.depends, _("None"));
        } else {
            gchar *joined = g_strjoinv(", ", deps);
            details_label_set_plain(app->details.depends, joined);
            gtk_widget_set_tooltip_text(GTK_WIDGET(app->details.depends), joined);
            g_free(joined);
        }
    }

    g_strfreev(deps);
    g_clear_error(&error);
    ui_async_context_free(ctx);
}

static void details_pane_update(AppState *app, const ModuleInfo *module)
{
    char *size_text;
    char *directory_text = NULL;

    if (app == NULL || app->details.name == NULL) {
        return;
    }

    app->selected_module = (ModuleInfo *)module;

    details_label_set_name(app->details.name, module != NULL ? module->name : NULL);
    details_label_set_plain(app->details.status, ui_details_status_text(app, module));
    ui_section_status_widget_apply(app, app->details.status_icon, module, ui_current_section_id(app));
    details_label_set_plain(app->details.version, module != NULL ? module->version : NULL);

    size_text = details_size_text_for_section(app, module);
    details_label_set_plain(app->details.size, size_text);
    g_free(size_text);

    if (module != NULL && module->path != NULL && module->path[0] != '\0') {
        directory_text = g_path_get_dirname(module->path);
    }
    details_label_set_plain(app->details.path, directory_text);
    gtk_widget_set_tooltip_text(GTK_WIDGET(app->details.path),
                                module != NULL && module->path != NULL && module->path[0] != '\0'
                                    ? module->path
                                    : NULL);
    g_free(directory_text);
    if (app->details.repo != NULL) {
        const char *repo_value = (module != NULL && module->repo != NULL && module->repo[0] != '\0')
                                    ? module->repo
                                    : NULL;
        details_label_set_plain(app->details.repo, repo_value);
        gtk_widget_set_tooltip_text(GTK_WIDGET(app->details.repo), repo_value);
    }
    if (module == NULL && !app_state_can_run_backend(app)) {
        details_label_set_plain(app->details.description, _("Install modman"));
    } else {
        details_label_set_plain(app->details.description,
                                (module != NULL && module->desc != NULL && module->desc[0] != '\0')
                                    ? module->desc
                                    : NULL);
    }
    gtk_widget_set_tooltip_text(GTK_WIDGET(app->details.description),
                                (module != NULL && module->desc != NULL && module->desc[0] != '\0')
                                    ? module->desc
                                    : NULL);

    app->details_deps_seq++;
    if (app->details.depends != NULL) {
        if (module == NULL || module->name == NULL || module->name[0] == '\0') {
            details_label_set_plain(app->details.depends, NULL);
        } else if (!app_state_can_run_backend(app)) {
            details_label_set_plain(app->details.depends, "—");
        } else {
            UiAsyncContext *deps_ctx = ui_async_context_new(app);
            if (deps_ctx != NULL) {
                deps_ctx->module_index = app->details_deps_seq;
                deps_ctx->module_name = g_strdup(module->name);
                details_label_set_plain(app->details.depends, _("Loading…"));
                backend_get_deps_async(module->name, NULL,
                                       on_details_deps_done, deps_ctx);
            }
        }
    }

    details_file_info_start(app, module);

    ui_sync_sidebar_accessibility(app);
}

static GtkWidget *details_pane_build(AppState *app, const SectionSpec *section)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *divider;
    GtkWidget *status_box;
    GtkWidget *status_caption;
    GtkWidget *status_value_box;
    char *details_desc;

    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_size_request(box, UI_DETAILS_PANE_MIN_WIDTH, -1);

    app->details.name = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_xalign(app->details.name, 0.0f);
    gtk_label_set_ellipsize(app->details.name, PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(app->details.name, UI_DETAILS_COMPACT_WRAP_CHARS + 2);
    g_object_set_data_full(G_OBJECT(app->details.name),
                           "details-accessible-prefix",
                           g_strdup(_("Selected module")),
                           g_free);
    details_label_set_name(app->details.name, NULL);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(app->details.name), FALSE, FALSE, 0);

    divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), divider, FALSE, FALSE, 0);

    status_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_halign(status_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(status_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(status_box, TRUE);
    status_caption = gtk_label_new(_("Status:"));
    gtk_label_set_xalign(GTK_LABEL(status_caption), 0.0f);
    ui_set_widget_accessible(status_caption,
                             _("Status"),
                             _("Caption for the selected module current status."));
    gtk_box_pack_start(GTK_BOX(status_box), status_caption, FALSE, FALSE, 0);

    status_value_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app->details.status_icon = gtk_image_new();
    gtk_widget_set_halign(app->details.status_icon, GTK_ALIGN_START);
    gtk_widget_set_valign(app->details.status_icon, GTK_ALIGN_CENTER);
    app->details.status = GTK_LABEL(gtk_label_new(DETAILS_PLACEHOLDER));
    gtk_label_set_xalign(app->details.status, 0.0f);
    g_object_set_data_full(G_OBJECT(app->details.status),
                           "details-accessible-prefix",
                           g_strdup(_("Status")),
                           g_free);
    details_label_sync_accessibility(app->details.status, NULL);
    gtk_box_pack_start(GTK_BOX(status_value_box), app->details.status_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_value_box), GTK_WIDGET(app->details.status), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_box), status_value_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);

    details_pane_append_value_row(box,
                                  "package-x-generic",
                                  _("Version:"),
                                  TRUE,
                                  FALSE,
                                  PANGO_ELLIPSIZE_END,
                                  &app->details.version,
                                  0,
                                  0,
                                  1);
    details_pane_append_value_row(box,
                                  "drive-harddisk",
                                  _("Size:"),
                                  TRUE,
                                  FALSE,
                                  PANGO_ELLIPSIZE_END,
                                  &app->details.size,
                                  0,
                                  0,
                                  1);
    details_pane_append_value_row(box,
                                  "folder",
                                  _("Directory:"),
                                  TRUE,
                                  TRUE,
                                  PANGO_ELLIPSIZE_END,
                                  &app->details.path,
                                  0,
                                  0,
                                  1);
    details_label_apply_compact_wrap(app->details.path);
    details_pane_append_value_row(box,
                                  "network-server",
                                  _("Repository:"),
                                  TRUE,
                                  TRUE,
                                  PANGO_ELLIPSIZE_NONE,
                                  &app->details.repo,
                                  0,
                                  0,
                                  1);
    details_label_apply_compact_wrap(app->details.repo);

    divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), divider, FALSE, FALSE, 0);

    details_pane_append_value_row(box,
                                  "text-x-generic",
                                  _("Description:"),
                                  TRUE,
                                  TRUE,
                                  PANGO_ELLIPSIZE_NONE,
                                  &app->details.description,
                                  0,
                                  0,
                                  1);
    details_label_apply_compact_wrap(app->details.description);

    details_pane_append_value_row(box,
                                  "applications-other",
                                  _("Dependencies:"),
                                  TRUE,
                                  TRUE,
                                  PANGO_ELLIPSIZE_NONE,
                                  &app->details.depends,
                                  0,
                                  0,
                                  1);
    details_label_apply_compact_wrap(app->details.depends);

    app->details.modules = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(app->details.modules, 0.0f);
    gtk_label_set_yalign(app->details.modules, 0.0f);
    gtk_label_set_selectable(app->details.modules, TRUE);
    gtk_label_set_line_wrap(app->details.modules, TRUE);
    gtk_label_set_line_wrap_mode(app->details.modules, PANGO_WRAP_WORD_CHAR);
    g_object_set_data_full(G_OBJECT(app->details.modules),
                           "details-accessible-prefix",
                           g_strdup(_("Модули внутри")),
                           g_free);
    details_label_sync_accessibility(app->details.modules, NULL);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(app->details.modules), FALSE, FALSE, 0);
    gtk_widget_hide(GTK_WIDGET(app->details.modules));

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(""), TRUE, TRUE, 0);

    details_desc = g_strdup_printf(_("Details pane for the “%s” section. It shows properties of the selected module."),
                                   section != NULL ? _(section->title) : _("modules"));
    ui_set_widget_accessible_with_role(box,
                                       _("Details pane"),
                                       details_desc,
                                       ATK_ROLE_PANEL);
    g_free(details_desc);
    details_pane_update(app, NULL);
    return box;
}

static void details_pane_bind_for_list(AppState *app, GtkListBox *list_box)
{
    if (app == NULL || list_box == NULL) {
        return;
    }

    app->details.name = g_object_get_data(G_OBJECT(list_box), "details-name");
    app->details.status_icon = g_object_get_data(G_OBJECT(list_box), "details-status-icon");
    app->details.status = g_object_get_data(G_OBJECT(list_box), "details-status");
    app->details.version = g_object_get_data(G_OBJECT(list_box), "details-version");
    app->details.category = g_object_get_data(G_OBJECT(list_box), "details-category");
    app->details.size = g_object_get_data(G_OBJECT(list_box), "details-size");
    app->details.path = g_object_get_data(G_OBJECT(list_box), "details-path");
    app->details.repo = g_object_get_data(G_OBJECT(list_box), "details-repo");
    app->details.description = g_object_get_data(G_OBJECT(list_box), "details-description");
    app->details.depends = g_object_get_data(G_OBJECT(list_box), "details-depends");
    app->details.modules = g_object_get_data(G_OBJECT(list_box), "details-modules");
    app->details.hooks_start = g_object_get_data(G_OBJECT(list_box), "details-hooks-start");
    app->details.hooks_stop = g_object_get_data(G_OBJECT(list_box), "details-hooks-stop");
}

static guint ui_list_box_row_count(GtkListBox *list_box)
{
    GList *children;
    GList *iter;
    guint count = 0U;

    if (list_box == NULL) {
        return 0U;
    }

    children = gtk_container_get_children(GTK_CONTAINER(list_box));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (GTK_IS_LIST_BOX_ROW(iter->data)) {
            count++;
        }
    }
    g_list_free(children);
    return count;
}

static char *ui_section_accessible_name(AppState *app, const SectionSpec *section)
{
    guint count = 0U;
    gint index;

    if (section == NULL) {
        return g_strdup(_("Module section"));
    }

    index = section_index_from_id(section->id);
    if (app != NULL && index >= 0 && index < SECTION_COUNT) {
        count = ui_list_box_row_count(app->list_boxes[index]);
        return g_strdup_printf(_("Section “%s”, modules: %u"), _(section->title), count);
    }

    return g_strdup_printf(_("Section “%s”"), _(section->title));
}

static char *ui_section_accessible_description(AppState *app, const SectionSpec *section)
{
    guint count = 0U;
    gint index;

    if (section == NULL) {
        return g_strdup(_("Module section."));
    }

    index = section_index_from_id(section->id);
    if (app != NULL && index >= 0 && index < SECTION_COUNT) {
        count = ui_list_box_row_count(app->list_boxes[index]);
        return g_strdup_printf(_("%s. %s There are currently %u modules in the list."),
                               _(section->empty_title),
                               _(section->empty_text),
                               count);
    }

    return g_strdup_printf("%s. %s", _(section->empty_title), _(section->empty_text));
}

static void ui_set_section_sidebar_name(AppState *app, GtkWidget *page, const SectionSpec *section)
{
    char *name;
    char *description;

    if (page == NULL || section == NULL) {
        return;
    }

    name = ui_section_accessible_name(app, section);
    description = ui_section_accessible_description(app, section);
    ui_set_widget_accessible(page, name, description);
    g_free(description);
    g_free(name);
}

static gboolean ui_widget_contains_label_text(GtkWidget *widget, const char *text)
{
    GList *children;
    GList *iter;

    if (widget == NULL || text == NULL) {
        return FALSE;
    }

    if (GTK_IS_LABEL(widget) && g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), text) == 0) {
        return TRUE;
    }

    if (GTK_IS_BUTTON(widget)) {
        const char *label = gtk_button_get_label(GTK_BUTTON(widget));
        if (label != NULL && g_strcmp0(label, text) == 0) {
            return TRUE;
        }
    }

    if (!GTK_IS_CONTAINER(widget)) {
        return FALSE;
    }

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (ui_widget_contains_label_text(GTK_WIDGET(iter->data), text)) {
            g_list_free(children);
            return TRUE;
        }
    }
    g_list_free(children);
    return FALSE;
}

static void ui_sync_sidebar_accessibility_recursive(GtkWidget *widget,
                                                    AppState *app,
                                                    const SectionSpec *section)
{
    GList *children;
    GList *iter;

    if (widget == NULL || section == NULL) {
        return;
    }

    if ((GTK_IS_BUTTON(widget) || gtk_widget_get_can_focus(widget) || GTK_IS_LABEL(widget)) &&
        ui_widget_contains_label_text(widget, _(section->title))) {
        char *name = ui_section_accessible_name(app, section);
        char *description = ui_section_accessible_description(app, section);

        ui_set_widget_accessible(widget, name, description);
        g_free(description);
        g_free(name);
    }

    if (!GTK_IS_CONTAINER(widget)) {
        return;
    }

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL; iter = iter->next) {
        ui_sync_sidebar_accessibility_recursive(GTK_WIDGET(iter->data), app, section);
    }
    g_list_free(children);
}

static void ui_sync_sidebar_accessibility(AppState *app)
{
    GList *pages;
    GList *iter;
    guint i;

    if (app == NULL) {
        return;
    }

    if (app->stack != NULL) {
        pages = gtk_container_get_children(GTK_CONTAINER(app->stack));
        for (iter = pages; iter != NULL; iter = iter->next) {
            const char *section_id = g_object_get_data(G_OBJECT(iter->data), "section-id");
            const SectionSpec *section = section_spec_from_id(section_id);

            ui_set_section_sidebar_name(app, GTK_WIDGET(iter->data), section);
        }
        g_list_free(pages);
    }

    if (app->sidebar == NULL) {
        return;
    }

    for (i = 0U; i < SECTION_COUNT; i++) {
        ui_sync_sidebar_accessibility_recursive(GTK_WIDGET(app->sidebar), app, &SECTION_SPECS[i]);
    }
}

static void on_section_list_children_changed(GtkContainer *container, GtkWidget *child, gpointer user_data)
{
    (void)container;
    (void)child;

    ui_sync_sidebar_accessibility((AppState *)user_data);
}

static gboolean ui_module_is_erofs(const ModuleInfo *module)
{
    if (module == NULL) {
        return FALSE;
    }

    if (module->name != NULL && g_str_has_suffix(module->name, ".erofs")) {
        return TRUE;
    }

    if (module->path != NULL && g_str_has_suffix(module->path, ".erofs")) {
        return TRUE;
    }

    return FALSE;
}

static void ui_apply_row_erofs_availability(AppState *app, GtkListBoxRow *row, gboolean force_local_context)
{
    ModuleInfo *module;
    gboolean in_local_section;
    gboolean unavailable;
    GtkWidget *name_label;
    GtkWidget *meta_label;

    if (app == NULL || row == NULL) {
        return;
    }

    module = g_object_get_data(G_OBJECT(row), "module-info");
    if (module == NULL) {
        return;
    }

    in_local_section = force_local_context || g_strcmp0(ui_current_section_id(app), "local") == 0;
    unavailable = in_local_section && !app->capabilities.has_erofs && ui_module_is_erofs(module);

    gtk_widget_set_sensitive(GTK_WIDGET(row), !unavailable);
    gtk_widget_set_tooltip_text(GTK_WIDGET(row), unavailable ? _("erofs-utils is not installed") : NULL);

    name_label = g_object_get_data(G_OBJECT(row), "row-name-label");
    meta_label = g_object_get_data(G_OBJECT(row), "row-meta-label");
    if (name_label != NULL) {
        gtk_widget_set_sensitive(name_label, !unavailable);
    }
    if (meta_label != NULL) {
        gtk_widget_set_sensitive(meta_label, !unavailable);
    }
}

static void ui_refresh_local_erofs_markers(AppState *app)
{
    GList *children;
    GList *iter;

    if (app == NULL || app->list_boxes[SECTION_LOCAL] == NULL) {
        return;
    }

    children = gtk_container_get_children(GTK_CONTAINER(app->list_boxes[SECTION_LOCAL]));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (GTK_IS_LIST_BOX_ROW(iter->data)) {
            ui_apply_row_erofs_availability(app, GTK_LIST_BOX_ROW(iter->data), TRUE);
        }
    }
    g_list_free(children);
}

static GtkWidget *ui_build_compact_module_row(ModuleInfo *module, const char *section_id)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *status = ui_build_status_widget(NULL, module, section_id);
    GtkWidget *name_label = gtk_label_new(NULL);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *size_label = gtk_label_new(NULL);
    char *name_markup;
    char *size_text;
    const char *desc_text;
    char *accessible_name;

    gtk_container_add(GTK_CONTAINER(row), hbox);
    g_object_set_data(G_OBJECT(row), "row-is-compact", GINT_TO_POINTER(1));
    gtk_widget_set_margin_top(hbox, 1);
    gtk_widget_set_margin_bottom(hbox, 1);
    gtk_widget_set_margin_start(hbox, 6);
    gtk_widget_set_margin_end(hbox, 6);

    {
        const char *name_text = (module != NULL && module->name != NULL) ? module->name : "";
        gtk_label_set_text(GTK_LABEL(name_label), name_text);
    }
    name_markup = NULL;
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(name_label), 40);

    size_text = ui_format_size_or_layer_for_section(module, section_id);
    {
        char *escaped_size = g_markup_escape_text(size_text != NULL ? size_text : "", -1);
        char *size_markup = g_strdup_printf("<span alpha='70%%' size='small'>%s</span>", escaped_size);
        gtk_label_set_markup(GTK_LABEL(size_label), size_markup);
        gtk_label_set_xalign(GTK_LABEL(size_label), 1.0f);
        g_free(size_markup);
        g_free(escaped_size);
    }

    gtk_widget_set_hexpand(spacer, TRUE);

    desc_text = (module != NULL && module->desc != NULL && module->desc[0] != '\0')
        ? module->desc
        : NULL;

    if (desc_text != NULL) {
        char *meta_plain;
        if (g_strcmp0(section_id, "inet") == 0) {
            meta_plain = g_strdup(module != NULL && module->desc != NULL && module->desc[0] != '\0'
                                  ? module->desc : "");
        } else if (g_strcmp0(section_id, "local") == 0) {
            meta_plain = g_strdup(module != NULL && module->path != NULL && module->path[0] != '\0'
                                  ? module->path : "");
        } else {
            meta_plain = ui_build_meta_text_for_section(module, section_id);
        }
        GtkWidget *meta_label = gtk_label_new(NULL);
        char *escaped_meta = g_markup_escape_text(meta_plain != NULL ? meta_plain : "", -1);
        char *meta_markup = g_strdup_printf("<span alpha='60%%' size='small'>%s</span>", escaped_meta);

        gtk_label_set_markup(GTK_LABEL(meta_label), meta_markup);
        gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(meta_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(meta_label), 50);
        gtk_widget_set_tooltip_text(row, desc_text);

        gtk_box_pack_start(GTK_BOX(hbox), status, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), name_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), meta_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), size_label, FALSE, FALSE, 0);

        g_free(meta_markup);
        g_free(escaped_meta);
        g_free(meta_plain);

        g_object_set_data(G_OBJECT(row), "row-meta-label", meta_label);
    } else {
        gtk_box_pack_start(GTK_BOX(hbox), status, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), name_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), size_label, FALSE, FALSE, 0);
    }

    accessible_name = g_strdup_printf(_("Module %s, %s"),
                                      module != NULL && module->name != NULL ? module->name : _("Unnamed module"),
                                      ui_status_text(module));
    ui_set_widget_accessible_with_role(row, accessible_name, desc_text != NULL ? desc_text : _("no description"),
                                       ATK_ROLE_LIST_ITEM);
    g_free(accessible_name);

    g_object_set_data(G_OBJECT(row), "module-info", module);
    g_object_set_data(G_OBJECT(row), "row-name-label", name_label);

    g_free(name_markup);
    g_free(size_text);

    return row;
}

static GtkWidget *ui_build_module_row_for_section(AppState *app, ModuleInfo *module, const char *section_id)
{
    GtkWidget *row;
    GtkWidget *grid;
    GtkWidget *status;

    if (app != NULL && app->compact_mode) {
        return ui_build_compact_module_row(module, section_id);
    }

    row = gtk_list_box_row_new();
    grid = gtk_grid_new();
    status = ui_build_status_widget(NULL, module, section_id);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *name_label = gtk_label_new(NULL);
    GtkWidget *meta_label = gtk_label_new(NULL);
    GtkWidget *badge_label = NULL;
    char *name_markup;
    char *meta_markup;
    char *accessible_name;
    char *size_text;
    char *side_text = NULL;
    const char *desc_text;

    gtk_container_add(GTK_CONTAINER(row), grid);
    gtk_widget_set_margin_top(grid, 3);
    gtk_widget_set_margin_bottom(grid, 3);
    gtk_widget_set_margin_start(grid, 8);
    gtk_widget_set_margin_end(grid, 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 0);

    gtk_widget_set_hexpand(text_box, TRUE);

    name_markup = ui_build_name_markup(module, NULL);
    meta_markup = ui_build_meta_markup_for_section(module, section_id, NULL);

    gtk_label_set_markup(GTK_LABEL(name_label), name_markup);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);

    gtk_label_set_markup(GTK_LABEL(meta_label), meta_markup);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(meta_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(meta_label), 60);

    gtk_box_pack_start(GTK_BOX(text_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text_box), meta_label, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(grid), status, 0, 0, 1, 2);
    gtk_grid_attach(GTK_GRID(grid), text_box, 1, 0, 1, 2);

    if ((g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) &&
        module != NULL && module->layer != NULL && module->layer[0] != '\0') {
        side_text = ui_layer_number_dup(module->layer);
    } else if (module != NULL && module->version != NULL && module->version[0] != '\0') {
        side_text = g_strdup(module->version);
    }

    if (side_text != NULL && side_text[0] != '\0') {
        char *escaped_version = g_markup_escape_text(side_text, -1);
        char *badge_markup = g_strdup_printf("<span alpha='70%%' size='small'>%s</span>", escaped_version);

        badge_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(badge_label), badge_markup);
        gtk_widget_set_halign(badge_label, GTK_ALIGN_END);
        gtk_widget_set_valign(badge_label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), badge_label, 2, 0, 1, 1);

        g_free(badge_markup);
        g_free(escaped_version);
    }
    g_free(side_text);

    size_text = ui_format_size_or_layer_for_section(module, section_id);
    desc_text = (module != NULL && module->desc != NULL && module->desc[0] != '\0') ? module->desc : _("no description");
    accessible_name = g_strdup_printf(_("Module %s, %s"),
                                      module != NULL && module->name != NULL ? module->name : _("Unnamed module"),
                                      ui_status_text(module));
    {
        const char *category_text = (module != NULL && module->category != NULL && module->category[0] != '\0')
            ? module->category
            : _("no category");
        char *accessible_desc = g_strdup_printf(_("Description: %s. Category: %s. Size or layer: %s."),
                                                desc_text,
                                                category_text,
                                                size_text);

        ui_set_widget_accessible_with_role(row,
                                           accessible_name,
                                           accessible_desc,
                                           ATK_ROLE_LIST_ITEM);
        g_free(accessible_desc);
    }

    g_object_set_data(G_OBJECT(row), "module-info", module);
    g_object_set_data(G_OBJECT(row), "row-name-label", name_label);
    g_object_set_data(G_OBJECT(row), "row-meta-label", meta_label);

    g_free(accessible_name);
    g_free(size_text);
    g_free(meta_markup);
    g_free(name_markup);

    return row;
}

GtkWidget *ui_build_module_row(ModuleInfo *module)
{
    return ui_build_module_row_for_section(NULL, module, NULL);
}

void ui_set_toolbar_state(AppState *app, const char *section_id)
{
    gint index;
    GtkListBoxRow *selected_row = NULL;
    gboolean can_load = FALSE;
    gboolean can_unload = FALSE;
    gboolean no_modman;
    gboolean no_layering;
    gboolean can_modify;
    gboolean is_system_section;
    gboolean has_selection;

    if (app == NULL) {
        return;
    }

    index = section_index_from_id(section_id != NULL ? section_id : ui_current_section_id(app));
    if (index >= 0 && index < SECTION_COUNT && app->list_boxes[index] != NULL) {
        selected_row = gtk_list_box_get_selected_row(app->list_boxes[index]);
    }

    has_selection = selected_row != NULL;
    if (has_selection) {
        can_load = (index == SECTION_INET || index == SECTION_LOCAL);
        can_unload = (index == SECTION_LOADED);
    }

    no_modman = !app->capabilities.has_modman;
    no_layering = !app->capabilities.has_aufs && !app->capabilities.has_overlayfs;
    can_modify = app_state_can_modify_modules(app);
    is_system_section = index == SECTION_SYSTEM;

    if (no_modman) {
        can_load = FALSE;
        can_unload = FALSE;
    }

    if (!can_modify) {
        can_load = FALSE;
        can_unload = FALSE;
    }

    gboolean is_load_section = (index == SECTION_INET || index == SECTION_LOCAL);
    gboolean is_unload_section = (index == SECTION_LOADED || index == SECTION_SYSTEM);
    gboolean is_local_section = (index == SECTION_LOCAL);

    if (app->btn_refresh != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->btn_refresh), !no_modman);
    }
    if (app->btn_load != NULL) {
        gtk_widget_set_visible(GTK_WIDGET(app->btn_load), !no_layering && is_load_section);
        gtk_widget_set_sensitive(GTK_WIDGET(app->btn_load), can_load);
    }
    if (app->mi_file_load != NULL) {
        gtk_widget_set_sensitive(app->mi_file_load,
                                 !no_layering && app_state_can_modify_modules(app));
    }
    if (app->btn_unload != NULL) {
        gtk_widget_set_visible(GTK_WIDGET(app->btn_unload), !no_layering && is_unload_section);
        gtk_widget_set_sensitive(GTK_WIDGET(app->btn_unload), can_unload);
        gtk_widget_set_tooltip_text(GTK_WIDGET(app->btn_unload),
                                    ui_unload_button_tooltip(no_modman,
                                                             no_layering,
                                                             can_modify,
                                                             is_system_section,
                                                             has_selection));
    }
    if (app->btn_delete != NULL) {
        gtk_widget_set_visible(GTK_WIDGET(app->btn_delete), is_local_section);
    }
    on_local_selection_changed(app->list_boxes[SECTION_LOCAL], app);
}

static void app_state_load_current_section(AppState *app)
{
    const char *section_id;
    UiAsyncContext *ctx;

    if (app == NULL) {
        return;
    }

    section_id = ui_current_section_id(app);
    ui_set_toolbar_state(app, section_id);

    if (section_id == NULL) {
        return;
    }

    if (!app_state_can_run_backend(app)) {
        app_state_show_info(app,
                            GTK_MESSAGE_ERROR,
                            _("modman was not found: list refresh is unavailable. Install modman."),
                            NULL,
                            NULL,
                            FALSE);
        return;
    }

    if (g_strcmp0(section_id, "loaded") == 0) {
        app_state_start_op(app);
        ctx = ui_async_context_new(app);
        if (ctx != NULL) {
            backend_list_loaded_async(NULL, FALSE, app->current_op, on_list_done, ctx);
        }
        return;
    }

    if (g_strcmp0(section_id, "system") == 0) {
        app_state_start_op(app);
        ctx = ui_async_context_new(app);
        if (ctx != NULL) {
            backend_list_loaded_async(NULL, TRUE, app->current_op, on_list_done, ctx);
        }
        return;
    }

    if (g_strcmp0(section_id, "local") == 0) {
        app_state_start_op(app);
        ctx = ui_async_context_new(app);
        if (ctx != NULL) {
            backend_list_local_async(NULL, app->current_op, on_local_done, ctx);
        }
        return;
    }

    if (g_strcmp0(section_id, "inet") == 0) {
        const char *query = "";
        gboolean query_is_empty;
        gint64 current_db_mtime = UI_INET_CACHE_MTIME_UNKNOWN;
        char *current_modman_bin;

        if (app->search_entry != NULL) {
            const char *entry_text = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
            if (entry_text != NULL) {
                query = entry_text;
            }
        }

        query_is_empty = query_is_empty_after_trim(query);
        current_modman_bin = ui_inet_cache_effective_modman_bin_dup(app);

        if (!query_is_empty && !search_query_is_valid(query)) {
            app_state_cancel_cancellable(&app->inet_prefetch_op);
            app_state_cancel_cancellable(&app->current_op);
            if (app->list_boxes[SECTION_INET] != NULL && ui_list_box_row_count(app->list_boxes[SECTION_INET]) > 0U) {
                GPtrArray *empty_items = g_ptr_array_new_with_free_func((GDestroyNotify)ui_module_info_free);
                ui_render_items_to_section(app, "inet", empty_items);
                g_ptr_array_unref(empty_items);
            }
            ui_set_inet_search_state(app, _("Введите запрос"));
            g_free(current_modman_bin);
            return;
        }

        if (app->inet_cache != NULL &&
            g_strcmp0(current_modman_bin, app->inet_cache_modman_bin) != 0) {
            ui_inet_cache_clear(app);
        }

        if (app->inet_cache_force_refresh) {
            ui_inet_cache_clear(app);
        }

        if (query_is_empty && app->inet_cache != NULL) {
            GPtrArray *cached_items = NULL;
            gboolean mtime_matches;

            current_db_mtime = ui_inet_cache_current_db_mtime(app);
            mtime_matches = current_db_mtime != UI_INET_CACHE_MTIME_UNKNOWN &&
                            current_db_mtime == app->inet_cache_db_mtime;

            if (mtime_matches) {
                cached_items = ui_module_info_array_dup(app->inet_cache);
            }

            if (cached_items != NULL) {
                /* Invalidate any in-flight inet search so late callbacks
                 * cannot repaint after this cache-hit fast path. */
                app_state_cancel_cancellable(&app->current_op);
                ui_render_items_to_section(app, "inet", cached_items);
                ui_apply_search_highlight_to_list(app->list_boxes[SECTION_INET], app->pending_query);
                app_state_show_info(app,
                                    GTK_MESSAGE_INFO,
                                    _("Online catalog loaded."),
                                    NULL,
                                    NULL,
                                    TRUE);
                g_ptr_array_unref(cached_items);
                app->inet_cache_force_refresh = FALSE;
                g_free(current_modman_bin);
                return;
            }

            ui_inet_cache_clear(app);
        }

        app->inet_cache_force_refresh = FALSE;
        app_state_cancel_cancellable(&app->inet_prefetch_op);
        app_state_start_op(app);
        ctx = ui_async_context_new(app);
        if (ctx != NULL) {
            ui_set_inet_search_state(app, _("Поиск…"));
            ctx->cacheable_inet_query = query_is_empty;
            if (query_is_empty) {
                ctx->inet_modman_bin = g_strdup(current_modman_bin != NULL ? current_modman_bin : "");
            }
            backend_search_async(NULL, query, app->current_op, on_search_done, ctx);
        }

        g_free(current_modman_bin);
    }
}

static void on_search_changed_impl(AppState *app)
{
    const char *section_id;
    const char *query;

    ui_set_toolbar_state(app, ui_current_section_id(app));

    if (app == NULL || app->search_entry == NULL) {
        return;
    }

    section_id = ui_current_section_id(app);
    query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));

    if (app->search_debounce_id != 0) {
        g_source_remove(app->search_debounce_id);
        app->search_debounce_id = 0;
    }

    g_free(app->pending_query);
    app->pending_query = query != NULL ? g_strdup(query) : NULL;

    ui_invalidate_section_filter(app, section_id);

    if (g_strcmp0(section_id, "inet") != 0) {
        return;
    }

    if (query_is_empty_after_trim(app->pending_query) || !search_query_is_valid(app->pending_query)) {
        app_state_cancel_cancellable(&app->current_op);
        if (g_strcmp0(section_id, "inet") == 0 && app->list_boxes[SECTION_INET] != NULL &&
            ui_list_box_row_count(app->list_boxes[SECTION_INET]) > 0U) {
            GPtrArray *empty_items = g_ptr_array_new_with_free_func((GDestroyNotify)ui_module_info_free);
            ui_render_items_to_section(app, "inet", empty_items);
            g_ptr_array_unref(empty_items);
        }
        ui_set_inet_search_state(app, _("Введите запрос"));
        return;
    }

    app_state_cancel_cancellable(&app->current_op);
    ui_set_inet_search_state(app, _("Поиск…"));
    app->search_debounce_id = g_timeout_add(220, on_search_debounce_fire, app);
}

static void on_action_clicked_impl(AppState *app, gboolean is_unload)
{
    const char *section_id;
    ModuleInfo *module;
    UiAsyncContext *ctx;
    gchar **selected_names = NULL;
    guint selected_count = 0U;

    ui_set_toolbar_state(app, ui_current_section_id(app));

    if (app == NULL) {
        return;
    }

    section_id = ui_current_section_id(app);
    module = app->selected_module;

    if (!is_unload && (g_strcmp0(section_id, "inet") == 0 || g_strcmp0(section_id, "local") == 0)) {
        gboolean use_paths = g_strcmp0(section_id, "local") == 0;
        selected_names = ui_selected_module_values_dup(app, &selected_count, FALSE);
        if (selected_count > 1U) {
            gchar **selected_args = ui_selected_module_values_dup(app, NULL, use_paths);

            app_state_start_op(app);
            ctx = ui_async_context_new(app);
            if (ctx == NULL) {
                g_strfreev(selected_names);
                g_strfreev(selected_args);
                app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare attach operation."), NULL, NULL, FALSE);
                return;
            }

            ctx->module_names = selected_names;
            ctx->module_args = selected_args;
            ctx->module_total = selected_count;
            app_state_progress_arm(app);
            ui_batch_load_next(app, ctx);
            return;
        }
    }

    g_strfreev(selected_names);

    if (section_id == NULL || module == NULL || module->name == NULL || module->name[0] == '\0') {
        app_state_show_info(app, GTK_MESSAGE_WARNING, _("Select a module in the list first."), NULL, NULL, FALSE);
        return;
    }

    if (!app_state_can_modify_modules(app)) {
        app_state_show_info(app,
                            GTK_MESSAGE_WARNING,
                            _("Attach/detach operations are unavailable: critical environment capabilities are missing."),
                            NULL,
                            NULL,
                            FALSE);
        return;
    }

    if (is_unload && g_strcmp0(section_id, "system") == 0) {
        app_state_show_info(app,
                            GTK_MESSAGE_WARNING,
                            _("Системный слой нельзя отключить из этого списка"),
                            NULL,
                            NULL,
                            FALSE);
        return;
    }

    app_state_start_op(app);
    ctx = ui_async_context_new(app);
    if (ctx == NULL) {
        app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare operation."), NULL, NULL, FALSE);
        return;
    }

    ctx->module_name = g_strdup(module->name);
    ctx->module_path = g_strdup(module->path != NULL && module->path[0] != '\0' ? module->path : module->name);
    ctx->is_unload = is_unload;

    if (is_unload) {
        app_state_set_operation_status(app, UI_OPERATION_STATUS_DISCONNECTING, NULL);
        app_state_progress_arm(app);
        backend_unload_async(NULL, module, app->current_op, on_module_action_done, ctx);
        return;
    }

    app_state_set_operation_status(app, UI_OPERATION_STATUS_CONNECTING, NULL);
    app_state_progress_arm(app);
    if (g_strcmp0(section_id, "local") == 0 && module->path != NULL && module->path[0] != '\0') {
        ModuleInfo *local_module = ui_module_info_dup(module);
        if (local_module == NULL) {
            ui_async_context_free(ctx);
            app_state_progress_disarm(app);
            app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare local module."), NULL, NULL, FALSE);
            return;
        }
        g_free(local_module->name);
        local_module->name = g_strdup(module->path);
        backend_load_async(NULL, local_module, app->current_op, on_module_action_done, ctx);
        ui_module_info_free(local_module);
        return;
    }
    backend_load_async(NULL, module, app->current_op, on_module_action_done, ctx);
}

static void on_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    clear_old_files((AppState *)user_data);
}

static void on_local_selection_changed(GtkListBox *box, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GList *selected_rows;

    if (app == NULL || app->btn_delete == NULL) {
        return;
    }

    if (app->list_boxes[SECTION_LOCAL] == NULL) {
        ui_apply_local_delete_button_state(app, NULL);
        return;
    }

    selected_rows = gtk_list_box_get_selected_rows(box != NULL ? box : app->list_boxes[SECTION_LOCAL]);

    if (selected_rows == NULL) {
        app->last_clicked_local_row = NULL;
    } else if (app->last_clicked_local_row == NULL ||
               g_list_find(selected_rows, app->last_clicked_local_row) == NULL) {
        app->last_clicked_local_row = GTK_LIST_BOX_ROW(selected_rows->data);
    }

    ui_apply_local_delete_button_state(app, selected_rows);

    g_list_free(selected_rows);
}

static GtkListBoxRow *ui_list_last_clicked_row(GtkListBox *box)
{
    if (box == NULL) {
        return NULL;
    }

    return g_object_get_data(G_OBJECT(box), "last-clicked-row");
}

static void ui_list_set_last_clicked_row(GtkListBox *box, GtkListBoxRow *row)
{
    if (box == NULL) {
        return;
    }

    g_object_set_data(G_OBJECT(box), "last-clicked-row", row);
}

static void ui_list_select_range(GtkListBox *box,
                                 GtkListBoxRow *anchor,
                                 GtkListBoxRow *target,
                                 gboolean extend_existing)
{
    GList *children;
    GList *iter;
    gint anchor_index;
    gint target_index;
    gint start_index;
    gint end_index;

    if (box == NULL || anchor == NULL || target == NULL) {
        return;
    }

    anchor_index = gtk_list_box_row_get_index(anchor);
    target_index = gtk_list_box_row_get_index(target);
    start_index = MIN(anchor_index, target_index);
    end_index = MAX(anchor_index, target_index);

    if (!extend_existing) {
        gtk_list_box_unselect_all(box);
    }

    children = gtk_container_get_children(GTK_CONTAINER(box));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(iter->data);
        gint row_index = gtk_list_box_row_get_index(row);

        if (row_index >= start_index && row_index <= end_index) {
            gtk_list_box_select_row(box, row);
        }
    }
    g_list_free(children);
}

static void on_context_menu_copy_name_activate(GtkMenuItem *item, gpointer user_data)
{
    const char *name;
    GtkClipboard *clipboard;

    (void)user_data;

    name = (const char *)g_object_get_data(G_OBJECT(item), "ctx-module-name");
    if (name == NULL || name[0] == '\0') {
        return;
    }

    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, name, -1);
}

static void on_context_menu_remove_activate(GtkMenuItem *item, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)item;

    if (app == NULL) {
        return;
    }
    on_btn_delete_clicked(NULL, app);
}

static void on_context_menu_unload_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    on_action_clicked_impl((AppState *)user_data, TRUE);
}

static void on_context_menu_download_activate(GtkMenuItem *item, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *module_name;
    ModuleInfo module = {0};
    UiAsyncContext *ctx;
    char *message;
    gchar *markup;

    if (app == NULL) {
        return;
    }

    module_name = (const char *)g_object_get_data(G_OBJECT(item), "ctx-module-name");
    if (module_name == NULL || module_name[0] == '\0') {
        return;
    }

    if (!app_state_can_run_backend(app)) {
        app_state_show_info(app,
                            GTK_MESSAGE_WARNING,
                            _("Download is unavailable: modman backend is missing."),
                            NULL,
                            NULL,
                            FALSE);
        return;
    }

    app_state_start_op(app);
    ctx = ui_async_context_new(app);
    if (ctx == NULL) {
        app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare download."), NULL, NULL, FALSE);
        return;
    }

    ctx->module_name = g_strdup(module_name);
    ctx->module_path = g_strdup(module_name);
    ctx->open_after_download = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "ctx-open-after-download")) != 0;

    message = g_strdup_printf(_("Downloading module “%s”…"), module_name);
    markup = ui_markup_from_text(message);
    app_state_show_info(app, GTK_MESSAGE_INFO, markup, NULL, NULL, TRUE);
    g_free(markup);
    g_free(message);

    app_state_progress_arm(app);
    module.name = (char *)module_name;
    backend_download_async(NULL, &module, app->current_op, on_download_action_done, ctx);
}

static void on_context_menu_download_open_activate(GtkMenuItem *item, gpointer user_data)
{
    g_object_set_data(G_OBJECT(item), "ctx-open-after-download", GINT_TO_POINTER(1));
    on_context_menu_download_activate(item, user_data);
}

static GtkWidget *ui_make_menu_item_with_icon(const char *icon_name, const char *label_text)
{
    GtkWidget *item = gtk_menu_item_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(item), hbox);
    gtk_widget_show_all(item);
    return item;
}

static void on_context_menu_load_activate(GtkMenuItem *item, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *load_cmd;
    const char *module_name;
    const char *module_path;
    const char *section_id;
    ModuleInfo module = {0};
    UiAsyncContext *ctx;

    if (app == NULL) {
        return;
    }

    load_cmd    = (const char *)g_object_get_data(G_OBJECT(item), "backend-loadcmd");
    module_name = (const char *)g_object_get_data(G_OBJECT(item), "ctx-module-name");
    module_path = (const char *)g_object_get_data(G_OBJECT(item), "ctx-module-path");
    section_id  = (const char *)g_object_get_data(G_OBJECT(item), "ctx-section-id");

    if (module_name == NULL || module_name[0] == '\0') {
        return;
    }

    if (!app_state_can_run_backend(app) || !app_state_can_modify_modules(app)) {
        app_state_show_info(app,
                            GTK_MESSAGE_WARNING,
                            _("Attach/detach operations are unavailable: critical environment capabilities are missing."),
                            NULL,
                            NULL,
                            FALSE);
        return;
    }

    app_state_start_op(app);
    ctx = ui_async_context_new(app);
    if (ctx == NULL) {
        app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare operation."), NULL, NULL, FALSE);
        return;
    }

    ctx->module_name = g_strdup(module_name);
    ctx->module_path = g_strdup(module_path != NULL && module_path[0] != '\0' ? module_path : module_name);
    ctx->is_unload = FALSE;

    app_state_set_operation_status(app, UI_OPERATION_STATUS_CONNECTING, NULL);

    app_state_progress_arm(app);

    if (g_strcmp0(section_id, "local") == 0 && module_path != NULL && module_path[0] != '\0') {
        ModuleInfo local_module = {0};
        local_module.name = (char *)module_path;
        local_module.path = (char *)module_path;
        backend_load_async_with_loadcmd(NULL, &local_module, load_cmd, app->current_op, on_module_action_done, ctx);
        return;
    }

    module.name = (char *)module_name;
    module.path = (char *)module_path;
    backend_load_async_with_loadcmd(NULL, &module, load_cmd, app->current_op, on_module_action_done, ctx);
}

static GtkMenu *ui_build_module_context_menu(AppState *app,
                                              GtkListBox *box,
                                              GtkListBoxRow *row,
                                              const char *section_id)
{
    ModuleInfo *module;
    GtkWidget *menu;
    GtkWidget *item;
    const char *name;
    const char *path;

    (void)box;

    module = (ModuleInfo *)g_object_get_data(G_OBJECT(row), "module-info");
    if (module == NULL || module->name == NULL || module->name[0] == '\0') {
        return NULL;
    }

    name = module->name;
    path = module->path != NULL ? module->path : "";

    menu = gtk_menu_new();

    if (g_strcmp0(section_id, "inet") == 0 || g_strcmp0(section_id, "local") == 0) {
        item = ui_make_menu_item_with_icon(ui_action_icon_name(UI_ACTION_ICON_CONNECT), _("Подключить"));
        g_object_set_data_full(G_OBJECT(item), "backend-loadcmd", NULL, NULL);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-section-id", g_strdup(section_id), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_load_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    if (g_strcmp0(section_id, "inet") == 0) {
        item = ui_make_menu_item_with_icon("folder-download", _("Скачать"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_download_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = ui_make_menu_item_with_icon(ui_action_icon_name(UI_ACTION_ICON_OPEN_LOCAL_FILE), _("Скачать и открыть"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_download_open_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    if (g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) {
        item = ui_make_menu_item_with_icon(ui_action_icon_name(UI_ACTION_ICON_DISCONNECT), _("Отключить"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-section-id", g_strdup(section_id), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_unload_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = ui_make_menu_item_with_icon("edit-copy", _("Copy name"));
    g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
    g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_copy_name_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    if (g_strcmp0(section_id, "local") == 0) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        item = ui_make_menu_item_with_icon(ui_action_icon_name(UI_ACTION_ICON_OPEN_LOCAL_FILE), _("Открыть"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-section-id", g_strdup(section_id), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_toolbar_open_local_clicked), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = ui_make_menu_item_with_icon("folder-open", _("Показать папку"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-section-id", g_strdup(section_id), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_toolbar_reveal_local_clicked), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = ui_make_menu_item_with_icon("user-trash", _("Remove"));
        g_object_set_data_full(G_OBJECT(item), "ctx-module-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-module-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(item), "ctx-section-id", g_strdup(section_id), g_free);
        g_signal_connect(item, "activate", G_CALLBACK(on_context_menu_remove_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_widget_show_all(menu);
    return GTK_MENU(menu);
}

static gboolean on_module_list_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GtkListBox *box;
    GtkListBoxRow *row;
    gboolean ctrl_pressed;
    gboolean shift_pressed;

    if (app == NULL || event == NULL || !GTK_IS_LIST_BOX(widget)) {
        return FALSE;
    }

    if (event->button == 3U) {
        GtkListBoxRow *right_row;
        const char *row_section_id;
        GtkMenu *menu;

        box = GTK_LIST_BOX(widget);
        right_row = gtk_list_box_get_row_at_y(box, (gint)event->y);
        if (right_row == NULL) {
            return FALSE;
        }

        row_section_id = (const char *)g_object_get_data(G_OBJECT(right_row), "section-id");
        if (g_strcmp0(row_section_id, "inet") != 0 &&
            g_strcmp0(row_section_id, "local") != 0 &&
            g_strcmp0(row_section_id, "loaded") != 0 &&
            g_strcmp0(row_section_id, "system") != 0) {
            return FALSE;
        }

        gtk_list_box_unselect_all(box);
        gtk_list_box_select_row(box, right_row);
        ui_list_set_last_clicked_row(box, right_row);
        details_pane_update(app, (ModuleInfo *)g_object_get_data(G_OBJECT(right_row), "module-info"));
        if (box == app->list_boxes[SECTION_LOCAL]) {
            app->last_clicked_local_row = right_row;
            on_local_selection_changed(box, app);
        }

        menu = ui_build_module_context_menu(app, box, right_row, row_section_id);
        if (menu != NULL) {
            g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
            gtk_menu_popup_at_pointer(menu, (GdkEvent *)event);
        }
        return TRUE;
    }

    if (event->button != 1U) {
        return FALSE;
    }

    box = GTK_LIST_BOX(widget);
    row = gtk_list_box_get_row_at_y(box, (gint)event->y);
    if (row == NULL) {
        return FALSE;
    }

    ctrl_pressed = (event->state & GDK_CONTROL_MASK) != 0;
    shift_pressed = (event->state & GDK_SHIFT_MASK) != 0;

    if (!ctrl_pressed && !shift_pressed) {
        ui_list_set_last_clicked_row(box, row);
        if (box == app->list_boxes[SECTION_LOCAL]) {
            app->last_clicked_local_row = row;
        }
        return FALSE;
    }

    if (shift_pressed) {
        GtkListBoxRow *last_clicked_row = ui_list_last_clicked_row(box);
        GtkListBoxRow *anchor = last_clicked_row != NULL
            ? last_clicked_row
            : row;

        ui_list_select_range(box, anchor, row, ctrl_pressed);
        ui_list_set_last_clicked_row(box, row);
        if (box == app->list_boxes[SECTION_LOCAL]) {
            app->last_clicked_local_row = row;
        }
    } else if (gtk_list_box_row_is_selected(row)) {
        gtk_list_box_unselect_row(box, row);
    } else {
        gtk_list_box_select_row(box, row);
        ui_list_set_last_clicked_row(box, row);
        if (box == app->list_boxes[SECTION_LOCAL]) {
            app->last_clicked_local_row = row;
        }
    }

    details_pane_update(app, g_object_get_data(G_OBJECT(row), "module-info"));
    if (box == app->list_boxes[SECTION_LOCAL]) {
        on_local_selection_changed(box, app);
    }
    return TRUE;
}

static void on_btn_delete_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *section_id;
    GList *selected_rows;
    guint selected_count;
    gchar **names;
    guint i;
    UiAsyncContext *ctx;

    (void)button;

    if (app == NULL || app->list_boxes[SECTION_LOCAL] == NULL) {
        return;
    }

    if (!app_state_can_run_backend(app)) {
        return;
    }

    section_id = ui_current_section_id(app);
    if (g_strcmp0(section_id, "local") != 0) {
        return;
    }

    selected_rows = gtk_list_box_get_selected_rows(app->list_boxes[SECTION_LOCAL]);
    if (ui_local_delete_state(app, selected_rows) != UI_LOCAL_DELETE_READY) {
        g_list_free(selected_rows);
        return;
    }

    selected_count = (guint)g_list_length(selected_rows);
    names = g_new0(gchar *, selected_count + 1U);
    i = 0U;

    for (GList *iter = selected_rows; iter != NULL; iter = iter->next) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(iter->data);
        ModuleInfo *module = g_object_get_data(G_OBJECT(row), "module-info");

        if (module != NULL && module->name != NULL && module->name[0] != '\0') {
            names[i++] = g_strdup(module->name);
        }
    }
    names[i] = NULL;
    g_list_free(selected_rows);

    if (i == 0U) {
        g_strfreev(names);
        return;
    }

    {
        char *message = i == 1U
            ? g_strdup_printf(_("Removing %u module…"), i)
            : g_strdup_printf(_("Removing %u modules…"), i);
        gchar *markup = ui_markup_from_text(message);
        app_state_show_info(app, GTK_MESSAGE_INFO, markup, NULL, NULL, TRUE);
        g_free(markup);
        g_free(message);
    }

    app_state_start_op(app);
    ctx = ui_async_context_new(app);
    if (ctx == NULL) {
        app_state_show_info(app, GTK_MESSAGE_WARNING, _("Failed to prepare removal."), NULL, NULL, FALSE);
        g_strfreev(names);
        return;
    }

    backend_remove_local_async(NULL,
                               (const char *const *)names,
                               app->current_op,
                               on_remove_local_done,
                               ctx);
    g_strfreev(names);
}

static void on_list_row_selected(GtkListBox *list_box, GtkListBoxRow *row, AppState *app)
{
    const ModuleInfo *module = NULL;

    if (app != NULL && list_box == app->list_boxes[SECTION_LOCAL]) {
        if (row != NULL) {
            app->last_clicked_local_row = row;
        } else {
            GList *selected_rows = gtk_list_box_get_selected_rows(list_box);

            if (selected_rows != NULL) {
                app->last_clicked_local_row = GTK_LIST_BOX_ROW(selected_rows->data);
            } else {
                app->last_clicked_local_row = NULL;
            }
            g_list_free(selected_rows);
            row = app->last_clicked_local_row;
        }
    }

    details_pane_bind_for_list(app, list_box);
    if (row != NULL) {
        ui_apply_row_erofs_availability(app, row, FALSE);
        module = g_object_get_data(G_OBJECT(row), "module-info");
    }

    details_pane_update(app, module);
    if (module != NULL && module->has_update) {
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            _("An update is available for the selected module."),
                            _("_Refresh"),
                            G_CALLBACK(on_info_action_refresh),
                            FALSE);
    }
    ui_set_toolbar_state(app, ui_current_section_id(app));
}

static void on_list_row_activated(GtkListBox *list_box, GtkListBoxRow *row, AppState *app)
{
    const char *section_id;

    if (app == NULL || row == NULL) {
        return;
    }

    gtk_list_box_select_row(list_box, row);
    section_id = ui_current_section_id(app);

    if (g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) {
        on_action_clicked_impl(app, TRUE);
        return;
    }

    if (g_strcmp0(section_id, "inet") == 0 || g_strcmp0(section_id, "local") == 0) {
        on_action_clicked_impl(app, FALSE);
    }
}

static void on_stack_child_changed(GObject *stack, GParamSpec *pspec, AppState *app)
{
    gint index;

    (void)stack;
    (void)pspec;

    ui_sync_tab_buttons(app);
    if (!(app != NULL &&
          app->initial_query != NULL &&
          app->initial_query[0] != '\0' &&
          g_strcmp0(ui_current_section_id(app), "inet") == 0)) {
        ui_reset_search_for_section_change(app);
    }
    app_state_load_current_section(app);

    if (app != NULL) {
        if (app_state_uses_overlay_session_model(app)) {
            app_state_show_info(app,
                                GTK_MESSAGE_INFO,
                                _("OverlayFS: attached modules are visible only to new processes."),
                                NULL,
                                NULL,
                                TRUE);
        }

        ui_refresh_local_erofs_markers(app);
    }

    index = section_index_from_id(ui_current_section_id(app));
    ui_sync_sort_combo_for_section(app, ui_current_section_id(app));
    if (index >= 0 && index < SECTION_COUNT) {
        details_pane_bind_for_list(app, app->list_boxes[index]);
        details_pane_update(app, NULL);
        ui_invalidate_section_filter(app, ui_current_section_id(app));

        if (app->stack != NULL) {
            GtkWidget *visible_child = gtk_stack_get_visible_child(app->stack);
            UiLayoutState *layout_state = ui_layout_state_get(app);

            if (layout_state != NULL && GTK_IS_PANED(visible_child)) {
                ui_layout_apply_position_to_paned(layout_state,
                                                  GTK_PANED(visible_child),
                                                  gtk_widget_get_allocated_width(visible_child),
                                                  FALSE);
            }
        }
    }
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)widget;

    if (app == NULL) {
        return;
    }

    if (app->search_debounce_id != 0) {
        g_source_remove(app->search_debounce_id);
        app->search_debounce_id = 0;
    }

    app_state_clear_info_timer(app);

    g_free(app->pending_query);
    app->pending_query = NULL;

    ui_inet_cache_clear(app);
    app->inet_cache_force_refresh = FALSE;
    app_state_cancel_cancellable(&app->inet_prefetch_op);
    app_state_cancel_cancellable(&app->details_file_info_op);

    app_state_cancel_cancellable(&app->current_op);
}

static void on_refresh_sync_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GError *error = NULL;

    (void)source_object;

    if (app == NULL) {
        return;
    }

    if (!backend_sync_db_finish(NULL, result, &error)) {
        if (error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            char *message = ui_strip_ansi_sequences(error->message);
            char *markup = ui_markup_from_text(message);
            app_state_show_info(app, GTK_MESSAGE_WARNING, markup, NULL, NULL, FALSE);
            g_free(markup);
            g_free(message);
        }
        g_clear_error(&error);
        return;
    }

    app->inet_cache_force_refresh = TRUE;
    app_state_load_current_section(app);
}

static void on_toolbar_refresh_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    const char *section_id;

    (void)button;

    if (app == NULL) {
        return;
    }

    section_id = ui_current_section_id(app);

    /* Internet tab: sync local index with mirrors via `modman -Sy` (pkexec),
     * then re-render from fresh db.txt. Other tabs only need a re-read. */
    if (g_strcmp0(section_id, "inet") == 0) {
        if (!app_state_can_run_backend(app)) {
            app_state_show_info(app,
                                GTK_MESSAGE_ERROR,
                                _("modman was not found: catalog refresh is unavailable."),
                                NULL, NULL, FALSE);
            return;
        }
        app_state_show_info(app,
                            GTK_MESSAGE_INFO,
                            _("Syncing online catalog…"),
                            NULL, NULL, TRUE);
        app->inet_auto_sync_attempted = FALSE;
        app_state_start_op(app);
        backend_sync_db_async(NULL, app->current_op, on_refresh_sync_done, app);
        return;
    }

    app_state_show_info(app,
                        GTK_MESSAGE_INFO,
                        _("Refreshing current tab…"),
                        NULL, NULL, TRUE);
    app->inet_cache_force_refresh = TRUE;
    app_state_load_current_section(app);
}

static void on_toolbar_load_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    on_action_clicked_impl((AppState *)user_data, FALSE);
}

static void on_toolbar_file_load_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GtkWidget *dialog;
    GtkFileFilter *filter;
    gint response;

    (void)button;

    if (app == NULL || app->window == NULL) {
        return;
    }

    dialog = gtk_file_chooser_dialog_new(_("Open local module"),
                                          GTK_WINDOW(app->window),
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          _("_Cancel"),
                                          GTK_RESPONSE_CANCEL,
                                          _("_Open"),
                                          GTK_RESPONSE_ACCEPT,
                                          NULL);
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("PFS modules (*.pfs)"));
    gtk_file_filter_add_pattern(filter, "*.pfs");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (filename != NULL) {
            ui_open_file_with_modman_open(app, filename);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

static gboolean ui_open_file_with_modman_open(AppState *app, const char *filename)
{
    const char *open_bin;
    const char *argv[3];

    if (app == NULL || filename == NULL || filename[0] == '\0') {
        return FALSE;
    }

    open_bin = g_getenv("MODMAN_OPEN_BIN");
    if (open_bin == NULL || open_bin[0] == '\0') {
        open_bin = UI_MODMAN_OPEN_FALLBACK;
    }

    argv[0] = open_bin;
    argv[1] = filename;
    argv[2] = NULL;
    return ui_launch_local_opener_argv(app, argv);
}

static char *ui_selected_local_file_path_dup(AppState *app, gboolean require_exists)
{
    ModuleInfo *module;

    if (app == NULL || g_strcmp0(ui_current_section_id(app), "local") != 0) {
        return NULL;
    }

    module = app->selected_module;
    if (module == NULL || module->path == NULL || module->path[0] == '\0') {
        return NULL;
    }

    if (!g_path_is_absolute(module->path) || !g_str_has_suffix(module->path, ".pfs")) {
        return NULL;
    }

    if (require_exists && !g_file_test(module->path, G_FILE_TEST_EXISTS)) {
        return NULL;
    }

    return g_strdup(module->path);
}

static char *ui_downloaded_module_path_dup(AppState *app, const char *module_name)
{
    GPtrArray *items;
    char *path;

    if (app == NULL || module_name == NULL || module_name[0] == '\0') {
        return NULL;
    }

    items = backend_list_local_sync(NULL);
    if (items != NULL) {
        char *expected_pfs_name = !g_str_has_suffix(module_name, ".pfs")
            ? g_strdup_printf("%s.pfs", module_name)
            : NULL;
        char *contains_match_path = NULL;
        guint contains_match_count = 0;

        for (guint i = 0; i < items->len; i++) {
            ModuleInfo *module = g_ptr_array_index(items, i);
            char *basename;
            gboolean matches;

            if (module == NULL || module->path == NULL || module->path[0] == '\0') {
                continue;
            }

            if (!g_path_is_absolute(module->path)) {
                continue;
            }

            basename = g_path_get_basename(module->path);
            matches = g_strcmp0(module->name, module_name) == 0 ||
                      g_strcmp0(basename, module_name) == 0 ||
                      (expected_pfs_name != NULL &&
                       (g_strcmp0(module->name, expected_pfs_name) == 0 ||
                        g_strcmp0(basename, expected_pfs_name) == 0));

            if (matches) {
                path = g_strdup(module->path);
                g_free(basename);
                g_free(expected_pfs_name);
                g_ptr_array_unref(items);
                return path;
            }

            if (strstr(basename, module_name) != NULL ||
                (module->name != NULL && strstr(module->name, module_name) != NULL)) {
                contains_match_count++;
                g_free(contains_match_path);
                contains_match_path = g_strdup(module->path);
            }
            g_free(basename);
        }

        if (contains_match_count == 1U && contains_match_path != NULL) {
            g_free(expected_pfs_name);
            g_ptr_array_unref(items);
            return contains_match_path;
        }

        g_free(contains_match_path);
        g_free(expected_pfs_name);
        g_ptr_array_unref(items);
    }
    return NULL;
}

static gboolean ui_launch_local_opener_argv(AppState *app, const char *const *argv)
{
    GSubprocess *proc;
    GError *error = NULL;
    UiOpenProcessContext *ctx;

    if (app == NULL || argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        return FALSE;
    }

    proc = g_subprocess_newv(argv,
                             G_SUBPROCESS_FLAGS_NONE,
                             &error);
    if (proc == NULL) {
        app_state_set_operation_status(app,
                                       UI_OPERATION_STATUS_ERROR,
                                       error != NULL && error->message != NULL
                                           ? error->message
                                           : _("unknown error"));
        g_clear_error(&error);
        return FALSE;
    }

    ctx = g_new0(UiOpenProcessContext, 1);
    ctx->app = app;
    app_state_set_operation_status(app, UI_OPERATION_STATUS_OPENING, NULL);
    g_subprocess_wait_async(proc, NULL, on_local_opener_done, ctx);
    return TRUE;
}

static void on_local_opener_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GSubprocess *proc = G_SUBPROCESS(source_object);
    UiOpenProcessContext *ctx = (UiOpenProcessContext *)user_data;
    GError *error = NULL;

    if (!g_subprocess_wait_finish(proc, result, &error)) {
        if (ctx != NULL && ctx->app != NULL) {
            app_state_set_operation_status(ctx->app,
                                           UI_OPERATION_STATUS_ERROR,
                                           error != NULL && error->message != NULL
                                               ? error->message
                                               : _("unknown error"));
        }
        g_clear_error(&error);
        g_object_unref(proc);
        g_free(ctx);
        return;
    }

    if (ctx != NULL && ctx->app != NULL) {
        if (g_subprocess_get_successful(proc)) {
            app_state_set_operation_status(ctx->app, UI_OPERATION_STATUS_READY, NULL);
        } else {
            char *message = g_strdup_printf(_("process exited with status %d"),
                                            g_subprocess_get_exit_status(proc));
            app_state_set_operation_status(ctx->app, UI_OPERATION_STATUS_ERROR, message);
            g_free(message);
        }
    }

    g_object_unref(proc);
    g_free(ctx);
}

static void on_toolbar_open_local_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    char *path;

    (void)button;

    path = ui_selected_local_file_path_dup(app, FALSE);
    if (path == NULL) {
        return;
    }

    ui_open_file_with_modman_open(app, path);
    g_free(path);
}

static void on_toolbar_reveal_local_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    char *path;
    char *directory;
    const char *argv[3];

    (void)button;

    path = ui_selected_local_file_path_dup(app, TRUE);
    if (path == NULL) {
        return;
    }

    directory = g_path_get_dirname(path);
    argv[0] = "xdg-open";
    argv[1] = directory;
    argv[2] = NULL;
    ui_launch_local_opener_argv(app, argv);
    g_free(directory);
    g_free(path);
}

static void on_sort_combo_changed(GtkComboBoxText *combo, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    gint active;

    if (app == NULL || combo == NULL) {
        return;
    }

    active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    if (active < UI_SORT_NAME || active > UI_SORT_LAYER) {
        active = UI_SORT_NAME;
    }
    app->sort_mode = active;

    for (guint i = 0; i < SECTION_COUNT; i++) {
        if (app->list_boxes[i] != NULL) {
            gtk_list_box_invalidate_sort(app->list_boxes[i]);
        }
    }
}

static void ui_refresh_all_rows(AppState *app)
{
    guint i;

    if (app == NULL) {
        return;
    }

    for (i = 0; i < SECTION_COUNT; i++) {
        GtkListBox *list_box = app->list_boxes[i];
        GList *children;
        GList *iter;
        GPtrArray *modules;
        GPtrArray *section_ids;
        guint j;

        if (list_box == NULL) {
            continue;
        }

        children = gtk_container_get_children(GTK_CONTAINER(list_box));
        modules = g_ptr_array_new();
        section_ids = g_ptr_array_new_with_free_func(g_free);

        for (iter = children; iter != NULL; iter = iter->next) {
            GtkWidget *child = GTK_WIDGET(iter->data);
            ModuleInfo *module = g_object_steal_data(G_OBJECT(child), "module-info");
            char *sec = g_object_steal_data(G_OBJECT(child), "section-id");

            if (module != NULL) {
                g_ptr_array_add(modules, module);
                g_ptr_array_add(section_ids, sec != NULL ? sec : g_strdup(""));
            } else {
                g_free(sec);
            }
        }
        g_list_free(children);

        children = gtk_container_get_children(GTK_CONTAINER(list_box));
        for (iter = children; iter != NULL; iter = iter->next) {
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        }
        g_list_free(children);

        for (j = 0; j < modules->len; j++) {
            ModuleInfo *module = (ModuleInfo *)g_ptr_array_index(modules, j);
            const char *sec = (const char *)g_ptr_array_index(section_ids, j);
            GtkListBoxRow *new_row = ui_module_to_row(app, sec, module);

            if (new_row != NULL) {
                gtk_list_box_insert(list_box, GTK_WIDGET(new_row), -1);
            } else {
                ui_module_info_free(module);
            }
        }

        g_ptr_array_free(section_ids, TRUE);
        g_ptr_array_free(modules, FALSE);

        gtk_widget_show_all(GTK_WIDGET(list_box));
        gtk_list_box_invalidate_sort(list_box);
        ui_invalidate_section_filter(app, SECTION_SPECS[i].id);
    }
}

static void ui_sync_sort_combo_for_section(AppState *app, const char *section_id)
{
    gboolean show_layer;
    guint i;

    if (app == NULL || app->sort_combo == NULL) {
        return;
    }
    show_layer = (g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0);

    g_signal_handlers_block_by_func(app->sort_combo, G_CALLBACK(on_sort_combo_changed), app);
    gtk_combo_box_text_remove_all(app->sort_combo);
    gtk_combo_box_text_append_text(app->sort_combo, _("Name"));
    gtk_combo_box_text_append_text(app->sort_combo, _("Size"));
    if (show_layer) {
        gtk_combo_box_text_append_text(app->sort_combo, _("Layer"));
    }
    if (!show_layer && app->sort_mode == UI_SORT_LAYER) {
        app->sort_mode = UI_SORT_NAME;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->sort_combo), app->sort_mode);
    g_signal_handlers_unblock_by_func(app->sort_combo, G_CALLBACK(on_sort_combo_changed), app);

    for (i = 0; i < SECTION_COUNT; i++) {
        if (app->list_boxes[i] != NULL) {
            gtk_list_box_invalidate_sort(app->list_boxes[i]);
        }
    }
}

static void on_toolbar_unload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    on_action_clicked_impl((AppState *)user_data, TRUE);
}

static void on_toolbar_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    (void)entry;
    on_search_changed_impl((AppState *)user_data);
}

static void ui_focus_search_entry(AppState *app)
{
    if (app == NULL || app->search_entry == NULL) {
        return;
    }

    gtk_widget_grab_focus(GTK_WIDGET(app->search_entry));
    gtk_editable_select_region(GTK_EDITABLE(app->search_entry), 0, -1);
}

static void ui_show_about_dialog(AppState *app)
{
    static const gchar *authors[] = {
        "PuppyRus modman contributors",
        NULL,
    };
    const char *comments =
        _("GTK3 module manager interface for frugal systems.\n\n"
          "Keyboard shortcuts:\n"
          "F5 — refresh current section\n"
          "Ctrl+F — focus search\n"
          "Ctrl+Return — perform the primary action\n"
          "Delete — detach module or remove local file\n"
          "Ctrl+I — open this window\n"
          "Ctrl+Q — close window\n"
          "Esc — clear search or close dialog\n"
          "Alt+1..4 — switch sections");
    const char *icon_name;

    if (app == NULL || app->window == NULL) {
        return;
    }

    icon_name = ui_icon_name_or_fallback(UI_APP_ICON_NAME, UI_APP_ICON_FALLBACK);

    gtk_show_about_dialog(GTK_WINDOW(app->window),
                          "program-name", "modman",
                          "logo-icon-name", icon_name,
                          "comments", comments,
                          "authors", authors,
                          NULL);
}

static void ui_switch_to_section(AppState *app, gint section_index)
{
    if (app == NULL || app->stack == NULL) {
        return;
    }

    if (section_index < 0 || section_index >= SECTION_COUNT) {
        return;
    }

    gtk_stack_set_visible_child_name(app->stack, SECTION_SPECS[section_index].id);
}

static void ui_reset_search_for_section_change(AppState *app)
{
    if (app == NULL || app->search_entry == NULL) {
        return;
    }

    if (app->search_debounce_id != 0) {
        g_source_remove(app->search_debounce_id);
        app->search_debounce_id = 0;
    }

    g_signal_handlers_block_by_func(app->search_entry, G_CALLBACK(on_toolbar_search_changed), app);
    gtk_entry_set_text(GTK_ENTRY(app->search_entry), "");
    g_signal_handlers_unblock_by_func(app->search_entry, G_CALLBACK(on_toolbar_search_changed), app);

    g_free(app->pending_query);
    app->pending_query = NULL;
}

static gboolean ui_handle_primary_shortcut(AppState *app)
{
    const char *section_id;

    if (app == NULL) {
        return FALSE;
    }

    section_id = ui_current_section_id(app);
    if (g_strcmp0(section_id, "loaded") == 0 || g_strcmp0(section_id, "system") == 0) {
        on_action_clicked_impl(app, TRUE);
        return TRUE;
    }

    if (g_strcmp0(section_id, "inet") == 0 || g_strcmp0(section_id, "local") == 0) {
        on_action_clicked_impl(app, FALSE);
        return TRUE;
    }

    return FALSE;
}

static gboolean ui_handle_unload_shortcut(AppState *app)
{
    const char *section_id;

    if (app == NULL) {
        return FALSE;
    }

    section_id = ui_current_section_id(app);
    if (g_strcmp0(section_id, "loaded") != 0 && g_strcmp0(section_id, "system") != 0) {
        return FALSE;
    }

    on_action_clicked_impl(app, TRUE);
    return TRUE;
}

static gboolean ui_handle_escape_shortcut(AppState *app)
{
    const char *query;

    if (app == NULL || app->search_entry == NULL) {
        return FALSE;
    }

    if (app->current_op != NULL) {
        app_state_cancel_current_op(app, TRUE);
        return TRUE;
    }

    if (!gtk_widget_has_focus(GTK_WIDGET(app->search_entry))) {
        return FALSE;
    }

    query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
    if (query == NULL || query[0] == '\0') {
        return FALSE;
    }

    gtk_entry_set_text(GTK_ENTRY(app->search_entry), "");
    gtk_widget_grab_focus(GTK_WIDGET(app->search_entry));
    return TRUE;
}

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GdkModifierType state;

    (void)widget;

    if (app == NULL || event == NULL) {
        return FALSE;
    }

    state = event->state & gtk_accelerator_get_default_mod_mask();

    if (state == GDK_MOD1_MASK) {
        switch (event->keyval) {
        case GDK_KEY_1:
        case GDK_KEY_KP_1:
            ui_switch_to_section(app, SECTION_LOADED);
            return TRUE;
        case GDK_KEY_2:
        case GDK_KEY_KP_2:
            ui_switch_to_section(app, SECTION_INET);
            return TRUE;
        case GDK_KEY_3:
        case GDK_KEY_KP_3:
            ui_switch_to_section(app, SECTION_LOCAL);
            return TRUE;
        case GDK_KEY_4:
        case GDK_KEY_KP_4:
            ui_switch_to_section(app, SECTION_SYSTEM);
            return TRUE;
        default:
            break;
        }
    }

    if (state == GDK_CONTROL_MASK) {
        switch (event->keyval) {
        case GDK_KEY_f:
        case GDK_KEY_F:
            ui_focus_search_entry(app);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
            return ui_handle_primary_shortcut(app);
        default:
            break;
        }
    }

    switch (event->keyval) {
    case GDK_KEY_Delete:
        if (g_strcmp0(ui_current_section_id(app), "local") == 0) {
            on_btn_delete_clicked(NULL, app);
            return TRUE;
        }
        return ui_handle_unload_shortcut(app);
    case GDK_KEY_Escape:
        return ui_handle_escape_shortcut(app);
    default:
        return FALSE;
    }
}

static void on_info_bar_response(GtkInfoBar *info_bar, gint response_id, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    if (response_id == GTK_RESPONSE_APPLY && app != NULL && app->info_action_cb != NULL) {
        void (*action_cb)(GtkWidget *, gpointer) = (void (*)(GtkWidget *, gpointer))app->info_action_cb;
        action_cb(GTK_WIDGET(info_bar), app);
        return;
    }

    app_state_hide_info(app);
}

static void on_toolbar_about_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ui_show_about_dialog((AppState *)user_data);
}

static void on_config_open_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *error = NULL;

    if (!g_subprocess_wait_finish(proc, result, &error)) {
        if (app != NULL) {
            char *message = ui_strip_ansi_sequences(error != NULL ? error->message : NULL);
            if (app->status_label != NULL) {
                gtk_label_set_text(app->status_label, _("Failed to open /etc/modman.conf."));
            }
            app_state_show_info(app,
                                GTK_MESSAGE_ERROR,
                                message != NULL && message[0] != '\0'
                                    ? message
                                    : _("Failed to open /etc/modman.conf."),
                                NULL,
                                NULL,
                                FALSE);
            g_free(message);
        }
        g_clear_error(&error);
        g_object_unref(proc);
        return;
    }

    if (app != NULL && app->status_label != NULL) {
        if (g_subprocess_get_successful(proc)) {
            app_state_set_operation_status(app, UI_OPERATION_STATUS_READY, NULL);
        } else {
            gtk_label_set_text(app->status_label, _("Failed to open /etc/modman.conf."));
        }
    }

    g_object_unref(proc);
}

static void on_toolbar_config_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    GError *error = NULL;
    GSubprocess *proc;
    char *display_arg = NULL;
    char *xauth_arg = NULL;
    char *wayland_arg = NULL;
    char *runtime_arg = NULL;
    char *session_arg = NULL;
    char *data_dirs_arg = NULL;
    const char *display = g_getenv("DISPLAY");
    const char *xauth = g_getenv("XAUTHORITY");
    const char *wayland = g_getenv("WAYLAND_DISPLAY");
    const char *runtime = g_getenv("XDG_RUNTIME_DIR");
    const char *session = g_getenv("XDG_SESSION_TYPE");
    const char *data_dirs = g_getenv("XDG_DATA_DIRS");

    (void)button;

    display_arg = g_strdup_printf("DISPLAY=%s", display != NULL ? display : "");
    xauth_arg = g_strdup_printf("XAUTHORITY=%s", xauth != NULL ? xauth : "");
    wayland_arg = g_strdup_printf("WAYLAND_DISPLAY=%s", wayland != NULL ? wayland : "");
    runtime_arg = g_strdup_printf("XDG_RUNTIME_DIR=%s", runtime != NULL ? runtime : "");
    session_arg = g_strdup_printf("XDG_SESSION_TYPE=%s", session != NULL ? session : "");
    data_dirs_arg = g_strdup_printf("XDG_DATA_DIRS=%s",
                                    data_dirs != NULL ? data_dirs : "/usr/local/share:/usr/share");

    {
        const char *argv[] = {
            "pkexec",
            "env",
            display_arg,
            xauth_arg,
            wayland_arg,
            runtime_arg,
            session_arg,
            data_dirs_arg,
            "xdg-open",
            "/etc/modman.conf",
            NULL
        };

        proc = g_subprocess_newv(argv,
                                 G_SUBPROCESS_FLAGS_STDIN_INHERIT,
                                 &error);
    }

    g_free(display_arg);
    g_free(xauth_arg);
    g_free(wayland_arg);
    g_free(runtime_arg);
    g_free(session_arg);
    g_free(data_dirs_arg);

    if (proc == NULL) {
        char *message = ui_strip_ansi_sequences(error != NULL ? error->message : NULL);
        app_state_show_info(app,
                            GTK_MESSAGE_ERROR,
                            message != NULL && message[0] != '\0'
                                ? message
                                : _("Failed to start pkexec to open /etc/modman.conf."),
                            NULL, NULL, FALSE);
        g_clear_error(&error);
        g_free(message);
        return;
    }

    g_subprocess_wait_async(proc, NULL, on_config_open_done, app);

    if (app != NULL && app->status_label != NULL) {
        app_state_set_operation_status(app, UI_OPERATION_STATUS_READY, NULL);
    }
}

static void on_btn_exit_clicked(GtkButton *button, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    (void)button;

    if (app == NULL || app->window == NULL) {
        return;
    }

    gtk_window_close(GTK_WINDOW(app->window));
}

GtkWidget *ui_build_section_page(AppState *app, const char *section_id)
{
    const SectionSpec *section = section_spec_from_id(section_id);
    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *list = gtk_list_box_new();
    gint index;

    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroller), list);

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_MULTIPLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), FALSE);
    gtk_widget_add_events(list, GDK_BUTTON_PRESS_MASK);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(list), ui_filter_row_for_current_section, app, NULL);
    gtk_list_box_set_sort_func(GTK_LIST_BOX(list), compare_status_priority, app, NULL);

    {
        const char *placeholder_text = section != NULL ? _(section->empty_text) : _("Section is empty.");
        if (!app_state_can_run_backend(app)) {
            placeholder_text = _("Install modman to work with modules.");
        }
        GtkWidget *placeholder = gtk_label_new(placeholder_text);
        gtk_widget_set_margin_top(placeholder, 24);
        gtk_widget_set_margin_bottom(placeholder, 24);
        gtk_widget_set_margin_start(placeholder, 24);
        gtk_widget_set_margin_end(placeholder, 24);
        gtk_label_set_line_wrap(GTK_LABEL(placeholder), TRUE);
        gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0f);
        g_object_set_data(G_OBJECT(list), "empty-placeholder", placeholder);
        gtk_list_box_set_placeholder(GTK_LIST_BOX(list), placeholder);
    }

    g_object_set_data(G_OBJECT(list), "details-name", app->details.name);
    g_object_set_data(G_OBJECT(list), "details-status-icon", app->details.status_icon);
    g_object_set_data(G_OBJECT(list), "details-status", app->details.status);
    g_object_set_data(G_OBJECT(list), "details-version", app->details.version);
    g_object_set_data(G_OBJECT(list), "details-size", app->details.size);
    g_object_set_data(G_OBJECT(list), "details-path", app->details.path);
    g_object_set_data(G_OBJECT(list), "details-repo", app->details.repo);
    g_object_set_data(G_OBJECT(list), "details-description", app->details.description);
    g_object_set_data(G_OBJECT(list), "details-depends", app->details.depends);
    g_object_set_data(G_OBJECT(list), "details-modules", app->details.modules);

    g_signal_connect(list, "row-selected", G_CALLBACK(on_list_row_selected), app);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_list_row_activated), app);
    g_signal_connect(list,
                     "button-press-event",
                     G_CALLBACK(on_module_list_button_press),
                     app);
    g_signal_connect(list, "add", G_CALLBACK(on_section_list_children_changed), app);
    g_signal_connect(list, "remove", G_CALLBACK(on_section_list_children_changed), app);

    index = section_index_from_id(section_id);
    if (index >= 0 && index < SECTION_COUNT) {
        app->list_boxes[index] = GTK_LIST_BOX(list);
    }

    g_object_set_data_full(G_OBJECT(scroller), "section-id", g_strdup(section_id), g_free);
    ui_set_section_sidebar_name(app, scroller, section);

    return scroller;
}

static void on_compact_menu_toggled(GtkCheckMenuItem *item, gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    if (app == NULL) {
        return;
    }
    app->compact_mode = gtk_check_menu_item_get_active(item);
    ui_refresh_all_rows(app);
}

static void on_file_load_menu_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    on_toolbar_file_load_clicked(NULL, user_data);
}

static void on_about_menu_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    on_toolbar_about_clicked(NULL, user_data);
}

static void on_config_menu_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    on_toolbar_config_clicked(NULL, user_data);
}

static void on_clear_old_menu_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    on_clear_clicked(NULL, user_data);
}

/* ── Update dialog ──────────────────────────────────────────────────────── */

/* Column indices for the update GtkTreeView store */
enum {
    UPDATE_COL_SELECTED = 0,   /* gboolean — checkbox */
    UPDATE_COL_NAME,           /* gchar*   — installed name */
    UPDATE_COL_PATH,           /* gchar*   — installed path */
    UPDATE_COL_OLD_VERSION,    /* gchar*   — current version */
    UPDATE_COL_NEW_VERSION,    /* gchar*   — new version */
    UPDATE_COL_REPO,           /* gchar*   — repo */
    UPDATE_COL_RISK,           /* gchar*   — risk / message */
    UPDATE_COL_GROUP,          /* gchar*   — "normal" | "loaded" | "system" */
    UPDATE_COL_ID,             /* gchar*   — update id for backend calls */
    UPDATE_COL_REBOOT,         /* gboolean — reboot_required */
    UPDATE_NUM_COLS
};

typedef struct {
    AppState *app;
    GtkListStore *store;
    GtkTreeView *tree_view;
    GtkDialog *dialog;
    GtkLabel *status_label;
    GtkProgressBar *progress_bar;
    GCancellable *cancellable;
    gboolean async_completed; /* TRUE once on_check_updates_done has fired */
} UpdateDialogState;

static void update_dialog_state_free(UpdateDialogState *s)
{
    if (s == NULL) {
        return;
    }
    if (s->cancellable != NULL) {
        g_cancellable_cancel(s->cancellable);
        g_object_unref(s->cancellable);
        s->cancellable = NULL;
    }
    g_free(s);
}

static void update_dialog_status_label_set(GtkLabel *label, GtkMessageType type, const char *message)
{
    const char *prefix = "";
    gchar *text;

    if (label == NULL) {
        return;
    }

    switch (type) {
    case GTK_MESSAGE_ERROR:
        prefix = _("Ошибка: ");
        break;
    case GTK_MESSAGE_WARNING:
        prefix = _("Внимание: ");
        break;
    default:
        prefix = "";
        break;
    }

    text = g_strdup_printf("%s%s", prefix, message != NULL ? message : "");
    gtk_label_set_text(label, text);
    gtk_widget_show(GTK_WIDGET(label));
    g_free(text);
}

/* Called when the checkbox cell is toggled */
static void on_update_row_toggled(GtkCellRendererToggle *renderer,
                                   gchar *path_str,
                                   gpointer user_data)
{
    UpdateDialogState *s = (UpdateDialogState *)user_data;
    GtkTreeIter iter;
    gboolean current;

    (void)renderer;

    if (s == NULL || s->store == NULL) {
        return;
    }

    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(s->store), &iter, path_str)) {
        return;
    }

    gtk_tree_model_get(GTK_TREE_MODEL(s->store), &iter,
                       UPDATE_COL_SELECTED, &current,
                       -1);
    gtk_list_store_set(s->store, &iter,
                       UPDATE_COL_SELECTED, !current,
                       -1);
}

/* Returns TRUE if any row with group=="system" is currently checked */
static gboolean update_store_has_system_selected(GtkListStore *store)
{
    GtkTreeIter iter;
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        gboolean selected;
        gchar *group = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           UPDATE_COL_SELECTED, &selected,
                           UPDATE_COL_GROUP, &group,
                           -1);
        if (selected && g_strcmp0(group, "system") == 0) {
            g_free(group);
            return TRUE;
        }
        g_free(group);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    return FALSE;
}

/* Collect IDs of all checked rows */
static GPtrArray *update_store_collect_selected_ids(GtkListStore *store)
{
    GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
    GtkTreeIter iter;
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        gboolean selected;
        gchar *id = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           UPDATE_COL_SELECTED, &selected,
                           UPDATE_COL_ID, &id,
                           -1);
        if (selected && id != NULL && id[0] != '\0') {
            g_ptr_array_add(ids, id); /* ownership transferred */
        } else {
            g_free(id);
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    return ids;
}

static gboolean update_store_all_rows_selected(GtkListStore *store)
{
    GtkTreeIter iter;
    gboolean valid;
    gboolean has_rows = FALSE;

    if (store == NULL) {
        return FALSE;
    }

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        gboolean selected = FALSE;

        has_rows = TRUE;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           UPDATE_COL_SELECTED, &selected,
                           -1);
        if (!selected) {
            return FALSE;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }

    return has_rows;
}

static void update_store_set_all_selected(GtkListStore *store, gboolean selected)
{
    GtkTreeIter iter;
    gboolean valid;

    if (store == NULL) {
        return;
    }

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        gtk_list_store_set(store, &iter,
                           UPDATE_COL_SELECTED, selected,
                           -1);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
}

/* Remove the row whose UPDATE_COL_ID matches id */
static void update_store_remove_by_id(GtkListStore *store, const char *id)
{
    GtkTreeIter iter;
    gboolean valid;

    if (id == NULL) {
        return;
    }

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        gchar *row_id = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           UPDATE_COL_ID, &row_id,
                           -1);
        if (g_strcmp0(row_id, id) == 0) {
            g_free(row_id);
            gtk_list_store_remove(store, &iter);
            return;
        }
        g_free(row_id);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
}

typedef struct {
    char *identity;
    char *version;
} UpdateBlacklistEntry;

static void update_blacklist_entry_free(gpointer data)
{
    UpdateBlacklistEntry *entry = (UpdateBlacklistEntry *)data;

    if (entry == NULL) {
        return;
    }

    g_free(entry->identity);
    g_free(entry->version);
    g_free(entry);
}

static GPtrArray *ui_update_blacklist_entries_load(AppState *app, GError **error)
{
    GPtrArray *entries;
    char *modman_bin;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    gchar **lines;

    modman_bin = ui_inet_cache_effective_modman_bin_dup(app);
    const char *direct_argv[] = {
        modman_bin,
        "--machine",
        "--update-blacklist-list",
        NULL
    };
    const char *pkexec_argv[] = {
        "pkexec",
        modman_bin,
        "--machine",
        "--update-blacklist-list",
        NULL
    };
    const char *const *argv = (geteuid() == 0) ? direct_argv : pkexec_argv;

    if (!backend_call_sync(argv, &stdout_text, &stderr_text, NULL, error)) {
        g_free(stderr_text);
        g_free(stdout_text);
        g_free(modman_bin);
        return NULL;
    }

    entries = g_ptr_array_new_with_free_func(update_blacklist_entry_free);
    lines = g_strsplit(stdout_text != NULL ? stdout_text : "", "\n", -1);
    for (guint i = 0U; lines != NULL && lines[i] != NULL; i++) {
        gchar **fields;
        UpdateBlacklistEntry *entry;

        if (lines[i][0] == '\0') {
            continue;
        }

        fields = g_strsplit(lines[i], "\t", 3);
        if (fields[0] == NULL || fields[0][0] == '\0' ||
            fields[1] == NULL || fields[1][0] == '\0') {
            g_strfreev(fields);
            continue;
        }

        entry = g_new0(UpdateBlacklistEntry, 1);
        entry->identity = g_strdup(fields[0]);
        entry->version = g_strdup(fields[1]);
        g_ptr_array_add(entries, entry);
        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(stderr_text);
    g_free(stdout_text);
    g_free(modman_bin);
    return entries;
}

static gboolean ui_update_blacklist_entry_remove(AppState *app,
                                                 const UpdateBlacklistEntry *entry,
                                                 GError **error)
{
    char *modman_bin;
    gboolean ok;

    if (entry == NULL || entry->identity == NULL || entry->version == NULL) {
        g_set_error_literal(error,
                            MODMAN_GUI_ERROR_DOMAIN,
                            MODMAN_GUI_ERR_INVALID_NAME,
                            "Некорректная запись чёрного списка");
        return FALSE;
    }

    modman_bin = ui_inet_cache_effective_modman_bin_dup(app);
    const char *argv[] = {
        "pkexec",
        modman_bin,
        "--machine",
        "--update-blacklist-remove",
        entry->identity,
        entry->version,
        NULL
    };

    ok = backend_call_sync(argv, NULL, NULL, NULL, error);
    g_free(modman_bin);
    return ok;
}

/* Context passed to the blacklist async callback */
typedef struct {
    AppState *app;
    GtkListStore *store;
    GtkLabel *status_label;
    GPtrArray *ids;
    guint index;
    guint success;
    guint failed;
    GCancellable *cancellable;
} UpdateBlacklistCtx;

static void update_blacklist_ctx_free(UpdateBlacklistCtx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->cancellable != NULL) {
        g_object_unref(ctx->cancellable);
    }
    if (ctx->store != NULL) {
        g_object_unref(ctx->store);
    }
    if (ctx->status_label != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    if (ctx->ids != NULL) {
        g_ptr_array_unref(ctx->ids);
    }
    g_free(ctx);
}

static void update_blacklist_add_next(UpdateBlacklistCtx *ctx);

static void on_update_blacklist_done(GObject *source_object,
                                      GAsyncResult *result,
                                      gpointer user_data)
{
    UpdateBlacklistCtx *ctx = (UpdateBlacklistCtx *)user_data;
    GError *error = NULL;
    gboolean ok;
    const char *id;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    id = (ctx->ids != NULL && ctx->index > 0U)
        ? (const char *)g_ptr_array_index(ctx->ids, ctx->index - 1U)
        : NULL;
    ok = backend_update_blacklist_add_finish(NULL, result, &error);

    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            ctx->failed++;
        }
        g_error_free(error);
    } else if (ok) {
        if (ctx->store != NULL && id != NULL) {
            update_store_remove_by_id(ctx->store, id);
        }
        ctx->success++;
    } else {
        ctx->failed++;
    }

    update_blacklist_add_next(ctx);
}

static void update_blacklist_add_next(UpdateBlacklistCtx *ctx)
{
    const char *id;

    if (ctx == NULL || ctx->ids == NULL) {
        return;
    }

    if (ctx->index >= ctx->ids->len) {
        if (ctx->failed == 0U) {
            gchar *message = g_strdup_printf(_("Добавлено в чёрный список: %u"), ctx->success);
            update_dialog_status_label_set(ctx->status_label, GTK_MESSAGE_INFO, message);
            g_free(message);
        } else {
            gchar *message = g_strdup_printf(_("Добавлено: %u, ошибок: %u"),
                                             ctx->success,
                                             ctx->failed);
            update_dialog_status_label_set(ctx->status_label, GTK_MESSAGE_WARNING, message);
            g_free(message);
        }
        update_blacklist_ctx_free(ctx);
        return;
    }

    id = (const char *)g_ptr_array_index(ctx->ids, ctx->index);
    ctx->index++;
    backend_update_blacklist_add_async(NULL,
                                       id,
                                       ctx->cancellable,
                                       on_update_blacklist_done,
                                       ctx);
}

static void update_blacklist_add_ids(UpdateDialogState *ds, GPtrArray *ids)
{
    UpdateBlacklistCtx *ctx;

    if (ds == NULL || ids == NULL || ids->len == 0U) {
        if (ids != NULL) {
            g_ptr_array_unref(ids);
        }
        return;
    }

    ctx = g_new0(UpdateBlacklistCtx, 1);
    ctx->app = ds->app;
    ctx->store = g_object_ref(ds->store);
    ctx->status_label = ds->status_label;
    if (ctx->status_label != NULL) {
        g_object_add_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    ctx->ids = ids; /* ownership transferred */
    ctx->cancellable = g_cancellable_new();

    update_blacklist_add_next(ctx);
}

typedef struct {
    GtkListStore *store;
    GtkLabel *status_label;
    GCancellable *cancellable;
} UpdateDialogRefreshCtx;

static void update_dialog_refresh_ctx_free(UpdateDialogRefreshCtx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->store != NULL) {
        g_object_unref(ctx->store);
    }
    if (ctx->status_label != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    if (ctx->cancellable != NULL) {
        g_object_unref(ctx->cancellable);
    }
    g_free(ctx);
}

static void ui_update_populate_store(GtkListStore *store, GPtrArray *updates);

static void on_update_dialog_refresh_done(GObject *source_object,
                                           GAsyncResult *result,
                                           gpointer user_data)
{
    UpdateDialogRefreshCtx *ctx = (UpdateDialogRefreshCtx *)user_data;
    GPtrArray *updates;
    GError *error = NULL;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    updates = backend_check_updates_finish(NULL, result, &error);
    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            update_dialog_status_label_set(ctx->status_label, GTK_MESSAGE_ERROR, error->message);
        }
        g_error_free(error);
        update_dialog_refresh_ctx_free(ctx);
        return;
    }

    if (ctx->store != NULL) {
        if (updates != NULL) {
            ui_update_populate_store(ctx->store, updates);
        } else {
            gtk_list_store_clear(ctx->store);
        }
    }
    if (updates != NULL) {
        backend_update_info_array_free(updates);
    }
    update_dialog_status_label_set(ctx->status_label,
                                   GTK_MESSAGE_INFO,
                                   _("Список обновлений обновлён."));
    update_dialog_refresh_ctx_free(ctx);
}

static void update_dialog_refresh_updates(UpdateDialogState *ds)
{
    UpdateDialogRefreshCtx *ctx;

    if (ds == NULL || ds->store == NULL) {
        return;
    }

    ctx = g_new0(UpdateDialogRefreshCtx, 1);
    ctx->store = g_object_ref(ds->store);
    ctx->status_label = ds->status_label;
    if (ctx->status_label != NULL) {
        g_object_add_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    ctx->cancellable = g_cancellable_new();

    backend_check_updates_async(NULL,
                                ctx->cancellable,
                                on_update_dialog_refresh_done,
                                ctx);
}

/* Context passed to the apply-single async callback */
typedef struct {
    AppState *app;
    GtkListStore *store;
    GPtrArray *ids;          /* all IDs to apply (owned) */
    GtkLabel *status_label;
    GtkProgressBar *progress_bar;
    guint progress_timer_id;
    guint index;             /* current index */
    guint success;
    guint failed;
    gboolean confirm_system;
    GCancellable *cancellable;
} UpdateApplyCtx;

static gboolean update_apply_progress_tick(gpointer user_data)
{
    UpdateApplyCtx *ctx = (UpdateApplyCtx *)user_data;
    char *content = NULL;
    char *name = NULL;
    char *stage = NULL;
    char *pct = NULL;
    gsize len = 0;

    if (ctx == NULL || ctx->progress_bar == NULL) {
        return G_SOURCE_REMOVE;
    }

    if (ctx->app != NULL && ctx->app->progress_file_path != NULL &&
        g_file_get_contents(ctx->app->progress_file_path, &content, &len, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (guint i = 0U; lines != NULL && lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "name=")) {
                g_free(name);
                name = g_strdup(lines[i] + 5);
            } else if (g_str_has_prefix(lines[i], "stage=")) {
                g_free(stage);
                stage = g_strdup(lines[i] + 6);
            } else if (g_str_has_prefix(lines[i], "pct=")) {
                g_free(pct);
                pct = g_strdup(lines[i] + 4);
            }
        }
        g_strfreev(lines);
        g_free(content);
    }

    if (pct != NULL && pct[0] != '\0') {
        char *end = NULL;
        gdouble pct_value = g_ascii_strtod(pct, &end);

        if (end != pct) {
            gtk_progress_bar_set_fraction(ctx->progress_bar, CLAMP(pct_value / 100.0, 0.0, 1.0));
        } else {
            gtk_progress_bar_pulse(ctx->progress_bar);
        }
    } else {
        gtk_progress_bar_pulse(ctx->progress_bar);
    }

    if (name != NULL && pct != NULL) {
        const char *verb = (stage != NULL && g_strcmp0(stage, "download") == 0) ? _("Downloading") : _("Processing");
        char *message = g_strdup_printf("%s %s: %s%%", verb, name, pct);

        update_dialog_status_label_set(ctx->status_label, GTK_MESSAGE_INFO, message);
        if (ctx->app != NULL && ctx->app->status_label != NULL) {
            gtk_label_set_text(ctx->app->status_label, message);
        }
        g_free(message);
    }

    g_free(pct);
    g_free(stage);
    g_free(name);
    return G_SOURCE_CONTINUE;
}

static void update_apply_ctx_free(UpdateApplyCtx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->cancellable != NULL) {
        g_object_unref(ctx->cancellable);
    }
    if (ctx->ids != NULL) {
        g_ptr_array_unref(ctx->ids);
    }
    if (ctx->store != NULL) {
        g_object_unref(ctx->store);
    }
    if (ctx->status_label != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    if (ctx->progress_timer_id != 0U) {
        g_source_remove(ctx->progress_timer_id);
    }
    if (ctx->progress_bar != NULL) {
        gtk_progress_bar_set_fraction(ctx->progress_bar, 0.0);
        gtk_widget_hide(GTK_WIDGET(ctx->progress_bar));
        g_object_remove_weak_pointer(G_OBJECT(ctx->progress_bar), (gpointer *)&ctx->progress_bar);
    }
    app_state_progress_disarm(ctx->app);
    g_free(ctx);
}

static void update_apply_next(UpdateApplyCtx *ctx);

static void on_update_apply_one_done(GObject *source_object,
                                      GAsyncResult *result,
                                      gpointer user_data)
{
    UpdateApplyCtx *ctx = (UpdateApplyCtx *)user_data;
    GError *error = NULL;
    gboolean ok;

    (void)source_object;

    if (ctx == NULL) {
        return;
    }

    ok = backend_update_apply_finish(NULL, result, &error);

    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            ctx->failed++;
        }
        g_error_free(error);
    } else if (ok) {
        const char *id = (const char *)g_ptr_array_index(ctx->ids, ctx->index - 1);
        if (ctx->store != NULL) {
            update_store_remove_by_id(ctx->store, id);
        }
        ctx->success++;
    } else {
        ctx->failed++;
    }

    update_apply_next(ctx);
}

static void update_apply_next(UpdateApplyCtx *ctx)
{
    const char *id;

    if (ctx == NULL) {
        return;
    }

    if (ctx->index >= ctx->ids->len) {
        AppState *app = ctx->app;
        guint success = ctx->success;
        guint failed = ctx->failed;
        char *message;

        if (failed == 0) {
            message = g_strdup_printf(_("✓ Обновлено модулей: %u"), success);
        } else {
            message = g_strdup_printf(_("Обновлено: %u, ошибок: %u"), success, failed);
        }
        update_dialog_status_label_set(ctx->status_label,
                                       failed > 0 ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                                       message);
        ui_status_show_plain(app,
                             failed > 0 ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                             message,
                             TRUE);
        g_free(message);

        if (app != NULL && g_strcmp0(ui_current_section_id(app), "inet") != 0) {
            app->suppress_next_refresh_info = TRUE;
            app_state_load_current_section(app);
        }
        update_apply_ctx_free(ctx);
        return;
    }

    id = (const char *)g_ptr_array_index(ctx->ids, ctx->index);
    ctx->index++;

    backend_update_apply_async(NULL,
                               id,
                               ctx->confirm_system,
                               ctx->cancellable,
                               on_update_apply_one_done,
                               ctx);
}

/* Build a group header row (non-interactive separator label) */
static GtkWidget *ui_update_make_group_header(const char *group_name)
{
    GtkWidget *label;
    gchar *markup;

    if (g_strcmp0(group_name, "system") == 0) {
        markup = g_markup_printf_escaped("<b>%s</b>", _("Системные / base"));
    } else if (g_strcmp0(group_name, "loaded") == 0) {
        markup = g_markup_printf_escaped("<b>%s</b>", _("Подключенные"));
    } else {
        markup = g_markup_printf_escaped("<b>%s</b>", _("Обычные"));
    }

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 6);
    gtk_widget_set_margin_bottom(label, 2);
    gtk_widget_set_margin_start(label, 4);
    g_free(markup);
    return label;
}

/* Populate the list store from a GPtrArray of ModuleUpdateInfo* */
static void ui_update_populate_store(GtkListStore *store, GPtrArray *updates)
{
    guint i;

    gtk_list_store_clear(store);

    if (updates == NULL) {
        return;
    }

    for (i = 0; i < updates->len; i++) {
        ModuleUpdateInfo *u = (ModuleUpdateInfo *)g_ptr_array_index(updates, i);
        GtkTreeIter iter;
        gboolean default_checked;
        const char *group;
        gchar *risk_msg;

        if (u == NULL) {
            continue;
        }

        /* Determine group from risk field: "system" or "base" → system group */
        if (g_strcmp0(u->risk, "system") == 0 || g_strcmp0(u->risk, "base") == 0) {
            group = "system";
        } else if (u->reboot_required) {
            group = "loaded";
        } else {
            group = "normal";
        }

        /* Default selection: normal and loaded checked, system unchecked */
        default_checked = (g_strcmp0(group, "system") != 0);

        /* Risk/message display */
        if (u->message != NULL && u->message[0] != '\0') {
            risk_msg = g_strdup_printf("%s — %s",
                                       u->risk != NULL ? u->risk : "",
                                       u->message);
        } else {
            risk_msg = g_strdup(u->risk != NULL ? u->risk : "");
        }

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           UPDATE_COL_SELECTED,    default_checked,
                           UPDATE_COL_NAME,        u->name != NULL ? u->name : "",
                           UPDATE_COL_PATH,        u->installed_path != NULL ? u->installed_path : "",
                           UPDATE_COL_OLD_VERSION, u->old_version != NULL ? u->old_version : "",
                           UPDATE_COL_NEW_VERSION, u->new_version != NULL ? u->new_version : "",
                           UPDATE_COL_REPO,        u->repo != NULL ? u->repo : "",
                           UPDATE_COL_RISK,        risk_msg,
                           UPDATE_COL_GROUP,       group,
                           UPDATE_COL_ID,          u->id != NULL ? u->id : "",
                           UPDATE_COL_REBOOT,      u->reboot_required,
                           -1);
        g_free(risk_msg);
    }
}

static void on_update_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    UpdateDialogState *ds = (UpdateDialogState *)user_data;
    AppState *app;
    GPtrArray *ids;
    gboolean has_system;
    UpdateApplyCtx *ctx;
    GCancellable *cancellable;

    if (ds == NULL) {
        return;
    }

    app = ds->app;

    if (response_id == GTK_RESPONSE_APPLY) {
        gboolean select_all = !update_store_all_rows_selected(ds->store);
        update_store_set_all_selected(ds->store, select_all);
        update_dialog_status_label_set(ds->status_label,
                                       GTK_MESSAGE_INFO,
                                       select_all
                                           ? _("Выделены все обновления.")
                                           : _("Выделение снято со всех обновлений."));
        return;
    }

    if (response_id != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        ds->dialog = NULL;
        if (ds->async_completed) {
            /* Async already finished — safe to free now */
            update_dialog_state_free(ds);
        } else {
            /* Async still in flight — cancel it; on_check_updates_done will free ds */
            if (ds->cancellable != NULL) {
                g_cancellable_cancel(ds->cancellable);
            }
        }
        return;
    }

    ids = update_store_collect_selected_ids(ds->store);
    if (ids == NULL || ids->len == 0) {
        if (ids != NULL) {
            g_ptr_array_unref(ids);
        }
        update_dialog_status_label_set(ds->status_label,
                                       GTK_MESSAGE_WARNING,
                                       _("Отметьте обновления для применения."));
        return;
    }

    has_system = update_store_has_system_selected(ds->store);

    if (has_system) {
        GtkWidget *confirm = gtk_message_dialog_new(
            GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "%s",
            _("Системные/base модули будут заменены. Нужна перезагрузка. Продолжить?"));
        gint confirm_response = gtk_dialog_run(GTK_DIALOG(confirm));
        gtk_widget_destroy(confirm);

        if (confirm_response != GTK_RESPONSE_YES) {
            g_ptr_array_unref(ids);
            update_dialog_status_label_set(ds->status_label,
                                           GTK_MESSAGE_INFO,
                                           _("Обновление отменено."));
            return;
        }
    }

    cancellable = g_cancellable_new();
    ctx = g_new0(UpdateApplyCtx, 1);
    ctx->app = app;
    ctx->store = ds->store != NULL ? g_object_ref(ds->store) : NULL;
    ctx->ids = ids;
    ctx->status_label = ds->status_label;
    if (ctx->status_label != NULL) {
        g_object_add_weak_pointer(G_OBJECT(ctx->status_label), (gpointer *)&ctx->status_label);
    }
    ctx->progress_bar = ds->progress_bar;
    if (ctx->progress_bar != NULL) {
        g_object_add_weak_pointer(G_OBJECT(ctx->progress_bar), (gpointer *)&ctx->progress_bar);
        gtk_progress_bar_set_fraction(ctx->progress_bar, 0.0);
        gtk_progress_bar_pulse(ctx->progress_bar);
        gtk_widget_show(GTK_WIDGET(ctx->progress_bar));
        ctx->progress_timer_id = g_timeout_add(250, update_apply_progress_tick, ctx);
    }
    ctx->index = 0;
    ctx->success = 0;
    ctx->failed = 0;
    ctx->confirm_system = has_system;
    ctx->cancellable = cancellable;

    app_state_progress_arm(app);
    update_dialog_status_label_set(ds->status_label, GTK_MESSAGE_INFO, _("Загрузка обновления…"));
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
    update_apply_next(ctx);
}

static void on_check_updates_done(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
    UpdateDialogState *ds = (UpdateDialogState *)user_data;
    GPtrArray *updates;
    GError *error = NULL;
    GtkWidget *spinner_box;

    (void)source_object;

    if (ds == NULL) {
        return;
    }

    ds->async_completed = TRUE;

    if (ds->dialog == NULL) {
        update_dialog_state_free(ds);
        return;
    }

    updates = backend_check_updates_finish(NULL, result, &error);

    spinner_box = (GtkWidget *)g_object_get_data(G_OBJECT(ds->store), "spinner-box");
    if (spinner_box != NULL) {
        GtkWidget *spinner = (GtkWidget *)g_object_get_data(G_OBJECT(ds->store), "spinner");
        if (spinner != NULL && GTK_IS_SPINNER(spinner)) {
            gtk_spinner_stop(GTK_SPINNER(spinner));
        }
        gtk_widget_hide(spinner_box);
    }

    if (error != NULL) {
        if (!(error->domain == MODMAN_GUI_ERROR_DOMAIN &&
              error->code == MODMAN_GUI_ERR_CANCELLED)) {
            update_dialog_status_label_set(ds->status_label, GTK_MESSAGE_ERROR, error->message);
            ui_status_show_plain(ds->app, GTK_MESSAGE_ERROR, error->message, FALSE);
        }
        g_error_free(error);
        return;
    }

    if (updates == NULL || updates->len == 0) {
        update_dialog_status_label_set(ds->status_label,
                                       GTK_MESSAGE_INFO,
                                       _("Обновлений не найдено."));
        ui_status_show_plain(ds->app, GTK_MESSAGE_INFO, _("Обновлений не найдено."), TRUE);
        if (updates != NULL) {
            backend_update_info_array_free(updates);
        }
        return;
    }

    ui_update_populate_store(ds->store, updates);
    backend_update_info_array_free(updates);
}

/* Build and run the update dialog.
 * Called from the menu item and from --updates startup flag. */
static void ui_show_update_dialog(AppState *app)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *scroller;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkWidget *label_top;
    GtkWidget *dialog_status_label;
    GtkWidget *dialog_progress_bar;
    GtkWidget *dialog_status_row;
    GtkWidget *vbox;
    UpdateDialogState *ds;
    GCancellable *cancellable;

    if (app == NULL) {
        return;
    }

    /* Build the dialog shell */
    dialog = gtk_dialog_new_with_buttons(
        _("Проверить обновления"),
        app->window != NULL ? GTK_WINDOW(app->window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Выделить"), GTK_RESPONSE_APPLY,
        _("_Отмена"),  GTK_RESPONSE_CANCEL,
        _("_Обновить"), GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 780, 460);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);

    label_top = gtk_label_new(_("Доступные обновления модулей. Снимите галочку, чтобы пропустить."));
    gtk_label_set_xalign(GTK_LABEL(label_top), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label_top), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), label_top, FALSE, FALSE, 0);

    dialog_status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(dialog_status_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(dialog_status_label), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(dialog_status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(dialog_status_label), TRUE);
    gtk_widget_set_no_show_all(dialog_status_label, TRUE);

    dialog_progress_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(dialog_progress_bar, 180, -1);
    gtk_widget_set_no_show_all(dialog_progress_bar, TRUE);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(dialog_progress_bar), FALSE);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(dialog_progress_bar), 0.08);

    dialog_status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(dialog_status_row, 2);
    gtk_box_pack_start(GTK_BOX(dialog_status_row), dialog_status_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(dialog_status_row), dialog_progress_bar, FALSE, FALSE, 0);

    /* List store */
    store = gtk_list_store_new(UPDATE_NUM_COLS,
                               G_TYPE_BOOLEAN,  /* SELECTED */
                               G_TYPE_STRING,   /* NAME */
                               G_TYPE_STRING,   /* PATH */
                               G_TYPE_STRING,   /* OLD_VERSION */
                               G_TYPE_STRING,   /* NEW_VERSION */
                               G_TYPE_STRING,   /* REPO */
                               G_TYPE_STRING,   /* RISK */
                               G_TYPE_STRING,   /* GROUP */
                               G_TYPE_STRING,   /* ID */
                               G_TYPE_BOOLEAN); /* REBOOT */

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree_view), TRUE);
    g_object_unref(store); /* tree_view holds a ref */

    /* Allocate dialog state early so toggle callback can reference it */
    ds = g_new0(UpdateDialogState, 1);
    ds->app = app;
    ds->store = store;
    ds->tree_view = GTK_TREE_VIEW(tree_view);
    ds->dialog = GTK_DIALOG(dialog);
    ds->status_label = GTK_LABEL(dialog_status_label);
    ds->progress_bar = GTK_PROGRESS_BAR(dialog_progress_bar);

    /* Column: checkbox */
    renderer = gtk_cell_renderer_toggle_new();
    gtk_cell_renderer_toggle_set_activatable(GTK_CELL_RENDERER_TOGGLE(renderer), TRUE);
    g_signal_connect(renderer, "toggled", G_CALLBACK(on_update_row_toggled), ds);
    col = gtk_tree_view_column_new_with_attributes("", renderer,
                                                   "active", UPDATE_COL_SELECTED,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, FALSE);
    gtk_tree_view_column_set_min_width(col, 32);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: name / path */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Модуль"), renderer,
                                                   "text", UPDATE_COL_NAME,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_min_width(col, 140);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: current version */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Текущая"), renderer,
                                                   "text", UPDATE_COL_OLD_VERSION,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_min_width(col, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: new version */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Новая"), renderer,
                                                   "text", UPDATE_COL_NEW_VERSION,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_min_width(col, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: repo */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Репозиторий"), renderer,
                                                   "text", UPDATE_COL_REPO,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_min_width(col, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: risk / message */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Риск / сообщение"), renderer,
                                                   "text", UPDATE_COL_RISK,
                                                   NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_min_width(col, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Column: group (hidden — used for system-confirmation logic) */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("group", renderer,
                                                   "text", UPDATE_COL_GROUP,
                                                   NULL);
    gtk_tree_view_column_set_visible(col, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col);

    /* Blacklist controls live below the tree view and operate on rows checked
     * in the first column, so multiple updates can be blacklisted at once. */

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroller, -1, 300);
    gtk_container_add(GTK_CONTAINER(scroller), tree_view);

    gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 4);

    /* Blacklist button row */
    {
        GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *blacklist_label = gtk_label_new(_("Черный список"));
        GtkWidget *btn_blacklist = gtk_button_new_with_label(_("Добавить"));
        GtkWidget *btn_remove_blacklist = gtk_button_new_with_label(_("Редактировать"));

        gtk_label_set_xalign(GTK_LABEL(blacklist_label), 0.0f);
        gtk_widget_set_margin_end(blacklist_label, 6);

        gtk_button_set_image(GTK_BUTTON(btn_blacklist), ui_icon_image_or_fallback("list-add", "list-add-symbolic"));
        gtk_button_set_always_show_image(GTK_BUTTON(btn_blacklist), TRUE);
        gtk_widget_set_tooltip_text(btn_blacklist,
                                    _("Добавить отмеченные версии обновлений в черный список."));

        gtk_button_set_image(GTK_BUTTON(btn_remove_blacklist), ui_icon_image_or_fallback("document-edit", "document-edit-symbolic"));
        gtk_button_set_always_show_image(GTK_BUTTON(btn_remove_blacklist), TRUE);
        gtk_widget_set_tooltip_text(btn_remove_blacklist,
                                    _("Открыть черный список и удалить выбранные записи."));

        /* We pass ds as user_data; the callback reads checked rows from the store. */
        g_signal_connect(btn_blacklist, "clicked",
                         G_CALLBACK(on_update_blacklist_selected_clicked), ds);
        g_signal_connect(btn_remove_blacklist, "clicked",
                         G_CALLBACK(on_update_blacklist_remove_clicked), ds);

        gtk_box_pack_start(GTK_BOX(btn_row), blacklist_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(btn_row), btn_blacklist, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(btn_row), btn_remove_blacklist, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), btn_row, FALSE, FALSE, 2);
        gtk_box_pack_start(GTK_BOX(vbox), dialog_status_row, FALSE, FALSE, 2);
        gtk_widget_show_all(btn_row);
        gtk_widget_show(dialog_status_row);
    }

    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    /* Show a spinner / loading label while the backend runs */
    {
        GtkWidget *spinner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *spinner = gtk_spinner_new();
        GtkWidget *loading_label = gtk_label_new(_("Проверка обновлений…"));

        gtk_spinner_start(GTK_SPINNER(spinner));
        gtk_box_pack_start(GTK_BOX(spinner_box), spinner, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(spinner_box), loading_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), spinner_box, FALSE, FALSE, 4);
        gtk_widget_show_all(spinner_box);

        /* Hide spinner once store is populated (done in on_check_updates_done) */
        g_object_set_data(G_OBJECT(store), "spinner-box", spinner_box);
        g_object_set_data(G_OBJECT(store), "spinner", spinner);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(on_update_dialog_response), ds);

    /* Don't show_all yet — we show after data arrives */
    gtk_widget_show(dialog);
    gtk_widget_show(content);
    gtk_widget_show(vbox);
    gtk_widget_show(label_top);
    gtk_widget_show(scroller);
    gtk_widget_show(tree_view);

    /* Launch async check */
    cancellable = g_cancellable_new();
    ds->cancellable = cancellable;

    backend_check_updates_async(NULL,
                                cancellable,
                                on_check_updates_done,
                                ds);
}

/* Blacklist all checked update rows */
static void on_update_blacklist_selected_clicked(GtkButton *button, gpointer user_data)
{
    UpdateDialogState *ds = (UpdateDialogState *)user_data;
    GPtrArray *ids;

    (void)button;

    if (ds == NULL || ds->store == NULL) {
        return;
    }

    ids = update_store_collect_selected_ids(ds->store);
    if (ids == NULL || ids->len == 0U) {
        if (ids != NULL) {
            g_ptr_array_unref(ids);
        }
        update_dialog_status_label_set(ds->status_label,
                                       GTK_MESSAGE_WARNING,
                                       _("Отметьте обновления для добавления в чёрный список."));
        return;
    }

    update_blacklist_add_ids(ds, ids);
}

static void on_update_blacklist_remove_clicked(GtkButton *button, gpointer user_data)
{
    UpdateDialogState *ds = (UpdateDialogState *)user_data;
    GError *error = NULL;
    GPtrArray *entries;
    GPtrArray *checks;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *scroller;
    GtkWidget *box;
    guint removed = 0U;
    guint failed = 0U;

    (void)button;

    if (ds == NULL) {
        return;
    }

    entries = ui_update_blacklist_entries_load(ds->app, &error);
    if (error != NULL) {
        update_dialog_status_label_set(ds->status_label, GTK_MESSAGE_ERROR, error->message);
        g_error_free(error);
        return;
    }

    if (entries == NULL || entries->len == 0U) {
        update_dialog_status_label_set(ds->status_label,
                                       GTK_MESSAGE_INFO,
                                       _("Черный список обновлений пуст."));
        if (entries != NULL) {
            g_ptr_array_unref(entries);
        }
        return;
    }

    dialog = gtk_dialog_new_with_buttons(_("Редактировать черный список"),
                                         ds->dialog != NULL ? GTK_WINDOW(ds->dialog) : NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         _("_Отмена"), GTK_RESPONSE_CANCEL,
                                         _("_Удалить выбранные"), GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 320);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroller = gtk_scrolled_window_new(NULL, NULL);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    checks = g_ptr_array_new();

    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroller), box);
    gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 0);

    for (guint i = 0U; i < entries->len; i++) {
        UpdateBlacklistEntry *entry = g_ptr_array_index(entries, i);
        gchar *text = g_strdup_printf("%s  %s", entry->identity, entry->version);
        GtkWidget *check = gtk_check_button_new_with_label(text);
        g_object_set_data(G_OBJECT(check), "blacklist-entry", entry);
        gtk_widget_set_tooltip_text(check, text);
        gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
        g_ptr_array_add(checks, check);
        g_free(text);
    }

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        for (guint i = 0U; i < checks->len; i++) {
            GtkWidget *check = g_ptr_array_index(checks, i);
            UpdateBlacklistEntry *entry = g_object_get_data(G_OBJECT(check), "blacklist-entry");
            GError *remove_error = NULL;

            if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
                continue;
            }

            if (ui_update_blacklist_entry_remove(ds->app, entry, &remove_error)) {
                removed++;
            } else {
                failed++;
                g_clear_error(&remove_error);
            }
        }
    }
    gtk_widget_destroy(dialog);
    g_ptr_array_unref(checks);
    g_ptr_array_unref(entries);

    if (removed > 0U || failed > 0U) {
        gchar *message = failed == 0U
            ? g_strdup_printf(_("Удалено из черного списка: %u"), removed)
            : g_strdup_printf(_("Удалено: %u, ошибок: %u"), removed, failed);
        update_dialog_status_label_set(ds->status_label,
                                       failed == 0U ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
                                       message);
        g_free(message);
        if (removed > 0U) {
            update_dialog_refresh_updates(ds);
        }
    }
}

/* Menu item handler */
static void on_check_updates_menu_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    ui_show_update_dialog((AppState *)user_data);
}

/* Idle callback: open update dialog on startup when --updates was passed */
static gboolean ui_open_updates_on_start_idle(gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    if (app == NULL || !app->open_updates_on_start) {
        return G_SOURCE_REMOVE;
    }

    app->open_updates_on_start = FALSE;
    ui_show_update_dialog(app);
    return G_SOURCE_REMOVE;
}

GtkWidget *ui_build_window(AppState *app)
{
    GtkApplication *application;
    GtkAccelGroup *accel_group;
    UiLayoutState *layout_state;
    GtkWidget *root_box;
    GtkWidget *top_row;
    GtkWidget *tabs_box;
    GtkWidget *content_row;
    GtkWidget *details_widget;
    GtkWidget *action_row;
    GtkWidget *status_box = NULL;
    GtkWidget *status_label = NULL;
    GtkWidget *progress_bar = NULL;
    GtkWidget *btn_refresh;
    GtkWidget *btn_menu;
    GtkWidget *btn_load;
    GtkWidget *btn_unload;
    GtkWidget *btn_exit;
    GtkSizeGroup *footer_destructive_size_group;
    GtkSizeGroup *top_action_size_group;
    GtkSizeGroup *tab_size_group;
    guint i;

    if (app == NULL) {
        return NULL;
    }

    ui_inet_cache_clear(app);
    app->inet_cache_force_refresh = FALSE;
    app->inet_auto_sync_attempted = FALSE;
    app->sort_mode = UI_SORT_NAME;
    footer_destructive_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    top_action_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    tab_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    if (!app->capabilities_detected) {
        capabilities_detect(&app->capabilities, &app->conf);
        app->capabilities_detected = TRUE;
    }

    application = GTK_APPLICATION(g_application_get_default());
    app->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app->window), "modman");
    gtk_widget_set_size_request(app->window, 720, 520);

    layout_state = g_new0(UiLayoutState, 1);
    layout_state->app = app;
    layout_state->paneds = g_ptr_array_new();
    layout_state->current_ratio = 0.6;
    layout_state->has_saved_position = ui_layout_state_load_position(&layout_state->current_position);
    g_object_set_data_full(G_OBJECT(app->window),
                           "ui-layout-state",
                           layout_state,
                           (GDestroyNotify)ui_layout_state_free);

    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_window_key_press), app);
    g_signal_connect(app->window, "size-allocate", G_CALLBACK(on_window_size_allocate), app);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app->window), accel_group);

    root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(root_box, "root-box");
    gtk_container_add(GTK_CONTAINER(app->window), root_box);

    app->stack = GTK_STACK(gtk_stack_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->stack), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(app->stack), TRUE);
    gtk_stack_set_transition_type(app->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(app->stack, 250);
    app->sidebar = NULL;

    tabs_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(tabs_box, "modman-tabs");
    gtk_style_context_add_class(gtk_widget_get_style_context(tabs_box), "linked");
    gtk_widget_set_margin_top(tabs_box, 0);
    gtk_widget_set_margin_bottom(tabs_box, 0);
    /* Tab strip uses .linked class + GTK_RELIEF_NONE for inactive tabs.
     * Visual border between tabs and content depends on the GTK theme
     * (Adwaita draws subtle frame on active tab). Cannot fully eliminate
     * without custom CSS, which AGENTS.md forbids. */

    app->search_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), _("Search modules"));
    g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(on_toolbar_search_changed), app);
    ui_set_widget_accessible(GTK_WIDGET(app->search_entry),
                             _("Search modules"),
                             _("Search the current tab."));
    gtk_widget_set_tooltip_text(GTK_WIDGET(app->search_entry),
                                _("Search the current tab. Ctrl+F focuses, Esc clears."));

    app->sort_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(app->sort_combo, _("Name"));
    gtk_combo_box_text_append_text(app->sort_combo, _("Size"));
    gtk_combo_box_text_append_text(app->sort_combo, _("Layer"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->sort_combo), UI_SORT_NAME);
    gtk_widget_set_tooltip_text(GTK_WIDGET(app->sort_combo), _("List sorting: name, size, or layer."));
    g_signal_connect(app->sort_combo, "changed", G_CALLBACK(on_sort_combo_changed), app);
    gtk_size_group_add_widget(top_action_size_group, GTK_WIDGET(app->sort_combo));

    top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(top_row, "modman-top-row");
    gtk_widget_set_margin_start(top_row, 6);
    gtk_widget_set_margin_end(top_row, 6);
    gtk_widget_set_margin_top(top_row, 2);
    gtk_widget_set_margin_bottom(top_row, 0);
    gtk_box_pack_start(GTK_BOX(top_row), tabs_box, FALSE, FALSE, 0);

    {
        GtkWidget *menu;
        GtkWidget *mi_file_load;
        GtkWidget *mi_compact;
        GtkWidget *mi_about;
        GtkWidget *mi_config;
        GtkWidget *mi_clear_old;

        gtk_widget_set_hexpand(GTK_WIDGET(app->search_entry), TRUE);
        gtk_entry_set_width_chars(GTK_ENTRY(app->search_entry), 24);
        gtk_entry_set_max_width_chars(GTK_ENTRY(app->search_entry), 60);

        btn_refresh = gtk_button_new_from_icon_name(ui_action_icon_name(UI_ACTION_ICON_REFRESH), GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_name(btn_refresh, "btn-refresh");
        gtk_widget_set_tooltip_text(btn_refresh, _("Reload the current tab. F5."));
        g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_toolbar_refresh_clicked), app);
        gtk_widget_add_accelerator(btn_refresh,
                                   "clicked",
                                   accel_group,
                                   GDK_KEY_F5,
                                   0,
                                   GTK_ACCEL_VISIBLE);
        ui_set_widget_accessible(btn_refresh,
                                 _("Refresh list"),
                                 _("Reload the current list without changing tabs."));
        app->btn_refresh = GTK_BUTTON(btn_refresh);

        btn_menu = gtk_menu_button_new();
        gtk_widget_set_name(btn_menu, "btn-app-menu");
        gtk_widget_set_tooltip_text(btn_menu, _("Menu"));
        gtk_button_set_image(GTK_BUTTON(btn_menu),
                             gtk_image_new_from_icon_name("open-menu-symbolic",
                                                          GTK_ICON_SIZE_BUTTON));
        ui_set_widget_accessible(btn_menu, _("Menu"), _("Open application menu."));

        menu = gtk_menu_new();

        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        mi_file_load = gtk_image_menu_item_new_with_label(_("Module…"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi_file_load),
                                      gtk_image_new_from_icon_name(ui_action_icon_name(UI_ACTION_ICON_OPEN_LOCAL_FILE),
                                                                    GTK_ICON_SIZE_MENU));
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi_file_load), TRUE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_widget_set_tooltip_text(mi_file_load, _("Open a .pfs file from disk."));
        g_signal_connect(mi_file_load, "activate", G_CALLBACK(on_file_load_menu_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_file_load);
        app->mi_file_load = mi_file_load;

        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        mi_clear_old = gtk_image_menu_item_new_with_label(_("Clean Up"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi_clear_old),
                                      gtk_image_new_from_icon_name("edit-clear-all",
                                                                    GTK_ICON_SIZE_MENU));
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi_clear_old), TRUE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_widget_set_tooltip_text(mi_clear_old, _("Remove old module versions."));
        g_signal_connect(mi_clear_old, "activate", G_CALLBACK(on_clear_old_menu_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_clear_old);
        app->mi_clear_old = mi_clear_old;

        {
            GtkWidget *mi_check_updates;

            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            mi_check_updates = gtk_image_menu_item_new_with_label(_("Проверить обновления\xe2\x80\xa6"));
            gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi_check_updates),
                                          gtk_image_new_from_icon_name("software-update-available",
                                                                        GTK_ICON_SIZE_MENU));
            gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi_check_updates), TRUE);
            G_GNUC_END_IGNORE_DEPRECATIONS
            gtk_widget_set_tooltip_text(mi_check_updates, _("Check for module updates."));
            g_signal_connect(mi_check_updates, "activate",
                             G_CALLBACK(on_check_updates_menu_activate), app);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_check_updates);
        }

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

        mi_compact = gtk_check_menu_item_new_with_label(_("Compact"));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_compact), app->compact_mode);
        gtk_widget_set_tooltip_text(mi_compact, _("Compact module list"));
        g_signal_connect(mi_compact, "toggled", G_CALLBACK(on_compact_menu_toggled), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_compact);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        mi_config = gtk_image_menu_item_new_with_label(_("Settings"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi_config),
                                      gtk_image_new_from_icon_name("preferences-system",
                                                                    GTK_ICON_SIZE_MENU));
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi_config), TRUE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_widget_set_tooltip_text(mi_config,
                                    _("Open /etc/modman.conf through xdg-open with root privileges."));
        g_signal_connect(mi_config, "activate", G_CALLBACK(on_config_menu_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_config);

        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        mi_about = gtk_image_menu_item_new_with_label(_("About"));
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi_about),
                                      gtk_image_new_from_icon_name("help-about",
                                                                    GTK_ICON_SIZE_MENU));
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi_about), TRUE);
        G_GNUC_END_IGNORE_DEPRECATIONS
        g_signal_connect(mi_about, "activate", G_CALLBACK(on_about_menu_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_about);

        gtk_widget_show_all(menu);
        gtk_menu_button_set_popup(GTK_MENU_BUTTON(btn_menu), menu);
    }

    app->info_bar = gtk_info_bar_new();
    gtk_info_bar_set_show_close_button(GTK_INFO_BAR(app->info_bar), TRUE);
    gtk_info_bar_set_message_type(GTK_INFO_BAR(app->info_bar), GTK_MESSAGE_INFO);
    gtk_widget_set_no_show_all(app->info_bar, TRUE);
    app->info_label = GTK_LABEL(gtk_label_new(_("Status messages will appear here.")));
    gtk_label_set_xalign(app->info_label, 0.0f);
    gtk_label_set_use_markup(app->info_label, TRUE);
    gtk_label_set_line_wrap(app->info_label, TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(app->info_bar))), GTK_WIDGET(app->info_label));
    g_signal_connect(app->info_bar, "response", G_CALLBACK(on_info_bar_response), app);
    ui_set_widget_accessible_with_role(app->info_bar,
                                       _("Status message"),
                                       _("Status messages will appear here."),
                                       ATK_ROLE_STATUSBAR);
    ui_set_widget_accessible(GTK_WIDGET(app->info_label),
                             _("Status message text"),
                             _("Text of the last status message."));

    details_widget = details_pane_build(app, &SECTION_SPECS[0]);
    gtk_widget_set_size_request(details_widget, UI_DETAILS_PANE_DEFAULT_WIDTH, -1);
    gtk_widget_set_hexpand(details_widget, FALSE);
    gtk_widget_set_halign(details_widget, GTK_ALIGN_START);

    for (i = 0; i < SECTION_COUNT; i++) {
        GtkWidget *page = ui_build_section_page(app, SECTION_SPECS[i].id);
        GtkWidget *tab_button;

        gtk_stack_add_titled(app->stack, page, SECTION_SPECS[i].id, _(SECTION_SPECS[i].title));
        ui_set_section_sidebar_name(app, page, &SECTION_SPECS[i]);

        tab_button = ui_build_section_tab_button(app, &SECTION_SPECS[i], i, tab_size_group);
        gtk_box_pack_start(GTK_BOX(tabs_box), tab_button, FALSE, FALSE, 0);
    }
    ui_sync_tab_buttons(app);
    g_object_unref(tab_size_group);

    /* Dynamic window width: measure tabs after construction, compute a
     * width that fits them together with the details pane plus margin. */
    {
        GtkRequisition tabs_req;
        gint computed_w;

        gtk_widget_show_all(tabs_box);   /* ensure children rendered for size calc */
        gtk_widget_get_preferred_size(tabs_box, NULL, &tabs_req);

        computed_w = tabs_req.width + UI_DETAILS_PANE_DEFAULT_WIDTH + 24;  /* 24 = margin slack */
        if (computed_w < 720) {
            computed_w = 720;     /* sanity floor for very short locales */
        }
        gtk_window_set_default_size(GTK_WINDOW(app->window), computed_w, 680);
    }

    if (app->list_boxes[0] != NULL) {
        gtk_widget_set_name(GTK_WIDGET(app->list_boxes[0]), "loaded-list");
    }
    if (app->list_boxes[1] != NULL) {
        gtk_widget_set_name(GTK_WIDGET(app->list_boxes[1]), "inet-list");
    }
    if (app->list_boxes[2] != NULL) {
        gtk_widget_set_name(GTK_WIDGET(app->list_boxes[2]), "local-list");
        g_signal_connect(app->list_boxes[2],
                         "selected-rows-changed",
                         G_CALLBACK(on_local_selection_changed),
                         app);
    }
    if (app->list_boxes[3] != NULL) {
        gtk_widget_set_name(GTK_WIDGET(app->list_boxes[3]), "system-list");
    }

    g_signal_connect(app->stack, "notify::visible-child", G_CALLBACK(on_stack_child_changed), app);

    content_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(content_row, "modman-content-paned");
    gtk_widget_set_margin_top(content_row, 0);
    gtk_box_pack_start(GTK_BOX(content_row), GTK_WIDGET(app->stack), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content_row), details_widget, FALSE, FALSE, 0);

    status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(status_box, 4);
    gtk_widget_set_margin_bottom(status_box, 4);
    gtk_widget_set_margin_start(status_box, 8);
    gtk_widget_set_margin_end(status_box, 8);

    status_label = gtk_label_new(ui_operation_status_text(UI_OPERATION_STATUS_READY));
    gtk_widget_set_name(status_label, "status-label");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(status_label), TRUE);
    gtk_box_pack_start(GTK_BOX(status_box), status_label, TRUE, TRUE, 0);
    app->status_label = GTK_LABEL(status_label);

    progress_bar = gtk_progress_bar_new();
    gtk_widget_set_name(progress_bar, "progress-bar");
    gtk_widget_set_size_request(progress_bar, 180, -1);
    gtk_widget_set_no_show_all(progress_bar, TRUE);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
    ui_set_widget_accessible(progress_bar,
                             _("Operation progress"),
                             _("Shows progress of the current attach or detach operation."));
    gtk_box_pack_end(GTK_BOX(status_box), progress_bar, FALSE, FALSE, 0);
    gtk_widget_hide(progress_bar);
    app->progress_bar = GTK_PROGRESS_BAR(progress_bar);

    action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(action_row, "modman-action-row");
    gtk_widget_set_margin_start(action_row, 6);
    gtk_widget_set_margin_end(action_row, 6);
    gtk_widget_set_margin_bottom(action_row, 6);
    app->action_row = GTK_BOX(action_row);

    btn_load = gtk_button_new_with_label(_("Attach"));
    gtk_widget_set_name(btn_load, "btn-load");
    gtk_button_set_image(GTK_BUTTON(btn_load),
                         ui_icon_image_or_fallback(ui_action_icon_name(UI_ACTION_ICON_CONNECT),
                                                   "list-add-symbolic"));
    gtk_button_set_always_show_image(GTK_BUTTON(btn_load), TRUE);
    gtk_widget_set_tooltip_text(btn_load,
                                _("Attach selected modules."));
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_toolbar_load_clicked), app);
    ui_set_widget_accessible(btn_load,
                             _("Attach selected module"),
                             _("Attach the selected module to the current session."));
    app->btn_load = GTK_BUTTON(btn_load);

    btn_unload = gtk_button_new_with_label(_("Detach"));
    gtk_widget_set_name(btn_unload, "btn-unload");
    gtk_button_set_image(GTK_BUTTON(btn_unload),
                         ui_icon_image_or_fallback(ui_action_icon_name(UI_ACTION_ICON_DISCONNECT),
                                                   "list-remove-symbolic"));
    gtk_button_set_always_show_image(GTK_BUTTON(btn_unload), TRUE);
    gtk_widget_set_tooltip_text(btn_unload,
                                _("Detach selected modules."));
    g_signal_connect(btn_unload, "clicked", G_CALLBACK(on_toolbar_unload_clicked), app);
    ui_set_widget_accessible(btn_unload,
                             _("Detach selected module"),
                             _("Detach the selected module from the current session."));
    app->btn_unload = GTK_BUTTON(btn_unload);

    btn_exit = gtk_button_new_from_icon_name("application-exit", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_name(btn_exit, "btn-exit");
    gtk_widget_set_tooltip_text(btn_exit, _("Quit"));
    ui_set_widget_accessible(btn_exit, _("Quit"), _("Close the application."));
    g_signal_connect(btn_exit, "clicked", G_CALLBACK(on_btn_exit_clicked), app);
    gtk_widget_add_accelerator(btn_exit,
                               "clicked",
                               accel_group,
                               GDK_KEY_q,
                               GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
    app->btn_exit = GTK_BUTTON(btn_exit);

    gtk_box_pack_start(GTK_BOX(action_row), btn_menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(action_row), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(action_row), GTK_WIDGET(app->sort_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(action_row), GTK_WIDGET(app->search_entry), TRUE, TRUE, 0);

    gtk_box_pack_end(GTK_BOX(action_row), btn_exit, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(action_row), btn_unload, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(action_row), btn_load, FALSE, FALSE, 0);
    g_object_unref(top_action_size_group);
    g_object_unref(footer_destructive_size_group);

    gtk_box_pack_start(GTK_BOX(root_box), top_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), app->info_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), content_row, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), status_box, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(root_box), action_row, FALSE, FALSE, 0);

    if (app->list_boxes[0] != NULL) {
        details_pane_bind_for_list(app, app->list_boxes[0]);
        details_pane_update(app, NULL);
    }

    ui_set_toolbar_state(app, "system");

    gtk_widget_show_all(app->window);
    ui_sync_sidebar_accessibility(app);
    gtk_widget_hide(app->info_bar);
    app_state_apply_capability_gates(app);
    g_idle_add(app_state_open_initial_system_section_idle, app);
    if (app->open_updates_on_start) {
        g_idle_add(ui_open_updates_on_start_idle, app);
    }

    return app->window;
}
