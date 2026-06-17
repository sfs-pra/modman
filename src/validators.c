#include "../include/validators.h"

#include <glib.h>
#include <string.h>

gboolean module_name_is_valid(const char *name)
{
    const char *pattern = "^[A-Za-z0-9._+-]+$";
    GError *error = NULL;
    GRegex *regex = NULL;
    gboolean matched = FALSE;

    if (name == NULL) {
        return FALSE;
    }

    if (strlen(name) == 0 || strlen(name) > 255) {
        return FALSE;
    }

    regex = g_regex_new(pattern, 0, 0, &error);
    if (error != NULL || regex == NULL) {
        if (error != NULL) {
            g_error_free(error);
        }
        return FALSE;
    }

    matched = g_regex_match(regex, name, 0, NULL);
    g_regex_unref(regex);

    return matched;
}

gboolean search_query_is_valid(const char *query)
{
    const gchar *cursor;

    if (query == NULL) {
        return FALSE;
    }

    if (!g_utf8_validate(query, -1, NULL)) {
        return FALSE;
    }

    if (strlen(query) > 255) {
        return FALSE;
    }

    cursor = query;
    while (*cursor != '\0') {
        gunichar ch = g_utf8_get_char(cursor);

        if (g_unichar_iscntrl(ch)) {
            return FALSE;
        }

        cursor = g_utf8_next_char(cursor);
    }

    return TRUE;
}
