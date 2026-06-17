#ifndef MODMAN_VALIDATORS_H
#define MODMAN_VALIDATORS_H

#include <glib.h>

gboolean module_name_is_valid(const char *name);
gboolean search_query_is_valid(const char *query);

#endif
