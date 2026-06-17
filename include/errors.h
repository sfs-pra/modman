#ifndef MODMAN_ERRORS_H
#define MODMAN_ERRORS_H

#if __has_include(<glib.h>)
#include <glib.h>
#else
typedef unsigned int GQuark;
#endif

#define MODMAN_GUI_ERROR_DOMAIN (modman_gui_error_quark())

typedef enum {
    MODMAN_GUI_ERR_UNKNOWN = 0,
    MODMAN_GUI_ERR_NETWORK,
    MODMAN_GUI_ERR_PERMISSION,
    MODMAN_GUI_ERR_BACKEND_MISSING,
    MODMAN_GUI_ERR_INVALID_NAME,
    MODMAN_GUI_ERR_PARSE,
    MODMAN_GUI_ERR_CANCELLED,
    MODMAN_GUI_ERR_IO,
    MODMAN_GUI_ERR_DEP_CONFLICT,
    MODMAN_GUI_ERR_REMOVE_LOCAL_PROCESS_FAILED
} ModmanGuiError;

GQuark modman_gui_error_quark(void);

#endif
