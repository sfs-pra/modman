#include "../include/open_ui.h"

#include "../include/backend.h"
#include "../include/i18n.h"

#include <gtk/gtk.h>
#include <stdlib.h>

typedef struct {
    char *resolved_path;
    char *module_name;
    char *format;
    char *format_detail;
    char *size;
    char *modules;
    char *dependencies;
    char *state;
    char *error_text;
    gboolean valid;
    gboolean is_loaded;
} OpenDialogData;

typedef struct {
    GtkWidget *dialog;
    GtkWidget *status;
    GtkWidget *status_bar;
    GtkWidget *state_value;
    GtkWidget *primary_button;
    GtkWidget *lower_check;
    GtkWidget *ram_check;
    GtkWidget *spinner;
    guint pulse_timer_id;
    GCancellable *cancellable;
    gboolean running;
    gboolean op_done;
    gboolean op_ok;
    gboolean is_unload;
    OpenDialogData *data;
} OpenDialogOp;

static void open_dialog_data_clear(OpenDialogData *data)
{
    if (data == NULL) {
        return;
    }

    g_free(data->resolved_path);
    g_free(data->module_name);
    g_free(data->format);
    g_free(data->format_detail);
    g_free(data->size);
    g_free(data->modules);
    g_free(data->dependencies);
    g_free(data->state);
    g_free(data->error_text);
}

static char *open_dialog_format_display(const OpenDialogData *data)
{
    if (data == NULL || !data->valid) {
        return g_strdup(_("—"));
    }

    if (data->format_detail != NULL && data->format_detail[0] != '\0') {
        return g_strdup(data->format_detail);
    }

    if (data->format == NULL || data->format[0] == '\0' || g_strcmp0(data->format, "unknown") == 0) {
        return g_strdup(_("Не определён"));
    }

    return g_strdup(data->format);
}

static char *open_dialog_items_display(const char *items)
{
    gchar **parts;
    GString *text;
    guint i;

    if (items == NULL || items[0] == '\0') {
        return g_strdup(_("—"));
    }

    parts = g_strsplit(items, " ", -1);
    text = g_string_new(NULL);
    for (i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0') {
            continue;
        }
        if (text->len > 0U) {
            g_string_append_c(text, '\n');
        }
        g_string_append(text, parts[i]);
    }
    g_strfreev(parts);

    if (text->len == 0U) {
        g_string_free(text, TRUE);
        return g_strdup("—");
    }

    return g_string_free(text, FALSE);
}

