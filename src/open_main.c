#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include "../include/i18n.h"
#include "../include/open_ui.h"

#define MODMAN_OPEN_VERSION "0.1"

static void print_usage(const char *prog)
{
    fprintf(stderr, _("Usage: %s FILE.pfs\n\n"
            "Open a local .pfs module file: attach or unload via modman.\n\n"
            "Options:\n"
            "  -h, --help     Show this help\n"
            "  -V, --version  Show version\n"),
            prog != NULL ? prog : "modman-open");
}

int main(int argc, char **argv)
{
    const char *file_path = NULL;
    int i;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, MODMAN_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_set_prgname("modman-gui");
    g_set_application_name(_("PFS Module Manager"));

    for (i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--help") == 0 || g_strcmp0(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (g_strcmp0(argv[i], "--version") == 0 || g_strcmp0(argv[i], "-V") == 0) {
            printf("modman-open %s\n", MODMAN_OPEN_VERSION);
            return 0;
        }
        if (argv[i][0] != '-') {
            file_path = argv[i];
        }
    }

    if (file_path == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    gtk_init(&argc, &argv);
    gdk_set_program_class("modman-gui");
    gtk_window_set_default_icon_name("package-x-generic");
    return modman_open_run(file_path);
}
