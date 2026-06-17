#ifndef MODMAN_UI_H
#define MODMAN_UI_H

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#else
typedef unsigned int guint;
typedef long long gint64;
typedef int gboolean;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkStack GtkStack;
typedef struct _GtkStackSidebar GtkStackSidebar;
typedef struct _GtkListBox GtkListBox;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkButton GtkButton;
typedef struct _GtkCheckButton GtkCheckButton;
typedef struct _GtkComboBoxText GtkComboBoxText;
typedef struct _GtkSearchEntry GtkSearchEntry;
typedef struct _GtkProgressBar GtkProgressBar;
typedef struct _GtkBox GtkBox;
typedef struct _GtkListBoxRow GtkListBoxRow;
typedef struct _GError GError;
typedef struct _GCancellable GCancellable;
typedef void (*GCallback)(void);
typedef char gchar;

typedef enum {
    GTK_MESSAGE_INFO,
    GTK_MESSAGE_WARNING,
    GTK_MESSAGE_QUESTION,
    GTK_MESSAGE_ERROR,
    GTK_MESSAGE_OTHER
} GtkMessageType;
#endif

#include "model.h"
#include "capabilities.h"

typedef struct AppDetailsWidgets {
    GtkLabel *name;
    GtkWidget *status_icon;
    GtkLabel *status;
    GtkLabel *version;
    GtkLabel *category;
    GtkLabel *size;
    GtkLabel *path;
    GtkLabel *repo;
    GtkLabel *description;
    GtkLabel *hooks_start;
    GtkLabel *hooks_stop;
    GtkLabel *depends;
    GtkLabel *modules;
} AppDetailsWidgets;

typedef struct AppState {
    GtkWidget *window;
    GtkStack *stack;
    GtkStackSidebar *sidebar;
    GtkListBox *list_boxes[4];
    GtkWidget *info_bar;
    GtkLabel *info_label;
    GtkLabel *status_label;
    char *progress_file_path;
    guint progress_timer_id;
    GtkWidget *info_action;
    GCallback info_action_cb;
    guint info_hide_timer;
    GtkMessageType info_message_type;
    int info_priority;
    GtkSearchEntry *search_entry;
    GtkComboBoxText *sort_combo;
    GtkButton *btn_load;
    GtkWidget *mi_file_load;
    GtkButton *btn_unload;
    GtkButton *btn_refresh;
    GtkButton *btn_clear_old;
    GtkWidget *mi_clear_old;
    GCancellable *current_op;
    GCancellable *inet_prefetch_op;
    GCancellable *details_file_info_op;
    guint search_debounce_id;
    char *pending_query;
    char *initial_query;
    GPtrArray *inet_cache;
    gint64 inet_cache_db_mtime;
    char *inet_cache_modman_bin;
    gboolean inet_cache_force_refresh;
    gboolean inet_auto_sync_attempted;
    guint details_deps_seq;
    guint details_file_info_seq;
    ModuleInfo *selected_module;
    AppDetailsWidgets details;
    ModmanConf conf;
    AppCapabilities capabilities;
    gboolean capabilities_detected;
    gboolean read_only_mode;
    gboolean suppress_next_refresh_info;
    gboolean inet_refresh_pending_completion;
    int sort_mode;
    GtkButton *btn_delete;
    GtkButton *btn_exit;
    GtkProgressBar *progress_bar;
    GtkBox *action_row;
    GtkListBoxRow *last_clicked_local_row;
    GtkWidget *tab_buttons[4];
    gboolean dump_widgets_and_exit;
    gboolean compact_mode;
    GtkButton *btn_compact;
    gboolean open_updates_on_start;
} AppState;

GtkWidget *ui_build_window(AppState *app);
GtkWidget *ui_build_module_row(ModuleInfo *module);
char *ui_build_highlight_markup(const char *text, const char *query);
void app_state_show_info(AppState *app,
                         GtkMessageType type,
                         const char *markup,
                         const char *action_label,
                         GCallback action_cb,
                         gboolean auto_hide_8s);
void app_state_hide_info(AppState *app);
gboolean app_state_confirm_dangerous(AppState *app,
                                     const char *title,
                                     const char *body,
                                     const char *danger_action_label);

gchar *ui_debug_dump_widget_tree(AppState *app);
void ui_debug_dump_to_stdout(AppState *app);
void ui_debug_install_sigusr1_handler(AppState *app);

#endif
