#include "../include/ui.h"
#include "../include/model.h"

#include <gtk/gtk.h>
#include <signal.h>
#include <string.h>

static volatile sig_atomic_t sigusr1_pending = 0;
static AppState *g_debug_app = NULL;

static void dump_widget_recursive(GtkWidget *w, gint depth, GString *out)
{
    gint i;
    GtkAllocation alloc;
    const gchar *name;

    /* indent */
    for (i = 0; i < depth * 2; i++)
        g_string_append_c(out, ' ');

    /* type name */
    g_string_append(out, g_type_name(G_OBJECT_TYPE(w)));

    /* widget name if non-default */
    name = gtk_widget_get_name(w);
    if (name && *name && g_strcmp0(name, g_type_name(G_OBJECT_TYPE(w))) != 0)
        g_string_append_printf(out, "#%s", name);

    /* visibility + sensitivity */
    g_string_append(out, gtk_widget_get_visible(w) ? " :visible" : " :hidden");
    g_string_append(out, gtk_widget_get_sensitive(w) ? " :sensitive" : " :insensitive");

    /* window-relative geometry via translate_coordinates */
    {
        GtkWidget *toplevel = gtk_widget_get_toplevel(w);
        gint wx = 0, wy = 0;
        gboolean ok = gtk_widget_translate_coordinates(w, toplevel, 0, 0, &wx, &wy);
        gtk_widget_get_allocation(w, &alloc);
        if (ok && (alloc.width > 0 || alloc.height > 0))
            g_string_append_printf(out, " wx=%d wy=%d w=%d h=%d", wx, wy, alloc.width, alloc.height);
        else
            g_string_append(out, " unrealized");
    }

    /* label text for GtkLabel, GtkButton, GtkEntry/GtkSearchEntry, GtkListBoxRow */
    if (GTK_IS_LABEL(w)) {
        const gchar *t = gtk_label_get_text(GTK_LABEL(w));
        if (t) {
            gchar *esc = g_strescape(t, NULL);
            g_string_append_printf(out, " \"%s\"", esc);
            g_free(esc);
        }
    } else if (GTK_IS_BUTTON(w)) {
        const gchar *t = gtk_button_get_label(GTK_BUTTON(w));
        if (t) {
            gchar *esc = g_strescape(t, NULL);
            g_string_append_printf(out, " \"%s\"", esc);
            g_free(esc);
        }
    } else if (GTK_IS_ENTRY(w)) {
        const gchar *t = gtk_entry_get_text(GTK_ENTRY(w));
        /* always emit for GtkEntry, even empty */
        gchar *esc = t ? g_strescape(t, NULL) : g_strdup("");
        g_string_append_printf(out, " \"%s\"", esc);
        g_free(esc);
    } else if (GTK_IS_LIST_BOX_ROW(w)) {
        /* emit module name if ModuleInfo attached */
        gpointer mod = g_object_get_data(G_OBJECT(w), "module-info");
        if (mod) {
            const gchar *mname = ((ModuleInfo *)mod)->name;
            if (mname) {
                gchar *esc = g_strescape(mname, NULL);
                g_string_append_printf(out, " module=\"%s\"", esc);
                g_free(esc);
            }
        } else {
            /* fall back: first GtkLabel child text */
            GList *children = gtk_container_get_children(GTK_CONTAINER(w));
            for (GList *l = children; l; l = l->next) {
                if (GTK_IS_LABEL(l->data)) {
                    const gchar *t = gtk_label_get_text(GTK_LABEL(l->data));
                    if (t && *t) {
                        gchar *esc = g_strescape(t, NULL);
                        g_string_append_printf(out, " \"%s\"", esc);
                        g_free(esc);
                        break;
                    }
                }
            }
            g_list_free(children);
        }
    }

    /* tooltip */
    {
        gchar *tt = gtk_widget_get_tooltip_text(w);
        if (tt && *tt) {
            gchar *esc = g_strescape(tt, NULL);
            g_string_append_printf(out, " tooltip=\"%s\"", esc);
            g_free(esc);
        }
        g_free(tt);
    }

    /* GtkProgressBar fraction */
    if (GTK_IS_PROGRESS_BAR(w))
        g_string_append_printf(out, " fraction=%.3f", gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(w)));

    /* GtkListBox selection_mode + selected_count */
    if (GTK_IS_LIST_BOX(w)) {
        GtkSelectionMode mode = gtk_list_box_get_selection_mode(GTK_LIST_BOX(w));
        g_string_append_printf(out, " selection_mode=%s",
            mode == GTK_SELECTION_MULTIPLE ? "MULTIPLE" : "SINGLE");
        GList *sel = gtk_list_box_get_selected_rows(GTK_LIST_BOX(w));
        g_string_append_printf(out, " selected_count=%u", g_list_length(sel));
        g_list_free(sel);
    }

    /* GtkStack visible_child_name */
    if (GTK_IS_STACK(w)) {
        GtkWidget *child = gtk_stack_get_visible_child(GTK_STACK(w));
        if (child) {
            const gchar *cn = gtk_stack_get_visible_child_name(GTK_STACK(w));
            if (cn) g_string_append_printf(out, " visible_child_name=%s", cn);
        }
    }

    /* GtkButton icon */
    if (GTK_IS_BUTTON(w)) {
        GtkWidget *img = gtk_button_get_image(GTK_BUTTON(w));
        if (img && GTK_IS_IMAGE(img)) {
            const gchar *icon_name = NULL;
            GtkIconSize sz;
            gtk_image_get_icon_name(GTK_IMAGE(img), &icon_name, &sz);
            if (icon_name) g_string_append_printf(out, " icon=%s", icon_name);
        }
    }

    /* orientation for GtkBox, GtkPaned */
    if (GTK_IS_ORIENTABLE(w)) {
        GtkOrientation orient = gtk_orientable_get_orientation(GTK_ORIENTABLE(w));
        g_string_append_printf(out, " orientation=%s",
            orient == GTK_ORIENTATION_HORIZONTAL ? "horizontal" : "vertical");
    }

    g_string_append_c(out, '\n');

    /* recurse into children */
    if (GTK_IS_CONTAINER(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *l = children; l; l = l->next)
            dump_widget_recursive(GTK_WIDGET(l->data), depth + 1, out);
        g_list_free(children);
    }
}

gchar *ui_debug_dump_widget_tree(AppState *app)
{
    GString *out = g_string_new(NULL);
    if (app && app->window)
        dump_widget_recursive(GTK_WIDGET(app->window), 0, out);
    return g_string_free(out, FALSE);
}

void ui_debug_dump_to_stdout(AppState *app)
{
    gchar *s = ui_debug_dump_widget_tree(app);
    g_print("%s", s);
    g_free(s);
}

static gboolean sigusr1_idle_cb(gpointer user_data)
{
    if (sigusr1_pending) {
        sigusr1_pending = 0;
        ui_debug_dump_to_stdout((AppState *)user_data);
    }
    return G_SOURCE_CONTINUE;
}

static void sigusr1_handler(int sig)
{
    (void)sig;
    sigusr1_pending = 1;
}

void ui_debug_install_sigusr1_handler(AppState *app)
{
    g_debug_app = app;
    signal(SIGUSR1, sigusr1_handler);
    g_timeout_add(100, sigusr1_idle_cb, app);
}
