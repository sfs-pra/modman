#if __has_include(<glib.h>)
#include <glib.h>
#endif

#include "../include/model.h"

#include <string.h>

struct ModuleRepository {
    GPtrArray *modules;
    GHashTable *by_name;
};

static void module_info_free_internal(ModuleInfo *module)
{
    if (module == NULL) {
        return;
    }

    g_free(module->name);
    g_free(module->layer);
    g_free(module->path);
    g_free(module->desc);
    g_free(module->category);
    g_free(module->version);
    g_free(module->repo);
    g_free(module->mount_mode);
    g_free(module->mount_in_ram);
    g_free(module);
}

void module_update_info_free(ModuleUpdateInfo *update_info)
{
    if (update_info == NULL) {
        return;
    }

    g_free(update_info->id);
    g_free(update_info->name);
    g_free(update_info->old_version);
    g_free(update_info->new_version);
    g_free(update_info->installed_path);
    g_free(update_info->candidate_filename);
    g_free(update_info->repo);
    g_free(update_info->risk);
    g_free(update_info->message);
    g_free(update_info);
}

static GQuark model_error_quark(void)
{
    return g_quark_from_static_string("modman-model-error");
}

static void set_parse_error(GError **error, const char *message)
{
    g_set_error_literal(error,
                        model_error_quark(),
                        2,
                        message);
}

static gboolean tsv_split_escaped(const char *line,
                                  char ***out_fields,
                                  gsize *out_count,
                                  gsize min_fields,
                                  gsize max_fields,
                                  const char *range_error,
                                  GError **error)
{
    GPtrArray *fields = NULL;
    GString *current = NULL;
    gboolean escaping = FALSE;

    if (line == NULL) {
        g_set_error_literal(error,
                            model_error_quark(),
                            1,
                            "TSV line is NULL");
        return FALSE;
    }

    if (*line == '\0') {
        set_parse_error(error, "TSV line is empty");
        return FALSE;
    }

    fields = g_ptr_array_new_with_free_func(g_free);
    current = g_string_new(NULL);

    for (const char *p = line; *p != '\0'; p++) {
        if (escaping) {
            if (*p == 't') {
                g_string_append_c(current, '\t');
            } else if (*p == 'n') {
                g_string_append_c(current, '\n');
            } else if (*p == '\\') {
                g_string_append_c(current, '\\');
            } else {
                g_string_append_c(current, '\\');
                g_string_append_c(current, *p);
            }
            escaping = FALSE;
            continue;
        }

        if (*p == '\\') {
            escaping = TRUE;
            continue;
        }

        if (*p == '\t') {
            g_ptr_array_add(fields, g_string_free(current, FALSE));
            current = g_string_new(NULL);
            continue;
        }

        g_string_append_c(current, *p);
    }

    if (escaping) {
        g_string_free(current, TRUE);
        g_ptr_array_unref(fields);
        set_parse_error(error, "TSV line ends with incomplete escape sequence");
        return FALSE;
    }

    g_ptr_array_add(fields, g_string_free(current, FALSE));

    if (fields->len < min_fields || fields->len > max_fields) {
        set_parse_error(error, range_error);
        g_ptr_array_unref(fields);
        return FALSE;
    }

    g_ptr_array_add(fields, NULL);
    *out_count = fields->len - 1;
    *out_fields = (char **)g_ptr_array_free(fields, FALSE);
    return TRUE;
}

char *tsv_escape(const char *input)
{
    GString *result = g_string_new(NULL);

    if (input == NULL) {
        return g_string_free(result, FALSE);
    }

    for (const char *p = input; *p != '\0'; p++) {
        switch (*p) {
            case '\\':
                g_string_append(result, "\\\\");
                break;
            case '\t':
                g_string_append(result, "\\t");
                break;
            case '\n':
                g_string_append(result, "\\n");
                break;
            default:
                g_string_append_c(result, *p);
                break;
        }
    }

    return g_string_free(result, FALSE);
}

char *tsv_unescape(const char *input, GError **error)
{
    GString *result = g_string_new(NULL);

    if (input == NULL) {
        g_set_error_literal(error,
                            model_error_quark(),
                            3,
                            "TSV input is NULL");
        g_string_free(result, TRUE);
        return NULL;
    }

    gboolean escaping = FALSE;

    for (const char *p = input; *p != '\0'; p++) {
        if (escaping) {
            if (*p == 't') {
                g_string_append_c(result, '\t');
            } else if (*p == 'n') {
                g_string_append_c(result, '\n');
            } else if (*p == '\\') {
                g_string_append_c(result, '\\');
            } else {
                g_string_append_c(result, '\\');
                g_string_append_c(result, *p);
            }

            escaping = FALSE;
            continue;
        }

        if (*p == '\\') {
            escaping = TRUE;
            continue;
        }

        g_string_append_c(result, *p);
    }

    if (escaping) {
        set_parse_error(error, "TSV line ends with incomplete escape sequence");
        g_string_free(result, TRUE);
        return NULL;
    }

    return g_string_free(result, FALSE);
}

