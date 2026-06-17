#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/backend.h"
#include "../include/i18n.h"
#include "../include/ui.h"

#if !__has_include(<gtk/gtk.h>)
typedef struct _GApplication GApplication;
typedef void *gpointer;
typedef void (*GDestroyNotify)(gpointer data);

#define G_APPLICATION_DEFAULT_FLAGS 0
#define G_APPLICATION(app) ((GApplication *)(app))
#define G_OBJECT(obj) (obj)
#define G_CALLBACK(fn) ((void *)(fn))
#define GTK_WINDOW(widget) (widget)

GtkApplication *gtk_application_new(const char *application_id, int flags);
int g_application_run(GApplication *application, int argc, char **argv);
void g_application_quit(GApplication *application);
void g_signal_connect(void *instance, const char *detailed_signal, void *c_handler, void *data);
void *g_object_get_data(void *object, const char *key);
void g_object_set_data_full(void *object, const char *key, void *data, GDestroyNotify destroy);
void g_object_unref(void *object);
GCancellable *g_cancellable_new(void);
void g_cancellable_cancel(GCancellable *cancellable);
guint g_source_remove(guint tag);
void g_error_free(GError *error);
void gtk_window_set_default_icon_name(const char *name);
void gtk_window_present(void *window);
#endif

#ifndef MODMAN_GUI_VERSION
#define MODMAN_GUI_VERSION "1.0.0"
#endif

static void app_state_free(AppState *state)
{
    if (state == NULL) {
        return;
    }

    if (state->search_debounce_id != 0) {
        g_source_remove(state->search_debounce_id);
    }

    if (state->current_op != NULL) {
        g_cancellable_cancel(state->current_op);
        g_object_unref(state->current_op);
        state->current_op = NULL;
    }

    free(state->pending_query);
    state->pending_query = NULL;
    free(state->initial_query);
    state->initial_query = NULL;
    backend_reset_runtime_conf();
    modman_conf_free(&state->conf);
    free(state);
}

static gboolean argv_has_flag(int argc, char **argv, const char *flag)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) {
            return 1;
        }
    }

    return FALSE;
}

static gboolean argv_has_version_flag(int argc, char **argv)
{
    return argv_has_flag(argc, argv, "--version") || argv_has_flag(argc, argv, "-V");
}

static gboolean argv_has_help_flag(int argc, char **argv)
{
    return argv_has_flag(argc, argv, "--help") || argv_has_flag(argc, argv, "-h");
}

static gboolean argv_has_updates_flag(int argc, char **argv)
{
    return argv_has_flag(argc, argv, "--updates");
}

static void print_help(const char *program_name)
{
    printf(_("Usage: %s [OPTIONS] [MODULE-NAME-PART]\\n\\n"), program_name != NULL ? program_name : "modman-gui");
    printf(_("Open the modman GTK interface. If a module name fragment is provided, the window opens on the Online tab with that filter.\\n\\n"));
    printf(_("Options:\\n"));
    printf(_("  -h, --help          Show this help\\n"));
    printf(_("  -V, --version       Show version\\n"));
    printf(_("      --dump-widgets  Print widget tree and exit\\n"));
    printf(_("      --updates       Open update dialog on startup\\n"));
}

static char *argv_first_query_arg(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-widgets") == 0 ||
            strcmp(argv[i], "--version") == 0 ||
            strcmp(argv[i], "-V") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--updates") == 0) {
            continue;
        }

        if (argv[i][0] != '\0') {
            return strdup(argv[i]);
        }
    }

    return NULL;
}

static gboolean dump_and_exit_idle(gpointer user_data)
{
    AppState *app = (AppState *)user_data;

    if (app != NULL && app->dump_widgets_and_exit) {
        ui_debug_dump_to_stdout(app);
        g_application_quit(G_APPLICATION(g_application_get_default()));
    }
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    GtkWidget *window;

    if (state == NULL) {
        g_application_quit(G_APPLICATION(app));
        return;
    }

    if (state->window != NULL) {
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }

    if (state->current_op == NULL) {
        state->current_op = g_cancellable_new();
    }

    window = ui_build_window(state);
    if (window == NULL) {
        g_application_quit(G_APPLICATION(app));
        return;
    }
    gtk_window_present(GTK_WINDOW(window));
    ui_debug_install_sigusr1_handler(state);
    g_idle_add(dump_and_exit_idle, state);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    AppState *state;
    GError *conf_error = NULL;
    char *app_argv[2] = { NULL, NULL };
    gboolean dump_widgets;
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, MODMAN_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    if (argv_has_version_flag(argc, argv)) {
        printf("modman-gui %s\n", MODMAN_GUI_VERSION);
        return 0;
    }

    if (argv_has_help_flag(argc, argv)) {
        print_help(argv[0]);
        return 0;
    }

    dump_widgets = argv_has_flag(argc, argv, "--dump-widgets");

    app = gtk_application_new("org.puppyrus.modman", G_APPLICATION_DEFAULT_FLAGS);
    gtk_window_set_default_icon_name("package-x-generic");
    state = calloc(1U, sizeof(AppState));
    if (state == NULL) {
        g_object_unref(app);
        return 1;
    }

    state->dump_widgets_and_exit = dump_widgets;
    state->open_updates_on_start = argv_has_updates_flag(argc, argv);
    state->initial_query = argv_first_query_arg(argc, argv);

    if (!modman_conf_load(&state->conf, &conf_error)) {
        if (conf_error != NULL) {
            g_error_free(conf_error);
            conf_error = NULL;
        }
        backend_reset_runtime_conf();
    } else {
        backend_set_runtime_conf(&state->conf);
    }

    g_object_set_data_full(G_OBJECT(app),
                           "modman-app-state",
                           state,
                           (GDestroyNotify)app_state_free);
    g_signal_connect(app, "activate", G_CALLBACK(activate), state);
    app_argv[0] = argc > 0 ? argv[0] : "modman-gui";
    status = g_application_run(G_APPLICATION(app), 1, app_argv);
    g_object_unref(app);

    return status;
}
