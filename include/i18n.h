#ifndef MODMAN_I18N_H
#define MODMAN_I18N_H

#include <libintl.h>
#include <locale.h>

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "modman"
#endif

#ifndef MODMAN_LOCALEDIR
#define MODMAN_LOCALEDIR "/usr/share/locale"
#endif

#define _(s) gettext(s)
#define N_(s) (s)

#endif