double parse_size_mb(const char *str)
{
    if (str == NULL || *str == '\0') {
        return 0.0;
    }

    double val = g_ascii_strtod(str, NULL);
    char suffix = '\0';
    const char *p = str;

    while (*p != '\0') {
        if (*p >= 'A' && *p <= 'Z') {
            suffix = *p;
        }
        p++;
    }

    if (suffix == 'G') {
        return val * 1024.0;
    }

    if (suffix == 'M') {
        return val;
    }

    if (suffix == 'K') {
        return val / 1024.0;
    }

    return val;
}

ModuleRepository *module_repo_new(void)
{
    ModuleRepository *repo = g_new0(ModuleRepository, 1);

    repo->modules = g_ptr_array_new();
    repo->by_name = g_hash_table_new(g_str_hash, g_str_equal);

    return repo;
}

void module_repo_clear(ModuleRepository *repo)
{
    if (repo == NULL) {
        return;
    }

    if (repo->modules != NULL) {
        for (guint i = 0; i < repo->modules->len; i++) {
            ModuleInfo *module = g_ptr_array_index(repo->modules, i);
            module_info_free_internal(module);
        }
        g_ptr_array_set_size(repo->modules, 0);
    }

    if (repo->by_name != NULL) {
        g_hash_table_remove_all(repo->by_name);
    }
}

void module_repo_free(ModuleRepository *repo)
{
    if (repo == NULL) {
        return;
    }

    module_repo_clear(repo);
    g_clear_pointer(&repo->modules, g_ptr_array_unref);
    g_clear_pointer(&repo->by_name, g_hash_table_unref);
    g_free(repo);
}

gboolean module_repo_add(ModuleRepository *repo, ModuleInfo *module)
{
    if (repo == NULL || module == NULL || module->name == NULL || module->name[0] == '\0') {
        return FALSE;
    }

    if (g_hash_table_contains(repo->by_name, module->name)) {
        return FALSE;
    }

    g_ptr_array_add(repo->modules, module);
    g_hash_table_insert(repo->by_name, module->name, module);
    return TRUE;
}

ModuleInfo *module_repo_find_by_name(ModuleRepository *repo, const char *name)
{
    if (repo == NULL || name == NULL) {
        return NULL;
    }

    return g_hash_table_lookup(repo->by_name, name);
}

GPtrArray *module_repo_filter(ModuleRepository *repo, const char *query, gboolean match_desc)
{
    GPtrArray *result = g_ptr_array_new();
    gboolean no_query = (query == NULL || query[0] == '\0');

    if (repo == NULL || repo->modules == NULL) {
        return result;
    }

    for (guint i = 0; i < repo->modules->len; i++) {
        ModuleInfo *module = g_ptr_array_index(repo->modules, i);

        if (no_query) {
            g_ptr_array_add(result, module);
            continue;
        }

        if (module->name != NULL && g_strrstr(module->name, query) != NULL) {
            g_ptr_array_add(result, module);
            continue;
        }

        if (match_desc && module->desc != NULL && g_strrstr(module->desc, query) != NULL) {
            g_ptr_array_add(result, module);
        }
    }

    return result;
}

ModuleInfo *module_info_from_tsv_line(const char *line, GError **error)
{
    return module_info_from_tsv_line_for(line, MODULE_TSV_FORMAT_LOADED, error);
}

