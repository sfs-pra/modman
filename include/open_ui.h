#ifndef MODMAN_OPEN_UI_H
#define MODMAN_OPEN_UI_H

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#else
typedef int gboolean;
#endif

int modman_open_run(const char *file_path);

#endif /* MODMAN_OPEN_UI_H */