static void open_dialog_set_state_badge(GtkWidget *label, gboolean valid, gboolean is_loaded)
{
    const char *color;
    const char *text;
    char *markup;

    if (label == NULL) {
        return;
    }

    if (!valid) {
        color = "#c62828";
        text = _("Ошибка");
    } else if (is_loaded) {
        color = "#2e7d32";
        text = _("Подключен");
    } else {
        color = "#546e7a";
        text = _("Локальный");
    }

    markup = g_strdup_printf("<span foreground='%s'><b>%s</b></span>", color, text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
}

static void open_dialog_update_primary_button(GtkWidget *button, gboolean is_loaded)
{
    GtkWidget *image;

    if (button == NULL) {
        return;
    }

    gtk_button_set_label(GTK_BUTTON(button), is_loaded ? _("_Отключить") : _("_Подключить"));
    gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
    image = gtk_image_new_from_icon_name(is_loaded ? "gtk-remove" : "gtk-add", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
}

static void open_dialog_set_status_markup(GtkWidget *status,
                                           const char *title,
                                           const char *message,
                                           const char *color)
{
    char *escaped = NULL;
    char *markup = NULL;

    if (status == NULL) {
        return;
    }

    escaped = g_markup_escape_text(message != NULL ? message : "", -1);
    markup = g_strdup_printf("<span foreground='%s'><b>%s:</b> %s</span>",
                             color != NULL ? color : "#424242",
                             title != NULL ? title : _("Статус"),
                             escaped != NULL ? escaped : "");
    gtk_label_set_markup(GTK_LABEL(status), markup);
    g_free(markup);
    g_free(escaped);
}

static char *open_dialog_resolve_path(const char *file_path)
{
    char *resolved;

    if (file_path == NULL || file_path[0] == '\0') {
        return NULL;
    }

    resolved = realpath(file_path, NULL);
    if (resolved != NULL) {
        return resolved;
    }

    if (g_path_is_absolute(file_path)) {
        return g_strdup(file_path);
    }

    return g_canonicalize_filename(file_path, NULL);
}

static void open_dialog_collect_file_info(const char *file_path, OpenDialogData *out)
{
    BackendOpenFileInfo *info = NULL;
    GError *error = NULL;
    char *query_path;

    if (out == NULL) {
        return;
    }

    query_path = open_dialog_resolve_path(file_path);
    if (query_path == NULL) {
        out->valid = FALSE;
        out->error_text = g_strdup(_("Не удалось определить путь к файлу."));
        return;
    }

    info = backend_open_file_info_sync(query_path, &error);
    if (info == NULL) {
        out->valid = FALSE;
        if (error != NULL && error->message != NULL && error->message[0] != '\0') {
            out->error_text = g_strdup(error->message);
        } else {
            out->error_text = g_strdup(_("Не удалось получить информацию о модуле."));
        }
        g_clear_error(&error);
        g_free(query_path);
        return;
    }

    out->valid = TRUE;
    out->is_loaded = info->is_loaded;
    out->resolved_path = g_strdup(info->path);
    out->module_name = g_strdup(info->name);
    out->format = g_strdup(info->format);
    out->format_detail = g_strdup(info->format_detail);
    out->size = g_strdup(info->size);
    out->modules = g_strdup(info->modules);
    out->dependencies = g_strdup(info->dependencies);
    out->state = g_strdup(info->loaded_state);

    backend_open_file_info_free(info);
    g_free(query_path);
}

static gboolean open_dialog_spinner_pulse_tick(gpointer user_data)
{
    OpenDialogOp *op = (OpenDialogOp *)user_data;

    if (op == NULL || op->spinner == NULL || !op->running) {
        return G_SOURCE_REMOVE;
    }

    gtk_spinner_start(GTK_SPINNER(op->spinner));
    return G_SOURCE_CONTINUE;
}

static void open_dialog_stop_progress(OpenDialogOp *op)
{
    if (op == NULL) {
        return;
    }

    if (op->pulse_timer_id != 0U) {
        g_source_remove(op->pulse_timer_id);
        op->pulse_timer_id = 0U;
    }

    if (op->spinner != NULL) {
        gtk_spinner_stop(GTK_SPINNER(op->spinner));
        gtk_widget_hide(op->spinner);
    }
}

static void open_dialog_refresh_after_success(OpenDialogOp *op)
{
    BackendOpenFileInfo *info;
    GError *error = NULL;

    if (op == NULL || op->data == NULL || op->data->resolved_path == NULL) {
        return;
    }

    info = backend_open_file_info_sync(op->data->resolved_path, &error);
    if (info == NULL) {
        if (error != NULL && error->message != NULL) {
            open_dialog_set_status_markup(op->status,
                                          _("Предупреждение"),
                                          error->message,
                                          "#ef6c00");
        }
        g_clear_error(&error);
        return;
    }

    op->data->is_loaded = info->is_loaded;

    g_free(op->data->module_name);
    g_free(op->data->format);
    g_free(op->data->format_detail);
    g_free(op->data->size);
    g_free(op->data->modules);
    g_free(op->data->dependencies);
    g_free(op->data->state);
    op->data->module_name = g_strdup(info->name);
    op->data->format = g_strdup(info->format);
    op->data->format_detail = g_strdup(info->format_detail);
    op->data->size = g_strdup(info->size);
    op->data->modules = g_strdup(info->modules);
    op->data->dependencies = g_strdup(info->dependencies);
    op->data->state = g_strdup(info->loaded_state);

    if (op->state_value != NULL) {
        open_dialog_set_state_badge(op->state_value, TRUE, info->is_loaded);
    }

    if (op->lower_check != NULL) {
        gtk_widget_set_sensitive(op->lower_check, !info->is_loaded);
    }
    if (op->ram_check != NULL) {
        gtk_widget_set_sensitive(op->ram_check, !info->is_loaded);
    }

    open_dialog_update_primary_button(op->primary_button, info->is_loaded);

    backend_open_file_info_free(info);
}

static void open_dialog_on_action_done(GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
    OpenDialogOp *op = (OpenDialogOp *)user_data;
    GError *error = NULL;
    gboolean ok = FALSE;

    (void)source_object;

    if (op == NULL) {
        return;
    }

    if (op->is_unload) {
        ok = backend_open_unload_path_finish(result, &error);
    } else {
        ok = backend_open_attach_path_finish(result, &error);
    }

    op->running = FALSE;
    op->op_done = TRUE;
    op->op_ok = ok;
    open_dialog_stop_progress(op);

    if (error != NULL) {
        if (op->status_bar != NULL) {
            gtk_info_bar_set_message_type(GTK_INFO_BAR(op->status_bar), GTK_MESSAGE_OTHER);
        }
        open_dialog_set_status_markup(op->status,
                                      _("Ошибка"),
                                      error->message != NULL ? error->message : _("Операция завершилась с ошибкой."),
                                      "#d32f2f");
        g_clear_error(&error);
    } else if (!ok) {
        if (op->status_bar != NULL) {
            gtk_info_bar_set_message_type(GTK_INFO_BAR(op->status_bar), GTK_MESSAGE_OTHER);
        }
        open_dialog_set_status_markup(op->status,
                                      _("Ошибка"),
                                      _("Операция завершилась без результата."),
                                      "#d32f2f");
    } else {
        if (op->status_bar != NULL) {
            gtk_info_bar_set_message_type(GTK_INFO_BAR(op->status_bar), GTK_MESSAGE_OTHER);
        }
        open_dialog_set_status_markup(op->status,
                                      _("Готово"),
                                      op->is_unload ? _("Модуль успешно отключен.") : _("Модуль успешно подключен."),
                                      "#2e7d32");
        open_dialog_refresh_after_success(op);
    }

    if (op->primary_button != NULL) {
        gtk_widget_set_sensitive(op->primary_button, op->data != NULL && op->data->valid);
    }
}

static GtkWidget *open_dialog_info_row(GtkWidget *grid,
                                       int row,
                                       const char *title,
                                       const char *value)
{
    GtkWidget *caption;
    GtkWidget *content;

    caption = gtk_label_new(title);
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_widget_set_valign(caption, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(caption), "dim-label");

    content = gtk_label_new(value != NULL && value[0] != '\0' ? value : "—");
    gtk_label_set_selectable(GTK_LABEL(content), TRUE);
    gtk_label_set_xalign(GTK_LABEL(content), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(content), TRUE);
    gtk_widget_set_halign(content, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(content, TRUE);

    gtk_grid_attach(GTK_GRID(grid), caption, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), content, 1, row, 1, 1);
    return content;
}

static GtkWidget *open_dialog_section_new(const char *title, GtkWidget **out_box)
{
    GtkWidget *frame;
    GtkWidget *box;

    frame = gtk_frame_new(title);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_widget_set_hexpand(frame, TRUE);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_container_add(GTK_CONTAINER(frame), box);

    if (out_box != NULL) {
        *out_box = box;
    }

    return frame;
}

static GtkWidget *open_dialog_caption_new(const char *text)
{
    GtkWidget *label;

    label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
    return label;
}

static GtkWidget *open_dialog_option_row_new(GtkWidget *check,
                                             const char *tooltip)
{
    GtkWidget *box;

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);

    if (tooltip != NULL && tooltip[0] != '\0') {
        gtk_widget_set_tooltip_text(check, tooltip);
        gtk_widget_set_tooltip_text(box, tooltip);
    }

    return box;
}

static void open_dialog_summary_attach(GtkWidget *grid,
                                       int column,
                                       const char *title,
                                       const char *value)
{
    GtkWidget *caption;
    GtkWidget *content;

    caption = gtk_label_new(title);
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(caption), "dim-label");

    content = gtk_label_new(value != NULL && value[0] != '\0' ? value : _("—"));
    gtk_label_set_selectable(GTK_LABEL(content), TRUE);
    gtk_label_set_xalign(GTK_LABEL(content), 0.0f);
    gtk_widget_set_halign(content, GTK_ALIGN_START);

    gtk_grid_attach(GTK_GRID(grid), caption, column * 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), content, column * 2 + 1, 0, 1, 1);
}