ModuleInfo *module_info_from_tsv_line_for(const char *line,
                                          ModuleTsvFormat format,
                                          GError **error)
{
    if (line == NULL) {
        g_set_error_literal(error,
                            model_error_quark(),
                            1,
                            "TSV line is NULL");
        return NULL;
    }

    char *line_copy = g_strdup(line);
    line_copy[strcspn(line_copy, "\r\n")] = '\0';

    char **parts = NULL;
    gsize field_count = 0;

    if (!tsv_split_escaped(line_copy,
                           &parts,
                           &field_count,
                           4,
                           6,
                           "TSV line must have 4, 5 or 6 fields",
                           error)) {
        g_free(line_copy);
        return NULL;
    }

    ModuleInfo *module = g_new0(ModuleInfo, 1);
    module->name = (parts[0] != NULL) ? g_strdup(parts[0]) : NULL;

    switch (format) {
        case MODULE_TSV_FORMAT_LOCAL:
            /* name | size | date | path */
            module->layer = (field_count > 1 && parts[1] != NULL) ? g_strdup(parts[1]) : NULL;
            module->size_mb = (field_count > 1) ? parse_size_mb(parts[1]) : 0.0;
            module->version = (field_count > 2 && parts[2] != NULL) ? g_strdup(parts[2]) : NULL;
            module->path = (field_count > 3 && parts[3] != NULL) ? g_strdup(parts[3]) : NULL;
            module->is_local = TRUE;
            break;

        case MODULE_TSV_FORMAT_INET:
            /* name | size | date | desc | category | repo_url */
            module->layer = (field_count > 1 && parts[1] != NULL) ? g_strdup(parts[1]) : NULL;
            module->size_mb = (field_count > 1) ? parse_size_mb(parts[1]) : 0.0;
            module->version = (field_count > 2 && parts[2] != NULL) ? g_strdup(parts[2]) : NULL;
            module->desc = (field_count > 3 && parts[3] != NULL) ? g_strdup(parts[3]) : NULL;
            module->category = (field_count > 4 && parts[4] != NULL) ? g_strdup(parts[4]) : NULL;
            module->repo = (field_count > 5 && parts[5] != NULL) ? g_strdup(parts[5]) : NULL;
            break;

        case MODULE_TSV_FORMAT_LOADED:
        default:
            module->layer = (field_count > 1 && parts[1] != NULL) ? g_strdup(parts[1]) : NULL;
            module->path = (field_count > 2 && parts[2] != NULL) ? g_strdup(parts[2]) : NULL;
            module->desc = (field_count > 3 && parts[3] != NULL) ? g_strdup(parts[3]) : NULL;
            module->size_mb = (field_count > 1) ? parse_size_mb(parts[1]) : 0.0;
            if (field_count > 4 && parts[4] != NULL && parts[4][0] != '\0') {
                module->mount_mode = g_strdup(parts[4]);
            }
            if (field_count > 5 && parts[5] != NULL && parts[5][0] != '\0') {
                module->mount_in_ram = g_strdup(parts[5]);
            }
            break;
    }

    g_strfreev(parts);
    g_free(line_copy);
    return module;
}

static gboolean update_risk_is_valid(const char *risk)
{
    return g_strcmp0(risk, "normal") == 0 ||
           g_strcmp0(risk, "loaded") == 0 ||
           g_strcmp0(risk, "system") == 0;
}

ModuleUpdateInfo *module_update_info_from_tsv_line(const char *line, GError **error)
{
    if (line == NULL) {
        g_set_error_literal(error,
                            model_error_quark(),
                            1,
                            "TSV line is NULL");
        return NULL;
    }

    char *line_copy = g_strdup(line);
    line_copy[strcspn(line_copy, "\r\n")] = '\0';

    char **parts = NULL;
    gsize field_count = 0;

    if (!tsv_split_escaped(line_copy,
                           &parts,
                           &field_count,
                           10,
                           11,
                           "Update TSV line must have 10 or 11 fields",
                           error)) {
        g_free(line_copy);
        return NULL;
    }

    if (g_strcmp0(parts[0], "update") != 0) {
        set_parse_error(error, "Update TSV line must start with 'update'");
        g_strfreev(parts);
        g_free(line_copy);
        return NULL;
    }

    if (parts[1] == NULL || !g_regex_match_simple("^[0-9a-f]{64}$", parts[1], 0, 0)) {
        set_parse_error(error, "Update id must be 64 lowercase hex characters");
        g_strfreev(parts);
        g_free(line_copy);
        return NULL;
    }

    if (!update_risk_is_valid(parts[8])) {
        set_parse_error(error, "Update risk must be normal, loaded or system");
        g_strfreev(parts);
        g_free(line_copy);
        return NULL;
    }

    if (g_strcmp0(parts[9], "0") != 0 && g_strcmp0(parts[9], "1") != 0) {
        set_parse_error(error, "Update reboot_required must be 0 or 1");
        g_strfreev(parts);
        g_free(line_copy);
        return NULL;
    }

    ModuleUpdateInfo *update_info = g_new0(ModuleUpdateInfo, 1);
    update_info->id = g_strdup(parts[1]);
    update_info->name = g_strdup(parts[2]);
    update_info->old_version = g_strdup(parts[3]);
    update_info->new_version = g_strdup(parts[4]);
    update_info->installed_path = g_strdup(parts[5]);
    update_info->candidate_filename = g_strdup(parts[6]);
    update_info->repo = g_strdup(parts[7]);
    update_info->risk = g_strdup(parts[8]);
    update_info->reboot_required = (g_strcmp0(parts[9], "1") == 0) ? TRUE : FALSE;
    update_info->message = g_strdup(field_count > 10 && parts[10] != NULL ? parts[10] : "");

    g_strfreev(parts);
    g_free(line_copy);
    return update_info;
}