static GtkWidget *open_dialog_content_list_new(const char *items)
{
    GtkWidget *scroll;
    GtkWidget *label;
    char *display;
    int line_count = 1;
    const char *p;

    display = open_dialog_items_display(items);
    for (p = display; p != NULL && *p != '\0'; p++) {
        if (*p == '\n') {
            line_count++;
        }
    }

    label = gtk_label_new(display);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, line_count <= 1 ? 30 : 82);
    gtk_container_add(GTK_CONTAINER(scroll), label);

    g_free(display);
    return scroll;
}

static GtkWidget *open_dialog_status_bar_new(GtkWidget **out_label)
{
    GtkWidget *bar;
    GtkWidget *area;
    GtkWidget *label;

    bar = gtk_info_bar_new();
    gtk_info_bar_set_message_type(GTK_INFO_BAR(bar), GTK_MESSAGE_OTHER);
    gtk_info_bar_set_show_close_button(GTK_INFO_BAR(bar), FALSE);

    area = gtk_info_bar_get_content_area(GTK_INFO_BAR(bar));
    label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_container_add(GTK_CONTAINER(area), label);

    if (out_label != NULL) {
        *out_label = label;
    }

    return bar;
}

int modman_open_run(const char *file_path)
{
    OpenDialogData data = { 0 };
    OpenDialogOp op = { 0 };
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *outer_box;
    GtkWidget *hero_box;
    GtkWidget *hero_icon;
    GtkWidget *hero_text_box;
    GtkWidget *hero_title;
    GtkWidget *hero_path;
    GtkWidget *file_section;
    GtkWidget *file_box;
    GtkWidget *file_grid;
    GtkWidget *content_section;
    GtkWidget *content_box;
    GtkWidget *action_section;
    GtkWidget *action_box;
    GtkWidget *action_row;
    GtkWidget *status;
    GtkWidget *status_bar;
    GtkWidget *primary_button;
    char *format_display;
    char *title_markup;
    int response;

    open_dialog_collect_file_info(file_path, &data);

    dialog = gtk_dialog_new_with_buttons(_("PFS Module Manager"),
                                         NULL,
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         _("_Закрыть"),
                                         GTK_RESPONSE_CLOSE,
                                         data.is_loaded ? _("_Отключить") : _("_Подключить"),
                                           GTK_RESPONSE_ACCEPT,
                                           NULL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, -1);
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "package-x-generic");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(outer_box, 14);
    gtk_widget_set_margin_bottom(outer_box, 14);
    gtk_widget_set_margin_start(outer_box, 14);
    gtk_widget_set_margin_end(outer_box, 14);
    gtk_container_add(GTK_CONTAINER(content_area), outer_box);

    hero_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(outer_box), hero_box, FALSE, FALSE, 0);

    hero_icon = gtk_image_new_from_icon_name("package-x-generic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_valign(hero_icon, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hero_box), hero_icon, FALSE, FALSE, 0);

    hero_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(hero_text_box, TRUE);
    gtk_box_pack_start(GTK_BOX(hero_box), hero_text_box, TRUE, TRUE, 0);

    title_markup = g_markup_printf_escaped("<span size='large' weight='bold'>%s</span>",
                                           data.module_name != NULL && data.module_name[0] != '\0'
                                               ? data.module_name
                                               : _("Локальный модуль"));
    hero_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hero_title), title_markup);
    gtk_label_set_xalign(GTK_LABEL(hero_title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hero_title), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(hero_text_box), hero_title, FALSE, FALSE, 0);

    hero_path = gtk_label_new(data.resolved_path != NULL ? data.resolved_path : file_path);
    gtk_label_set_selectable(GTK_LABEL(hero_path), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hero_path), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(hero_path), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(hero_path), "dim-label");
    gtk_box_pack_start(GTK_BOX(hero_text_box), hero_path, FALSE, FALSE, 0);

    op.state_value = gtk_label_new(NULL);
    gtk_widget_set_valign(op.state_value, GTK_ALIGN_START);
    open_dialog_set_state_badge(op.state_value, data.valid, data.is_loaded);
    gtk_box_pack_start(GTK_BOX(hero_box), op.state_value, FALSE, FALSE, 0);
    g_free(title_markup);

    file_section = open_dialog_section_new(_("Файл"), &file_box);
    gtk_box_pack_start(GTK_BOX(outer_box), file_section, FALSE, FALSE, 0);

    file_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(file_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(file_grid), 12);
    gtk_widget_set_hexpand(file_grid, TRUE);
    gtk_box_pack_start(GTK_BOX(file_box), file_grid, FALSE, FALSE, 0);

    format_display = open_dialog_format_display(&data);
    open_dialog_summary_attach(file_grid, 0, _("Формат:"), format_display);
    open_dialog_summary_attach(file_grid, 1, _("Размер:"), data.size);
    g_free(format_display);

    content_section = open_dialog_section_new(_("Содержимое"), &content_box);
    gtk_box_pack_start(GTK_BOX(outer_box), content_section, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content_box), open_dialog_caption_new(_("Модули:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_box), open_dialog_content_list_new(data.modules), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_box), open_dialog_caption_new(_("Зависимости:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_box), open_dialog_content_list_new(data.dependencies), FALSE, FALSE, 0);

    action_section = open_dialog_section_new(_("Действие"), &action_box);
    gtk_box_pack_start(GTK_BOX(outer_box), action_section, FALSE, FALSE, 0);

    action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_box_set_homogeneous(GTK_BOX(action_row), TRUE);
    gtk_box_pack_start(GTK_BOX(action_box), action_row, FALSE, FALSE, 0);

    op.lower_check = gtk_check_button_new_with_label(_("В нижний слой"));
    op.ram_check = gtk_check_button_new_with_label(_("Загрузить в RAM"));
    gtk_box_pack_start(GTK_BOX(action_row),
                       open_dialog_option_row_new(op.lower_check,
                                                  _("Ниже текущих runtime-модулей; полезно для базовых слоёв.")),
                       TRUE,
                       TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(action_row),
                       open_dialog_option_row_new(op.ram_check,
                                                  _("Быстрее при работе, но расходует оперативную память.")),
                       TRUE,
                       TRUE,
                       0);
    if (!data.valid || data.is_loaded) {
        gtk_widget_set_sensitive(op.lower_check, FALSE);
        gtk_widget_set_sensitive(op.ram_check, FALSE);
    }

    status_bar = open_dialog_status_bar_new(&status);
    gtk_box_pack_start(GTK_BOX(outer_box), status_bar, FALSE, FALSE, 0);

    op.spinner = gtk_spinner_new();
    gtk_widget_set_halign(op.spinner, GTK_ALIGN_START);
    gtk_widget_set_no_show_all(op.spinner, TRUE);
    gtk_box_pack_start(GTK_BOX(outer_box), op.spinner, FALSE, FALSE, 0);

    if (!data.valid) {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(status_bar), GTK_MESSAGE_OTHER);
        open_dialog_set_status_markup(status,
                                      _("Ошибка"),
                                      data.error_text != NULL ? data.error_text : _("Неизвестная ошибка."),
                                      "#d32f2f");
    } else if (data.is_loaded) {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(status_bar), GTK_MESSAGE_OTHER);
        gtk_label_set_text(GTK_LABEL(status), _("Подключен"));
    } else {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(status_bar), GTK_MESSAGE_OTHER);
        gtk_label_set_text(GTK_LABEL(status), _("Не подключен"));
    }

    primary_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    op.dialog = dialog;
    op.status = status;
    op.status_bar = status_bar;
    op.primary_button = primary_button;
    op.data = &data;
    if (primary_button != NULL) {
        gtk_widget_set_sensitive(primary_button, data.valid);
        open_dialog_update_primary_button(primary_button, data.is_loaded);
        if (!data.valid) {
            gtk_widget_set_tooltip_text(primary_button,
                                        _("Действие недоступно: ошибка проверки файла."));
        } else {
            gtk_widget_set_tooltip_text(primary_button, NULL);
        }
    }

    gtk_widget_show_all(dialog);

    response = GTK_RESPONSE_NONE;
    while (response != GTK_RESPONSE_CLOSE && response != GTK_RESPONSE_DELETE_EVENT) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response != GTK_RESPONSE_ACCEPT) {
            continue;
        }

        if (!data.valid || data.resolved_path == NULL || primary_button == NULL) {
            continue;
        }

        op.running = TRUE;
        op.op_done = FALSE;
        op.op_ok = FALSE;
        op.is_unload = data.is_loaded;

        op.cancellable = g_cancellable_new();
        gtk_widget_set_sensitive(primary_button, FALSE);
        gtk_widget_show(op.spinner);
        gtk_spinner_start(GTK_SPINNER(op.spinner));
        op.pulse_timer_id = g_timeout_add(120, open_dialog_spinner_pulse_tick, &op);

        if (op.is_unload) {
            gtk_info_bar_set_message_type(GTK_INFO_BAR(status_bar), GTK_MESSAGE_OTHER);
            open_dialog_set_status_markup(status,
                                          _("Выполняется"),
                                          _("Отключение модуля…"),
                                          "#424242");
            backend_open_unload_path_async(data.resolved_path,
                                           op.cancellable,
                                           open_dialog_on_action_done,
                                           &op);
        } else {
            gtk_info_bar_set_message_type(GTK_INFO_BAR(status_bar), GTK_MESSAGE_OTHER);
            open_dialog_set_status_markup(status,
                                          _("Выполняется"),
                                          _("Подключение модуля…"),
                                          "#424242");
            backend_open_attach_path_with_modes_async(
                data.resolved_path,
                op.lower_check != NULL && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(op.lower_check)),
                op.ram_check != NULL && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(op.ram_check)),
                op.cancellable,
                open_dialog_on_action_done,
                &op);
        }

        while (!op.op_done) {
            g_main_context_iteration(NULL, TRUE);
        }

        g_clear_object(&op.cancellable);
    }

    open_dialog_stop_progress(&op);
    g_clear_object(&op.cancellable);
    gtk_widget_destroy(dialog);

    open_dialog_data_clear(&data);
    return 0;
}
