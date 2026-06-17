#include "../include/backend.h"

#if __has_include(<gio/gio.h>)
#include <gio/gio.h>
#endif

#if __has_include(<glib.h>)
#include <glib.h>
#endif

#include "../include/errors.h"
#include "../include/validators.h"

#include <string.h>
#include <unistd.h>

#ifndef MODMAN_BIN
#define MODMAN_BIN "/usr/bin/modman"
#endif

#ifndef MODMAN_AUTOLOAD_HELPER
#define MODMAN_AUTOLOAD_HELPER "/usr/bin/modman-autoload-helper"
#endif

static void set_backend_error(GError **error,
                              ModmanGuiError code,
                              const char *message);
static void set_backend_error_from_subprocess(GError **error,
                                               GError *subprocess_error,
                                               const char *prefix);
static gchar **dup_argv(const char *const *argv);
static gchar **backend_pkexec_argv_dup(const char *const *argv);
static gboolean backend_timeout_cancel_cb(gpointer user_data);
static void backend_parent_cancel_cb(GCancellable *parent, gpointer user_data);
static gchar *backend_open_file_info_normalize_list_field(const char *value);
static void backend_start_async(ModuleRepository *repo,
                                 const char *const *argv,
                                 gboolean expect_list,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);

static GMutex backend_runtime_conf_lock;
static gchar *backend_runtime_modman_bin = NULL;
static gchar *backend_runtime_download_dir = NULL;
static gchar *backend_runtime_cache_file = NULL;

static char *backend_strip_ansi_sequences(const char *text);

static void backend_launcher_apply_runtime_env(GSubprocessLauncher *launcher)
{
    char *runtime_download_dir = NULL;
    char *runtime_cache_file = NULL;

    if (launcher == NULL) {
        return;
    }

    g_mutex_lock(&backend_runtime_conf_lock);
    if (backend_runtime_download_dir != NULL && backend_runtime_download_dir[0] != '\0') {
        runtime_download_dir = g_strdup(backend_runtime_download_dir);
    }
    if (backend_runtime_cache_file != NULL && backend_runtime_cache_file[0] != '\0') {
        runtime_cache_file = g_strdup(backend_runtime_cache_file);
    }
    g_mutex_unlock(&backend_runtime_conf_lock);

    if (g_getenv("DOWNLOAD_DIR") == NULL && runtime_download_dir != NULL) {
        g_subprocess_launcher_setenv(launcher, "DOWNLOAD_DIR", runtime_download_dir, TRUE);
    }
    if (g_getenv("CACHE_FILE") == NULL && runtime_cache_file != NULL) {
        g_subprocess_launcher_setenv(launcher, "CACHE_FILE", runtime_cache_file, TRUE);
    }

    g_free(runtime_download_dir);
    g_free(runtime_cache_file);
}

/* Build a human-readable command string for error messages: basename of
 * argv[0] plus the rest, shell-quoting tokens that are empty or contain
 * whitespace/quotes/backslashes. Caller frees with g_free. */
static gchar *backend_format_argv_for_error(const char *const *argv)
{
    GString *out;
    gsize i;

    if (argv == NULL || argv[0] == NULL) {
        return g_strdup("(пустая команда)");
    }

    out = g_string_new(NULL);
    for (i = 0; argv[i] != NULL; i++) {
        const char *tok = argv[i];
        const char *display;
        const char *p;

        if (i == 0) {
            const char *slash = strrchr(tok, '/');
            display = slash != NULL ? slash + 1 : tok;
        } else {
            display = tok;
        }

        if (i > 0) {
            g_string_append_c(out, ' ');
        }

        if (display[0] == '\0') {
            g_string_append(out, "\"\"");
        } else if (strpbrk(display, " \t\n\"'\\") != NULL) {
            g_string_append_c(out, '"');
            for (p = display; *p != '\0'; p++) {
                if (*p == '"' || *p == '\\') {
                    g_string_append_c(out, '\\');
                }
                g_string_append_c(out, *p);
            }
            g_string_append_c(out, '"');
        } else {
            g_string_append(out, display);
        }
    }

    return g_string_free(out, FALSE);
}

GQuark modman_gui_error_quark(void)
{
    return g_quark_from_static_string("modman-gui");
}

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
    g_free(module->mount_mode);
    g_free(module->mount_in_ram);
    g_free(module);
}

static char *backend_modman_bin_dup(void)
{
    const char *value = g_getenv("MODMAN_BIN");

    if (value != NULL && value[0] != '\0') {
        return g_strdup(value);
    }

    g_mutex_lock(&backend_runtime_conf_lock);

    if (backend_runtime_modman_bin != NULL && backend_runtime_modman_bin[0] != '\0') {
        char *dup = g_strdup(backend_runtime_modman_bin);
        g_mutex_unlock(&backend_runtime_conf_lock);
        return dup;
    }

    g_mutex_unlock(&backend_runtime_conf_lock);
    return g_strdup(MODMAN_BIN);
}

static char *backend_autoload_helper_dup(void)
{
    const char *value = g_getenv("MODMAN_AUTOLOAD_HELPER");

    if (value != NULL && value[0] != '\0') {
        return g_strdup(value);
    }

    return g_strdup(MODMAN_AUTOLOAD_HELPER);
}

static gboolean modman_conf_key_is_known(const char *key)
{
    return g_strcmp0(key, "MODMAN_BIN") == 0 ||
           g_strcmp0(key, "DOWNLOAD_DIR") == 0 ||
           g_strcmp0(key, "CACHE_FILE") == 0 ||
           g_strcmp0(key, "AUFS_INITRD_PREFIX") == 0 ||
           g_strcmp0(key, "AUFS_SYSTEM_PATH") == 0 ||
           g_strcmp0(key, "CMD_LOAD") == 0 ||
           g_strcmp0(key, "CMD_UNLOAD") == 0 ||
           g_strcmp0(key, "IFS_SEP") == 0 ||
            g_strcmp0(key, "LAYERING_OVERRIDE") == 0 ||
            g_strcmp0(key, "PFSLOAD_ALLOW_LOWER") == 0 ||
            g_strcmp0(key, "PFSLOAD_ALLOW_TORAM") == 0 ||
           g_strcmp0(key, "PFSLOAD_ALLOW_LOWER_TORAM") == 0 ||
           g_strcmp0(key, "PFSLOAD_ALLOW_UPPER_TORAM") == 0;
}

static gboolean modman_conf_key_is_valid(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return FALSE;
    }

    if (!(g_ascii_isalpha(key[0]) || key[0] == '_')) {
        return FALSE;
    }

    for (const char *p = key + 1; *p != '\0'; p++) {
        if (!(g_ascii_isalnum(*p) || *p == '_')) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean modman_conf_has_unsafe_substitution(const char *value)
{
    if (value == NULL) {
        return FALSE;
    }

    return strstr(value, "$(") != NULL ||
           strstr(value, "${") != NULL ||
           strchr(value, '`') != NULL;
}

static gboolean modman_conf_trailing_is_comment(const char *tail)
{
    const char *p = tail;

    while (p != NULL && *p != '\0' && g_ascii_isspace(*p)) {
        p++;
    }

    return p == NULL || *p == '\0' || *p == '#';
}

static char *modman_conf_strip_inline_comment(const char *value)
{
    GString *result = g_string_new(NULL);

    if (value == NULL) {
        return g_string_free(result, FALSE);
    }

    for (const char *p = value; *p != '\0'; p++) {
        if (*p == '#' && (p == value || g_ascii_isspace(*(p - 1)))) {
            break;
        }

        g_string_append_c(result, *p);
    }

    return g_strstrip(g_string_free(result, FALSE));
}

static char *modman_conf_parse_shell_single_quoted(const char *value, GError **error)
{
    GString *out = g_string_new(NULL);
    gboolean escaping = FALSE;

    for (const char *p = value + 2; *p != '\0'; p++) {
        if (escaping) {
            switch (*p) {
                case 't':
                    g_string_append_c(out, '\t');
                    break;
                case 'n':
                    g_string_append_c(out, '\n');
                    break;
                case 'r':
                    g_string_append_c(out, '\r');
                    break;
                case '\\':
                    g_string_append_c(out, '\\');
                    break;
                case '\'':
                    g_string_append_c(out, '\'');
                    break;
                default:
                    g_string_append_c(out, *p);
                    break;
            }

            escaping = FALSE;
            continue;
        }

        if (*p == '\\') {
            escaping = TRUE;
            continue;
        }

        if (*p == '\'') {
            if (!modman_conf_trailing_is_comment(p + 1)) {
                g_set_error_literal(error,
                                    MODMAN_GUI_ERROR_DOMAIN,
                                    MODMAN_GUI_ERR_PARSE,
                                    "Некорректное значение в modman.conf");
                g_string_free(out, TRUE);
                return NULL;
            }

            return g_string_free(out, FALSE);
        }

        g_string_append_c(out, *p);
    }

    if (escaping) {
        g_set_error_literal(error,
                            MODMAN_GUI_ERROR_DOMAIN,
                            MODMAN_GUI_ERR_PARSE,
                            "Незавершённая escape-последовательность в modman.conf");
    } else {
        g_set_error_literal(error,
                            MODMAN_GUI_ERROR_DOMAIN,
                            MODMAN_GUI_ERR_PARSE,
                            "Незакрытая quoted-строка в modman.conf");
    }

    g_string_free(out, TRUE);
    return NULL;
}

static char *modman_conf_parse_quoted(const char *value, GError **error)
{
    char quote = value[0];

    for (const char *p = value + 1; *p != '\0'; p++) {
        if (*p != quote) {
            continue;
        }

        if (!modman_conf_trailing_is_comment(p + 1)) {
            g_set_error_literal(error,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_PARSE,
                                "Некорректное значение в modman.conf");
            return NULL;
        }

        return g_strndup(value + 1, (gsize)(p - (value + 1)));
    }

    g_set_error_literal(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_PARSE,
                        "Незакрытая quoted-строка в modman.conf");
    return NULL;
}

static char *modman_conf_parse_value(const char *raw_value, GError **error)
{
    char *trimmed = g_strdup(raw_value != NULL ? raw_value : "");
    char *result = NULL;

    g_strstrip(trimmed);

    if (g_str_has_prefix(trimmed, "$'")) {
        result = modman_conf_parse_shell_single_quoted(trimmed, error);
    } else if (trimmed[0] == '\'' || trimmed[0] == '"') {
        result = modman_conf_parse_quoted(trimmed, error);
    } else {
        result = modman_conf_strip_inline_comment(trimmed);
    }

    g_free(trimmed);

    if (result != NULL && modman_conf_has_unsafe_substitution(result)) {
        g_set_error_literal(error,
                            MODMAN_GUI_ERROR_DOMAIN,
                            MODMAN_GUI_ERR_PARSE,
                            "Unsafe substitution in config value");
        g_free(result);
        return NULL;
    }

    return result;
}

static void modman_conf_assign(char **slot, const char *value)
{
    g_free(*slot);
    *slot = value != NULL ? g_strdup(value) : NULL;
}

void modman_conf_free(ModmanConf *conf)
{
    if (conf == NULL) {
        return;
    }

    g_clear_pointer(&conf->modman_bin, g_free);
    g_clear_pointer(&conf->download_dir, g_free);
    g_clear_pointer(&conf->cache_file, g_free);
    g_clear_pointer(&conf->aufs_initrd_prefix, g_free);
    g_clear_pointer(&conf->aufs_system_path, g_free);
    g_clear_pointer(&conf->cmd_load, g_free);
    g_clear_pointer(&conf->cmd_unload, g_free);
    g_clear_pointer(&conf->ifs_sep, g_free);
    g_clear_pointer(&conf->layering_override, g_free);
    g_clear_pointer(&conf->pfsload_allow_lower, g_free);
    g_clear_pointer(&conf->pfsload_allow_toram, g_free);
    g_clear_pointer(&conf->pfsload_allow_lower_toram, g_free);
    g_clear_pointer(&conf->pfsload_allow_upper_toram, g_free);
}

void modman_conf_clear(ModmanConf *conf)
{
    modman_conf_free(conf);
}

void backend_set_runtime_conf(const ModmanConf *conf)
{
    char *resolved_modman_bin = NULL;
    char *resolved_download_dir = NULL;
    char *resolved_cache_file = NULL;

    if (conf != NULL && conf->modman_bin != NULL && conf->modman_bin[0] != '\0') {
        resolved_modman_bin = g_strdup(conf->modman_bin);
    }
    if (conf != NULL && conf->download_dir != NULL && conf->download_dir[0] != '\0') {
        resolved_download_dir = g_strdup(conf->download_dir);
    }
    if (conf != NULL && conf->cache_file != NULL && conf->cache_file[0] != '\0') {
        resolved_cache_file = g_strdup(conf->cache_file);
    }

    g_mutex_lock(&backend_runtime_conf_lock);
    g_free(backend_runtime_modman_bin);
    g_free(backend_runtime_download_dir);
    g_free(backend_runtime_cache_file);
    backend_runtime_modman_bin = resolved_modman_bin;
    backend_runtime_download_dir = resolved_download_dir;
    backend_runtime_cache_file = resolved_cache_file;
    g_mutex_unlock(&backend_runtime_conf_lock);
}

void backend_reset_runtime_conf(void)
{
    backend_set_runtime_conf(NULL);
}

static void modman_conf_apply_defaults(ModmanConf *conf)
{
    const char *home_dir = g_get_home_dir();

    conf->modman_bin = g_strdup(MODMAN_BIN);
    conf->download_dir = (home_dir != NULL && home_dir[0] != '\0')
                             ? g_build_filename(home_dir, "modman", "modules", NULL)
                             : g_strdup("modman/modules");
    conf->cache_file = NULL;
    conf->aufs_initrd_prefix = g_strdup("/run/archroot/root_ro");
    conf->aufs_system_path = g_strdup("/mnt/.");
    conf->ifs_sep = g_strdup("|");
}

static char *modman_conf_resolve_path(void)
{
    const char *env_path = g_getenv("MODMAN_CONF");
    const char *xdg_config_home = g_getenv("XDG_CONFIG_HOME");
    const char *home_dir = g_get_home_dir();
    char *candidate = NULL;

    if (env_path != NULL && env_path[0] != '\0') {
        return g_strdup(env_path);
    }

    if (xdg_config_home != NULL && xdg_config_home[0] != '\0') {
        candidate = g_build_filename(xdg_config_home, "modman", "modman.conf", NULL);
        if (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
            return candidate;
        }
        g_free(candidate);
    }

    if (home_dir != NULL && home_dir[0] != '\0') {
        candidate = g_build_filename(home_dir, ".config", "modman", "modman.conf", NULL);
        if (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
            return candidate;
        }
        g_free(candidate);
    }

    if (g_file_test("/etc/modman.conf", G_FILE_TEST_EXISTS)) {
        return g_strdup("/etc/modman.conf");
    }

    if (g_file_test("/usr/share/modman/modman.conf", G_FILE_TEST_EXISTS)) {
        return g_strdup("/usr/share/modman/modman.conf");
    }

    return NULL;
}

static gboolean modman_conf_parse_contents(ModmanConf *conf,
                                           const char *path,
                                           char *contents,
                                           GError **error)
{
    gchar **lines = g_strsplit(contents != NULL ? contents : "", "\n", -1);
    gboolean skipping_array = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strstrip(lines[i]);

        if (skipping_array) {
            if (g_strcmp0(line, ")") == 0) {
                skipping_array = FALSE;
            }
            continue;
        }

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == NULL) {
            g_set_error(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_PARSE,
                        "%s:%u: ожидался формат KEY=VALUE",
                        path,
                        i + 1);
            g_strfreev(lines);
            return FALSE;
        }

        *eq = '\0';
        char *key = g_strstrip(line);
        char *raw_value = g_strstrip(eq + 1);

        if (!modman_conf_key_is_valid(key)) {
            g_set_error(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_PARSE,
                        "%s:%u: некорректный ключ '%s'",
                        path,
                        i + 1,
                        key);
            g_strfreev(lines);
            return FALSE;
        }

        if (raw_value[0] == '(') {
            if (modman_conf_key_is_known(key)) {
                g_set_error(error,
                            MODMAN_GUI_ERROR_DOMAIN,
                            MODMAN_GUI_ERR_PARSE,
                            "%s:%u: ключ %s должен быть строкой, а не массивом",
                            path,
                            i + 1,
                            key);
                g_strfreev(lines);
                return FALSE;
            }

            if (strchr(raw_value, ')') == NULL) {
                skipping_array = TRUE;
            }
            continue;
        }

        GError *value_error = NULL;
        char *parsed_value = modman_conf_parse_value(raw_value, &value_error);
        if (parsed_value == NULL) {
            g_propagate_prefixed_error(error,
                                       value_error,
                                       "%s:%u (%s): ",
                                       path,
                                       i + 1,
                                       key);
            g_strfreev(lines);
            return FALSE;
        }

        if (g_strcmp0(key, "MODMAN_BIN") == 0) {
            modman_conf_assign(&conf->modman_bin, parsed_value);
        } else if (g_strcmp0(key, "DOWNLOAD_DIR") == 0) {
            modman_conf_assign(&conf->download_dir, parsed_value);
        } else if (g_strcmp0(key, "CACHE_FILE") == 0) {
            modman_conf_assign(&conf->cache_file, parsed_value);
        } else if (g_strcmp0(key, "AUFS_INITRD_PREFIX") == 0) {
            modman_conf_assign(&conf->aufs_initrd_prefix, parsed_value);
        } else if (g_strcmp0(key, "AUFS_SYSTEM_PATH") == 0) {
            modman_conf_assign(&conf->aufs_system_path, parsed_value);
        } else if (g_strcmp0(key, "CMD_LOAD") == 0) {
            modman_conf_assign(&conf->cmd_load, parsed_value);
        } else if (g_strcmp0(key, "CMD_UNLOAD") == 0) {
            modman_conf_assign(&conf->cmd_unload, parsed_value);
        } else if (g_strcmp0(key, "IFS_SEP") == 0) {
            modman_conf_assign(&conf->ifs_sep, parsed_value);
        } else if (g_strcmp0(key, "LAYERING_OVERRIDE") == 0) {
            modman_conf_assign(&conf->layering_override, parsed_value);
        } else if (g_strcmp0(key, "PFSLOAD_ALLOW_LOWER") == 0) {
            g_free(conf->pfsload_allow_lower);
            conf->pfsload_allow_lower = parsed_value[0] != '\0' ? g_strdup(parsed_value) : NULL;
        } else if (g_strcmp0(key, "PFSLOAD_ALLOW_TORAM") == 0) {
            g_free(conf->pfsload_allow_toram);
            conf->pfsload_allow_toram = parsed_value[0] != '\0' ? g_strdup(parsed_value) : NULL;
        } else if (g_strcmp0(key, "PFSLOAD_ALLOW_LOWER_TORAM") == 0) {
            g_free(conf->pfsload_allow_lower_toram);
            conf->pfsload_allow_lower_toram = parsed_value[0] != '\0' ? g_strdup(parsed_value) : NULL;
        } else if (g_strcmp0(key, "PFSLOAD_ALLOW_UPPER_TORAM") == 0) {
            g_free(conf->pfsload_allow_upper_toram);
            conf->pfsload_allow_upper_toram = parsed_value[0] != '\0' ? g_strdup(parsed_value) : NULL;
        }

        g_free(parsed_value);
    }

    if (skipping_array) {
        g_set_error(error,
                    MODMAN_GUI_ERROR_DOMAIN,
                    MODMAN_GUI_ERR_PARSE,
                    "%s: незавершённый массив в modman.conf",
                    path);
        g_strfreev(lines);
        return FALSE;
    }

    g_strfreev(lines);
    return TRUE;
}

gboolean modman_conf_load(ModmanConf *out_conf, GError **error)
{
    char *path = NULL;
    char *contents = NULL;
    GError *read_error = NULL;

    if (out_conf == NULL) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "modman_conf_load: out_conf is NULL");
        return FALSE;
    }

    modman_conf_free(out_conf);
    modman_conf_apply_defaults(out_conf);

    path = modman_conf_resolve_path();
    if (path == NULL) {
        return TRUE;
    }

    if (!g_file_get_contents(path, &contents, NULL, &read_error)) {
        if (read_error != NULL &&
            g_error_matches(read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(&read_error);
            g_free(path);
            return TRUE;
        }

        g_propagate_prefixed_error(error, read_error, "%s: ", path);
        g_free(path);
        return FALSE;
    }

    if (!modman_conf_parse_contents(out_conf, path, contents, error)) {
        modman_conf_free(out_conf);
        g_free(contents);
        g_free(path);
        return FALSE;
    }

    g_free(contents);
    g_free(path);
    return TRUE;
}

static void set_backend_error(GError **error,
                              ModmanGuiError code,
                              const char *message)
{
    g_set_error_literal(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        code,
                        message != NULL ? message : "backend error");
}

static void set_backend_error_from_subprocess(GError **error,
                                              GError *subprocess_error,
                                              const char *prefix)
{
    if (subprocess_error != NULL &&
        g_error_matches(subprocess_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_set_error(error,
                    MODMAN_GUI_ERROR_DOMAIN,
                    MODMAN_GUI_ERR_BACKEND_MISSING,
                    "%s: %s",
                    prefix,
                    subprocess_error->message);
        return;
    }

    g_set_error(error,
                MODMAN_GUI_ERROR_DOMAIN,
                MODMAN_GUI_ERR_IO,
                "%s: %s",
                prefix,
                subprocess_error != NULL ? subprocess_error->message : "I/O error");
}

static GPtrArray *parse_machine_tsv_output_for(const char *stdout_text,
                                               ModuleTsvFormat format,
                                               GError **error)
{
    GPtrArray *items = g_ptr_array_new_with_free_func((GDestroyNotify)module_info_free_internal);

    if (stdout_text == NULL || stdout_text[0] == '\0') {
        return items;
    }

    gchar **lines = g_strsplit(stdout_text, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0') {
            continue;
        }

        if (g_str_has_prefix(lines[i], "warn\t")) {
            continue;
        }

        GError *parse_error = NULL;
        ModuleInfo *module = module_info_from_tsv_line_for(lines[i], format, &parse_error);
        if (module == NULL) {
            g_set_error(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_PARSE,
                        "Некорректная строка machine-вывода: %s",
                        parse_error != NULL ? parse_error->message : "unknown parse error");
            g_clear_error(&parse_error);
            g_strfreev(lines);
            g_ptr_array_unref(items);
            return NULL;
        }

        g_ptr_array_add(items, module);
    }

    g_strfreev(lines);
    return items;
}

static gboolean run_machine_list_sync_for(const char *const *argv,
                                          ModuleTsvFormat format,
                                          GPtrArray **out_items,
                                          GError **error)
{
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    int exit_status = -1;

    if (!backend_call_sync(argv, &stdout_text, &stderr_text, &exit_status, error)) {
        g_free(stdout_text);
        g_free(stderr_text);
        return FALSE;
    }

    GPtrArray *items = parse_machine_tsv_output_for(stdout_text, format, error);
    g_free(stdout_text);
    g_free(stderr_text);

    if (items == NULL) {
        return FALSE;
    }

    *out_items = items;
    return TRUE;
}

static gboolean run_machine_list_sync(const char *const *argv,
                                      GPtrArray **out_items,
                                      GError **error)
{
    return run_machine_list_sync_for(argv, MODULE_TSV_FORMAT_LOADED, out_items, error);
}

static gboolean backend_open_path_is_absolute_pfs(const char *absolute_path)
{
    return absolute_path != NULL &&
           absolute_path[0] != '\0' &&
           g_path_is_absolute(absolute_path) &&
           g_str_has_suffix(absolute_path, ".pfs");
}

static BackendOpenFileInfo *backend_open_file_info_parse_row(const char *stdout_text,
                                                              GError **error)
{
    gchar **lines = NULL;
    gchar **fields = NULL;
    gchar *payload = NULL;
    const char *row = NULL;
    guint row_count = 0;
    guint field_count = 0;
    BackendOpenFileInfo *info = NULL;

    if (stdout_text == NULL || stdout_text[0] == '\0') {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "Пустой machine-ответ file-info");
        return NULL;
    }

    lines = g_strsplit(stdout_text, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0') {
            continue;
        }
        row = lines[i];
        row_count++;
    }

    if (row_count != 1 || row == NULL) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "Ожидалась одна строка machine-ответа file-info");
        g_strfreev(lines);
        return NULL;
    }

    fields = g_strsplit(row, "\t", -1);
    while (fields[field_count] != NULL) {
        field_count++;
    }

    if (field_count == 2 && g_strcmp0(fields[0], "error") == 0) {
        payload = g_strdup(fields[1]);
        set_backend_error(error,
                          MODMAN_GUI_ERR_IO,
                          payload);
        g_free(payload);
        g_strfreev(fields);
        g_strfreev(lines);
        return NULL;
    }

    if ((field_count < 5 || field_count > 9) || g_strcmp0(fields[0], "file") != 0) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "Некорректная строка machine file-info");
        g_strfreev(fields);
        g_strfreev(lines);
        return NULL;
    }

    if (fields[1][0] == '\0' || fields[2][0] == '\0' ||
        fields[3][0] == '\0' || fields[4][0] == '\0') {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "В machine file-info отсутствуют обязательные поля");
        g_strfreev(fields);
        g_strfreev(lines);
        return NULL;
    }

    if (g_strcmp0(fields[4], "loaded") != 0 && g_strcmp0(fields[4], "local") != 0) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_PARSE,
                          "Некорректное состояние в machine file-info");
        g_strfreev(fields);
        g_strfreev(lines);
        return NULL;
    }

    info = g_new0(BackendOpenFileInfo, 1);
    info->path = g_strdup(fields[1]);
    info->name = g_strdup(fields[2]);
    info->format = g_strdup(fields[3]);
    info->loaded_state = g_strdup(fields[4]);
    info->size = field_count > 5 ? g_strdup(fields[5]) : NULL;
    info->format_detail = field_count > 6 ? g_strdup(fields[6]) : NULL;
    info->modules = field_count > 7 ? backend_open_file_info_normalize_list_field(fields[7]) : NULL;
    info->dependencies = field_count > 8 ? backend_open_file_info_normalize_list_field(fields[8]) : NULL;
    info->is_loaded = g_strcmp0(fields[4], "loaded") == 0;

    g_strfreev(fields);
    g_strfreev(lines);
    return info;
}

static gchar *backend_open_file_info_normalize_list_field(const char *value)
{
    gchar **parts;
    GString *normalized;

    if (value == NULL) {
        return NULL;
    }

    parts = g_strsplit_set(value, " \r\n", -1);
    normalized = g_string_new(NULL);

    for (guint i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0') {
            continue;
        }

        if (normalized->len > 0) {
            g_string_append_c(normalized, '\n');
        }
        g_string_append(normalized, parts[i]);
    }

    g_strfreev(parts);
    return g_string_free(normalized, FALSE);
}

void backend_open_file_info_free(BackendOpenFileInfo *info)
{
    if (info == NULL) {
        return;
    }

    g_free(info->path);
    g_free(info->name);
    g_free(info->format);
    g_free(info->loaded_state);
    g_free(info->size);
    g_free(info->format_detail);
    g_free(info->modules);
    g_free(info->dependencies);
    g_free(info);
}

gchar *backend_open_unload_name_from_path(const char *absolute_path,
                                          GError **error)
{
    gchar *basename = NULL;
    gchar *name = NULL;
    gsize len;

    if (!backend_open_path_is_absolute_pfs(absolute_path)) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Ожидался абсолютный путь к .pfs файлу");
        return NULL;
    }

    basename = g_path_get_basename(absolute_path);
    len = strlen(basename);
    if (len <= 4 || !g_str_has_suffix(basename, ".pfs")) {
        g_free(basename);
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Некорректное имя .pfs файла");
        return NULL;
    }

    name = g_strndup(basename, len - 4);
    g_free(basename);

    if (!module_name_is_valid(name)) {
        g_free(name);
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Некорректное имя модуля для отключения");
        return NULL;
    }

    return name;
}

gboolean backend_call_sync(const char *const *argv,
                           char **stdout_out,
                           char **stderr_out,
                           int *exit_status,
                           GError **error)
{
    if (stdout_out != NULL) {
        *stdout_out = NULL;
    }

    if (stderr_out != NULL) {
        *stderr_out = NULL;
    }

    if (exit_status != NULL) {
        *exit_status = -1;
    }

    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        set_backend_error(error, MODMAN_GUI_ERR_INVALID_NAME, "backend_call_sync: empty argv");
        return FALSE;
    }

    GError *subprocess_error = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess *proc;

    backend_launcher_apply_runtime_env(launcher);
    proc = g_subprocess_launcher_spawnv(launcher,
                                        (const gchar *const *)argv,
                                        &subprocess_error);
    g_object_unref(launcher);
    if (proc == NULL) {
        set_backend_error_from_subprocess(error,
                                          subprocess_error,
                                          "Не удалось запустить modman");
        g_clear_error(&subprocess_error);
        return FALSE;
    }

    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;
    if (!g_subprocess_communicate(proc,
                                  NULL,
                                  NULL,
                                  &stdout_bytes,
                                  &stderr_bytes,
                                  &subprocess_error)) {
        set_backend_error_from_subprocess(error,
                                          subprocess_error,
                                          "Ошибка чтения ответа modman");
        g_clear_error(&subprocess_error);
        g_object_unref(proc);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        return FALSE;
    }

    gsize stdout_len = 0;
    gsize stderr_len = 0;
    const char *stdout_data = stdout_bytes != NULL ? g_bytes_get_data(stdout_bytes, &stdout_len) : "";
    const char *stderr_data = stderr_bytes != NULL ? g_bytes_get_data(stderr_bytes, &stderr_len) : "";

    char *stdout_dup = g_strndup(stdout_data, stdout_len);
    char *stderr_dup = g_strndup(stderr_data, stderr_len);

    if (exit_status != NULL && g_subprocess_get_if_exited(proc)) {
        *exit_status = g_subprocess_get_exit_status(proc);
    }

    if (!g_subprocess_get_if_exited(proc)) {
        set_backend_error(error, MODMAN_GUI_ERR_UNKNOWN, "Процесс modman завершился некорректно");
        g_free(stdout_dup);
        g_free(stderr_dup);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(proc);
        return FALSE;
    }

    gint status_code = g_subprocess_get_exit_status(proc);
    if (status_code != 0) {
        gchar *cmd_str = backend_format_argv_for_error(argv);
        char *clean_error = backend_strip_ansi_sequences(stderr_dup != NULL && stderr_dup[0] != '\0'
                                                             ? stderr_dup
                                                             : "без stderr");
        if (exit_status != NULL) {
            *exit_status = status_code;
        }
        g_set_error(error,
                    MODMAN_GUI_ERROR_DOMAIN,
                    MODMAN_GUI_ERR_IO,
                    "%s завершился с кодом %d: %s",
                    cmd_str,
                    status_code,
                    clean_error);
        g_free(clean_error);
        g_free(cmd_str);
        g_free(stdout_dup);
        g_free(stderr_dup);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(proc);
        return FALSE;
    }

    if (stdout_out != NULL) {
        *stdout_out = stdout_dup;
    } else {
        g_free(stdout_dup);
    }

    if (stderr_out != NULL) {
        *stderr_out = stderr_dup;
    } else {
        g_free(stderr_dup);
    }

    if (stdout_bytes != NULL) {
        g_bytes_unref(stdout_bytes);
    }
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }
    g_object_unref(proc);
    return TRUE;
}

static gboolean backend_call_sync_capture_machine(const char *const *argv,
                                                  char **stdout_out,
                                                  char **stderr_out,
                                                  int *exit_status,
                                                  GError **error)
{
    GError *subprocess_error = NULL;
    GSubprocessLauncher *launcher;
    GSubprocess *proc;
    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;
    gsize stdout_len = 0;
    gsize stderr_len = 0;
    const char *stdout_data;
    const char *stderr_data;

    if (stdout_out != NULL) {
        *stdout_out = NULL;
    }
    if (stderr_out != NULL) {
        *stderr_out = NULL;
    }
    if (exit_status != NULL) {
        *exit_status = -1;
    }

    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        set_backend_error(error, MODMAN_GUI_ERR_INVALID_NAME, "backend_call_sync_capture_machine: empty argv");
        return FALSE;
    }

    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    backend_launcher_apply_runtime_env(launcher);
    proc = g_subprocess_launcher_spawnv(launcher,
                                        (const gchar *const *)argv,
                                        &subprocess_error);
    g_object_unref(launcher);
    if (proc == NULL) {
        set_backend_error_from_subprocess(error,
                                          subprocess_error,
                                          "Не удалось запустить modman");
        g_clear_error(&subprocess_error);
        return FALSE;
    }

    if (!g_subprocess_communicate(proc,
                                  NULL,
                                  NULL,
                                  &stdout_bytes,
                                  &stderr_bytes,
                                  &subprocess_error)) {
        set_backend_error_from_subprocess(error,
                                          subprocess_error,
                                          "Ошибка чтения ответа modman");
        g_clear_error(&subprocess_error);
        g_object_unref(proc);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        return FALSE;
    }

    if (!g_subprocess_get_if_exited(proc)) {
        set_backend_error(error, MODMAN_GUI_ERR_UNKNOWN, "Процесс modman завершился некорректно");
        g_object_unref(proc);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        return FALSE;
    }

    stdout_data = stdout_bytes != NULL ? g_bytes_get_data(stdout_bytes, &stdout_len) : "";
    stderr_data = stderr_bytes != NULL ? g_bytes_get_data(stderr_bytes, &stderr_len) : "";

    if (stdout_out != NULL) {
        *stdout_out = g_strndup(stdout_data, stdout_len);
    }
    if (stderr_out != NULL) {
        *stderr_out = g_strndup(stderr_data, stderr_len);
    }
    if (exit_status != NULL) {
        *exit_status = g_subprocess_get_exit_status(proc);
    }

    g_object_unref(proc);
    if (stdout_bytes != NULL) {
        g_bytes_unref(stdout_bytes);
    }
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }
    return TRUE;
}

GPtrArray *backend_list_loaded_sync(const char *filter, GError **error)
{
    const char *effective_filter = (filter != NULL && filter[0] != '\0') ? filter : "after";

    if (g_strcmp0(effective_filter, "after") != 0 &&
        g_strcmp0(effective_filter, "before") != 0) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Допустимые фильтры: after | before");
        return NULL;
    }

    gchar *cmd = g_strdup_printf("--list-loaded-%s", effective_filter);
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        cmd,
        "--machine",
        NULL
    };

    GPtrArray *items = NULL;
    gboolean ok = run_machine_list_sync(argv, &items, error);
    g_free(modman_bin);
    g_free(cmd);

    return ok ? items : NULL;
}

GPtrArray *backend_list_local_sync(GError **error)
{
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "--list-local",
        "--machine",
        NULL
    };

    GPtrArray *items = NULL;
    if (!run_machine_list_sync_for(argv, MODULE_TSV_FORMAT_LOCAL, &items, error)) {
        g_free(modman_bin);
        return NULL;
    }

    g_free(modman_bin);
    return items;
}

GPtrArray *backend_search_sync(const char *query, GError **error)
{
    if (!search_query_is_valid(query)) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Некорректный поисковый запрос");
        return NULL;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-Ss",
        query,
        "--machine",
        NULL
    };

    GPtrArray *items = NULL;
    if (!run_machine_list_sync_for(argv, MODULE_TSV_FORMAT_INET, &items, error)) {
        g_free(modman_bin);
        return NULL;
    }

    g_free(modman_bin);
    return items;
}

gboolean backend_load_sync(const char *name, GError **error)
{
    gboolean is_local_path = name != NULL && strchr(name, '/') != NULL && g_str_has_suffix(name, ".pfs");

    if (!is_local_path && !module_name_is_valid(name)) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Некорректное имя модуля для подключения");
        return FALSE;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-S",
        name,
        NULL
    };
    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    gboolean ok = backend_call_sync((const char *const *)pkexec_argv, NULL, NULL, NULL, error);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
    return ok;
}

gboolean backend_unload_sync(const char *name, GError **error)
{
    if (!module_name_is_valid(name)) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Некорректное имя модуля для отключения");
        return FALSE;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-R",
        name,
        NULL
    };
    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    gboolean ok = backend_call_sync((const char *const *)pkexec_argv, NULL, NULL, NULL, error);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
    return ok;
}

gboolean backend_sync_db_sync(GError **error)
{
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-Sy",
        NULL
    };
    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    gboolean ok = backend_call_sync((const char *const *)pkexec_argv, NULL, NULL, NULL, error);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
    return ok;
}

BackendOpenFileInfo *backend_open_file_info_sync(const char *absolute_path,
                                                 GError **error)
{
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    int exit_status = -1;
    GError *call_error = NULL;
    BackendOpenFileInfo *info = NULL;
    char *modman_bin;
    const char *argv[] = {
        NULL,
        "--machine",
        "-Qp",
        absolute_path,
        NULL
    };

    if (!backend_open_path_is_absolute_pfs(absolute_path)) {
        set_backend_error(error,
                          MODMAN_GUI_ERR_INVALID_NAME,
                          "Ожидался абсолютный путь к .pfs файлу");
        return NULL;
    }

    modman_bin = backend_modman_bin_dup();
    argv[0] = modman_bin;

    if (!backend_call_sync_capture_machine(argv,
                                           &stdout_text,
                                           &stderr_text,
                                           &exit_status,
                                           &call_error)) {
        g_free(modman_bin);
        g_free(stdout_text);
        g_free(stderr_text);
        if (call_error != NULL) {
            g_propagate_error(error, call_error);
        }
        return NULL;
    }

    if (exit_status != 0) {
        gchar *cmd_str = backend_format_argv_for_error(argv);

        if (stdout_text != NULL && g_str_has_prefix(stdout_text, "error\t")) {
            info = backend_open_file_info_parse_row(stdout_text, error);
        } else {
            char *clean_error = backend_strip_ansi_sequences(stderr_text != NULL && stderr_text[0] != '\0'
                                                                 ? stderr_text
                                                                 : (stdout_text != NULL && stdout_text[0] != '\0'
                                                                        ? stdout_text
                                                                        : "пустой stderr"));
            g_set_error(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_IO,
                        "%s завершился с кодом %d: %s",
                        cmd_str,
                        exit_status,
                        clean_error);
            g_free(clean_error);
        }

        g_free(cmd_str);
        g_free(modman_bin);
        g_free(stdout_text);
        g_free(stderr_text);
        return NULL;
    }

    info = backend_open_file_info_parse_row(stdout_text, error);
    if (info == NULL && error != NULL && *error == NULL && exit_status != 0) {
        gchar *cmd_str = backend_format_argv_for_error(argv);
        char *clean_error = backend_strip_ansi_sequences(stderr_text != NULL && stderr_text[0] != '\0'
                                                             ? stderr_text
                                                             : "без stderr");
        g_set_error(error,
                    MODMAN_GUI_ERROR_DOMAIN,
                    MODMAN_GUI_ERR_IO,
                    "%s завершился с кодом %d: %s",
                    cmd_str,
                    exit_status,
                    clean_error);
        g_free(clean_error);
        g_free(cmd_str);
    }
    g_free(modman_bin);
    g_free(stdout_text);
    g_free(stderr_text);
    return info;
}

void backend_sync_db_async(ModuleRepository *repo,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-Sy",
        NULL
    };
    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE, cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
}

gboolean backend_sync_db_finish(ModuleRepository *repo,
                                GAsyncResult *result,
                                GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void backend_open_file_info_thread_fn(GTask *task,
                                             gpointer source_object,
                                             gpointer task_data,
                                             GCancellable *cancellable)
{
    const char *absolute_path = (const char *)task_data;
    BackendOpenFileInfo *info;
    GError *error = NULL;

    (void)source_object;

    if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_CANCELLED,
                                "%s",
                                "Операция была отменена");
        return;
    }

    info = backend_open_file_info_sync(absolute_path, &error);
    if (info == NULL) {
        g_task_return_error(task, error);
        return;
    }

    if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
        backend_open_file_info_free(info);
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_CANCELLED,
                                "%s",
                                "Операция была отменена");
        return;
    }

    g_task_return_pointer(task, info, (GDestroyNotify)backend_open_file_info_free);
}

void backend_open_file_info_async(const char *absolute_path,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GTask *task;

    task = g_task_new(NULL, cancellable, callback, user_data);
    g_task_set_task_data(task,
                         g_strdup(absolute_path != NULL ? absolute_path : ""),
                         g_free);
    g_task_run_in_thread(task, backend_open_file_info_thread_fn);
    g_object_unref(task);
}

BackendOpenFileInfo *backend_open_file_info_finish(GAsyncResult *result,
                                                   GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return (BackendOpenFileInfo *)g_task_propagate_pointer(G_TASK(result), error);
}

void backend_open_attach_path_with_modes_async(const char *absolute_path,
                                               gboolean lower_layer,
                                               gboolean copy_to_ram,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    char *modman_bin;
    GPtrArray *argv_arr;
    gchar **pkexec_argv;

    if (!backend_open_path_is_absolute_pfs(absolute_path)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_open_attach_path_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Ожидался абсолютный путь к .pfs файлу");
        return;
    }

    modman_bin = backend_modman_bin_dup();
    argv_arr = g_ptr_array_new_with_free_func(NULL);
    g_ptr_array_add(argv_arr, modman_bin);
    g_ptr_array_add(argv_arr, (gpointer)"--machine");
    if (lower_layer) {
        g_ptr_array_add(argv_arr, (gpointer)"--lower");
    }
    if (copy_to_ram) {
        g_ptr_array_add(argv_arr, (gpointer)"--toram");
    }
    g_ptr_array_add(argv_arr, (gpointer)"-S");
    g_ptr_array_add(argv_arr, (gpointer)absolute_path);
    g_ptr_array_add(argv_arr, NULL);

    pkexec_argv = backend_pkexec_argv_dup((const char *const *)argv_arr->pdata);
    g_ptr_array_unref(argv_arr);
    backend_start_async(NULL, (const char *const *)pkexec_argv, FALSE,
                        cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
}

void backend_open_attach_path_async(const char *absolute_path,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    backend_open_attach_path_with_modes_async(absolute_path,
                                              FALSE,
                                              FALSE,
                                              cancellable,
                                              callback,
                                              user_data);
}

gboolean backend_open_attach_path_finish(GAsyncResult *result,
                                         GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

void backend_open_unload_path_async(const char *absolute_path,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    char *modman_bin;
    gchar *name;
    GError *error = NULL;
    const char *argv[] = {
        NULL,
        "--machine",
        "-R",
        NULL,
        NULL
    };
    gchar **pkexec_argv;

    name = backend_open_unload_name_from_path(absolute_path, &error);
    if (name == NULL) {
        g_task_report_error(NULL,
                            callback,
                            user_data,
                            backend_open_unload_path_async,
                            error);
        return;
    }

    modman_bin = backend_modman_bin_dup();
    argv[0] = modman_bin;
    argv[3] = name;
    pkexec_argv = backend_pkexec_argv_dup(argv);
    backend_start_async(NULL, (const char *const *)pkexec_argv, FALSE,
                        cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
    g_free(name);
}

gboolean backend_open_unload_path_finish(GAsyncResult *result,
                                         GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static char *backend_strip_ansi_sequences(const char *text)
{
    GString *clean;
    const char *p;

    if (text == NULL) {
        return g_strdup("");
    }

    clean = g_string_new(NULL);
    p = text;
    while (*p != '\0') {
        if (g_str_has_prefix(p, "\342\220\233")) {
            p += 3;
            if (*p == '[') {
                p++;
                while (*p != '\0' && !((*p >= '@' && *p <= '~'))) {
                    p++;
                }
                if (*p != '\0') {
                    p++;
                }
            }
            continue;
        }

        if ((guchar)*p == 0x1b) {
            p++;
            if (*p == '[') {
                p++;
                while (*p != '\0' && !((*p >= '@' && *p <= '~'))) {
                    p++;
                }
                if (*p != '\0') {
                    p++;
                }
                continue;
            }
            if (*p == ']') {
                p++;
                while (*p != '\0' && *p != '\a') {
                    if ((guchar)*p == 0x1b && *(p + 1) == '\\') {
                        p += 2;
                        break;
                    }
                    p++;
                }
                if (*p == '\a') {
                    p++;
                }
                continue;
            }
            if (*p != '\0') {
                p++;
            }
            continue;
        }

        g_string_append_c(clean, *p);
        p++;
    }

    return g_string_free(clean, FALSE);
}

typedef struct {
    char *module_name;
} BackendDepsCall;

static void backend_deps_call_free(BackendDepsCall *call)
{
    if (call == NULL) {
        return;
    }
    g_free(call->module_name);
    g_free(call);
}

static void backend_get_deps_thread_fn(GTask *task,
                                       gpointer source_object,
                                       gpointer task_data,
                                       GCancellable *cancellable)
{
    BackendDepsCall *call = (BackendDepsCall *)task_data;
    GError *error = NULL;
    char *modman_bin = NULL;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    int exit_status = 0;
    gchar **lines = NULL;
    GPtrArray *filtered = NULL;
    gchar **result_arr = NULL;
    gsize i;

    (void)source_object;
    (void)cancellable;

    modman_bin = backend_modman_bin_dup();
    {
        const char *argv[] = {
            modman_bin,
            "--deps",
            call->module_name,
            "--machine",
            NULL
        };

        if (!backend_call_sync(argv, &stdout_text, &stderr_text, &exit_status, &error)) {
            g_free(modman_bin);
            g_free(stdout_text);
            g_free(stderr_text);
            g_task_return_error(task, error);
            return;
        }
    }
    g_free(modman_bin);
    g_free(stderr_text);

    lines = g_strsplit(stdout_text != NULL ? stdout_text : "", "\n", -1);
    g_free(stdout_text);

    filtered = g_ptr_array_new_with_free_func(g_free);
    for (i = 0; lines[i] != NULL; i++) {
        char *trimmed = g_strdup(lines[i]);
        g_strstrip(trimmed);
        if (trimmed[0] != '\0') {
            g_ptr_array_add(filtered, trimmed);
        } else {
            g_free(trimmed);
        }
    }
    g_strfreev(lines);
    g_ptr_array_add(filtered, NULL);
    result_arr = (gchar **)g_ptr_array_free(filtered, FALSE);

    g_task_return_pointer(task, result_arr, (GDestroyNotify)g_strfreev);
}

void backend_get_deps_async(const char *module_name,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);
    BackendDepsCall *call;

    if (module_name == NULL || module_name[0] == '\0') {
        g_task_return_new_error(task, MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s", "Имя модуля не указано");
        g_object_unref(task);
        return;
    }

    call = g_new0(BackendDepsCall, 1);
    call->module_name = g_strdup(module_name);
    g_task_set_task_data(task, call, (GDestroyNotify)backend_deps_call_free);
    g_task_run_in_thread(task, backend_get_deps_thread_fn);
    g_object_unref(task);
}

gchar **backend_get_deps_finish(GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return (gchar **)g_task_propagate_pointer(G_TASK(result), error);
}

typedef struct {
    gboolean expect_list;
    ModuleTsvFormat tsv_format;
    gchar **argv;
    GSubprocess *proc;
    GCancellable *op_cancellable;
    GCancellable *parent_cancellable;
    gulong parent_cancel_handler;
    guint timeout_source_id;
    char *stdout_for_parse;
} BackendAsyncRequest;

typedef struct {
    gboolean is_query;
    gchar **argv;
    GSubprocess *proc;
    GCancellable *op_cancellable;
    GCancellable *parent_cancellable;
    gulong parent_cancel_handler;
    guint timeout_source_id;
} BackendAutoloadRequest;

typedef struct {
    GPtrArray *results;
} BackendRemoveLocalTaskData;

static void backend_async_request_stop_controls(BackendAsyncRequest *request)
{
    if (request == NULL) {
        return;
    }

    if (request->timeout_source_id != 0) {
        GSource *src = g_main_context_find_source_by_id(NULL, request->timeout_source_id);
        if (src != NULL) {
            g_source_remove(request->timeout_source_id);
        }
        request->timeout_source_id = 0;
    }

    if (request->parent_cancellable != NULL && request->parent_cancel_handler != 0) {
        g_cancellable_disconnect(request->parent_cancellable, request->parent_cancel_handler);
        request->parent_cancel_handler = 0;
    }
}

static void backend_async_request_free(BackendAsyncRequest *request)
{
    if (request == NULL) {
        return;
    }

    backend_async_request_stop_controls(request);

    if (request->argv != NULL) {
        g_strfreev(request->argv);
    }

    if (request->proc != NULL) {
        g_object_unref(request->proc);
    }

    if (request->op_cancellable != NULL) {
        g_object_unref(request->op_cancellable);
    }

    if (request->parent_cancellable != NULL) {
        g_object_unref(request->parent_cancellable);
    }

    g_free(request->stdout_for_parse);
    g_free(request);
}

static void backend_autoload_request_stop_controls(BackendAutoloadRequest *request)
{
    if (request == NULL) {
        return;
    }

    if (request->timeout_source_id != 0) {
        GSource *src = g_main_context_find_source_by_id(NULL, request->timeout_source_id);
        if (src != NULL) {
            g_source_remove(request->timeout_source_id);
        }
        request->timeout_source_id = 0;
    }

    if (request->parent_cancellable != NULL && request->parent_cancel_handler != 0) {
        g_cancellable_disconnect(request->parent_cancellable, request->parent_cancel_handler);
        request->parent_cancel_handler = 0;
    }
}

static void backend_autoload_request_free(BackendAutoloadRequest *request)
{
    if (request == NULL) {
        return;
    }

    backend_autoload_request_stop_controls(request);

    if (request->argv != NULL) {
        g_strfreev(request->argv);
    }

    if (request->proc != NULL) {
        g_object_unref(request->proc);
    }

    if (request->op_cancellable != NULL) {
        g_object_unref(request->op_cancellable);
    }

    if (request->parent_cancellable != NULL) {
        g_object_unref(request->parent_cancellable);
    }

    g_free(request);
}

static GError *backend_autoload_error_from_exit_status(gint status_code,
                                                       gboolean is_query,
                                                       const char *stderr_text)
{
    const char *details = (stderr_text != NULL && stderr_text[0] != '\0')
        ? stderr_text
        : "без stderr";

    switch (status_code) {
    case 1:
        return g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                           MODMAN_GUI_ERR_IO,
                           "%s: %s",
                           is_query ? "Не удалось получить состояние автозагрузки"
                                    : "Операция автозагрузки не выполнена",
                           details);
    case 2:
        return g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                           MODMAN_GUI_ERR_INVALID_NAME,
                           "%s",
                           details);
    case 3:
        return g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                           MODMAN_GUI_ERR_IO,
                           "systemd/systemctl недоступен: %s",
                           details);
    default:
        return g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                           MODMAN_GUI_ERR_IO,
                           "modman-autoload-helper завершился с кодом %d: %s",
                           status_code,
                           details);
    }
}

static void backend_autoload_return_process_error(GTask *task,
                                                  BackendAutoloadRequest *request,
                                                  GError *subprocess_error,
                                                  const char *prefix)
{
    GError *backend_error = NULL;

    backend_autoload_request_stop_controls(request);

    if (subprocess_error != NULL &&
        g_error_matches(subprocess_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_CANCELLED,
                                "%s",
                                "Операция была отменена");
        g_clear_error(&subprocess_error);
        return;
    }

    set_backend_error_from_subprocess(&backend_error, subprocess_error, prefix);
    g_clear_error(&subprocess_error);
    g_task_return_error(task, backend_error);
}

static void backend_autoload_communicate_done(GObject *source_object,
                                              GAsyncResult *result,
                                              gpointer user_data)
{
    GTask *task = G_TASK(user_data);
    BackendAutoloadRequest *request = (BackendAutoloadRequest *)g_task_get_task_data(task);
    GError *subprocess_error = NULL;
    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;
    gchar *stderr_text = NULL;

    (void)source_object;

    if (!g_subprocess_communicate_finish(request->proc,
                                         result,
                                         &stdout_bytes,
                                         &stderr_bytes,
                                         &subprocess_error)) {
        backend_autoload_return_process_error(task,
                                              request,
                                              subprocess_error,
                                              "Ошибка чтения ответа modman-autoload-helper");
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    backend_autoload_request_stop_controls(request);

    if (!g_subprocess_get_if_exited(request->proc)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_UNKNOWN,
                                "%s",
                                "Процесс modman-autoload-helper завершился некорректно");
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    if (stderr_bytes != NULL) {
        gsize stderr_len = 0;
        const char *stderr_data = g_bytes_get_data(stderr_bytes, &stderr_len);

        stderr_text = g_strndup(stderr_data, stderr_len);
    }

    if (request->is_query && g_subprocess_get_exit_status(request->proc) <= 1) {
        gboolean *enabled = g_new(gboolean, 1);
        *enabled = (g_subprocess_get_exit_status(request->proc) == 0);
        g_task_return_pointer(task, enabled, g_free);
    } else if (!request->is_query && g_subprocess_get_exit_status(request->proc) == 0) {
        g_task_return_boolean(task, TRUE);
    } else {
        g_task_return_error(task,
                            backend_autoload_error_from_exit_status(g_subprocess_get_exit_status(request->proc),
                                                                    request->is_query,
                                                                    stderr_text));
    }

    g_free(stderr_text);

    if (stdout_bytes != NULL) {
        g_bytes_unref(stdout_bytes);
    }
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }
    g_object_unref(task);
}

static void backend_autoload_start_async(const char *const *argv,
                                         gboolean is_query,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);
    BackendAutoloadRequest *request = g_new0(BackendAutoloadRequest, 1);
    GError *subprocess_error = NULL;

    request->is_query = is_query;
    request->argv = dup_argv(argv);
    request->op_cancellable = g_cancellable_new();

    if (cancellable != NULL) {
        request->parent_cancellable = g_object_ref(cancellable);
        request->parent_cancel_handler =
            g_cancellable_connect(cancellable,
                                  G_CALLBACK(backend_parent_cancel_cb),
                                  request->op_cancellable,
                                  NULL);
    }

    request->timeout_source_id =
        g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
                                   600,
                                   backend_timeout_cancel_cb,
                                   g_object_ref(request->op_cancellable),
                                   (GDestroyNotify)g_object_unref);

    g_task_set_task_data(task, request, (GDestroyNotify)backend_autoload_request_free);

    {
        GSubprocessLauncher *launcher =
            g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);

        backend_launcher_apply_runtime_env(launcher);
        request->proc = g_subprocess_launcher_spawnv(launcher,
                                                     (const gchar *const *)request->argv,
                                                     &subprocess_error);
        g_object_unref(launcher);
    }
    if (request->proc == NULL) {
        backend_autoload_return_process_error(task,
                                              request,
                                              subprocess_error,
                                              "Не удалось запустить modman-autoload-helper");
        g_object_unref(task);
        return;
    }

    g_subprocess_communicate_async(request->proc,
                                   NULL,
                                   request->op_cancellable,
                                   backend_autoload_communicate_done,
                                   task);
}

static gchar **dup_argv(const char *const *argv)
{
    guint count = 0;
    while (argv[count] != NULL) {
        count++;
    }

    gchar **copy = g_new0(gchar *, count + 1);
    for (guint i = 0; i < count; i++) {
        copy[i] = g_strdup(argv[i]);
    }

    return copy;
}

static gchar **backend_pkexec_argv_dup(const char *const *argv)
{
    guint count = 0;

    while (argv[count] != NULL) {
        count++;
    }

    gchar **copy = g_new0(gchar *, count + 2);
    copy[0] = g_strdup("pkexec");
    for (guint i = 0; i < count; i++) {
        copy[i + 1] = g_strdup(argv[i]);
    }

    return copy;
}

static gchar **backend_privileged_argv_dup(const char *const *argv)
{
    if (geteuid() == 0) {
        return dup_argv(argv);
    }

    return backend_pkexec_argv_dup(argv);
}

/* Build a "--progress-file=PATH" token if MODMAN_PROGRESS_FILE is set in the
 * GUI process environment. Returned string is owned by the caller and must
 * be freed with g_free. Returns NULL when no progress sink is configured.
 *
 * pkexec strips environment variables that aren't on its keep-env whitelist,
 * so we cannot rely on env propagation alone — the path must travel through
 * argv to reach the modman shell under privilege escalation. */
static gchar *backend_progress_flag_dup(void)
{
    const char *path = g_getenv("MODMAN_PROGRESS_FILE");

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    return g_strdup_printf("--progress-file=%s", path);
}

static gboolean backend_timeout_cancel_cb(gpointer user_data)
{
    GCancellable *op_cancellable = G_CANCELLABLE(user_data);
    if (!g_cancellable_is_cancelled(op_cancellable)) {
        g_cancellable_cancel(op_cancellable);
    }

    return G_SOURCE_REMOVE;
}

static void backend_parent_cancel_cb(GCancellable *parent, gpointer user_data)
{
    (void)parent;
    GCancellable *op_cancellable = G_CANCELLABLE(user_data);
    if (!g_cancellable_is_cancelled(op_cancellable)) {
        g_cancellable_cancel(op_cancellable);
    }
}

static gboolean tsv_has_more_than_100_lines(const char *stdout_text)
{
    if (stdout_text == NULL || stdout_text[0] == '\0') {
        return FALSE;
    }

    guint line_count = 0;
    const char *p = stdout_text;
    while (*p != '\0') {
        if (*p == '\n') {
            line_count++;
            if (line_count > 100) {
                return TRUE;
            }
        }
        p++;
    }

    if (p != stdout_text && p[-1] != '\n') {
        line_count++;
    }

    return line_count > 100;
}

static void backend_parse_tsv_in_thread(GTask *task,
                                        gpointer source_object,
                                        gpointer task_data,
                                        GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    BackendAsyncRequest *request = (BackendAsyncRequest *)task_data;
    GError *parse_error = NULL;
    GPtrArray *items = parse_machine_tsv_output_for(request->stdout_for_parse,
                                                    request->tsv_format,
                                                    &parse_error);

    if (items == NULL) {
        g_task_return_error(task, parse_error);
        return;
    }

    g_task_return_pointer(task, items, (GDestroyNotify)g_ptr_array_unref);
}

static void backend_async_return_process_error(GTask *task,
                                               BackendAsyncRequest *request,
                                               GError *subprocess_error,
                                               const char *prefix)
{
    backend_async_request_stop_controls(request);

    if (subprocess_error != NULL &&
        g_error_matches(subprocess_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_CANCELLED,
                                "%s",
                                "Операция была отменена");
        g_clear_error(&subprocess_error);
        return;
    }

    GError *backend_error = NULL;
    set_backend_error_from_subprocess(&backend_error, subprocess_error, prefix);
    g_clear_error(&subprocess_error);
    g_task_return_error(task, backend_error);
}

static void backend_communicate_done(GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data)
{
    (void)source_object;

    GTask *task = G_TASK(user_data);
    BackendAsyncRequest *request = (BackendAsyncRequest *)g_task_get_task_data(task);

    GError *subprocess_error = NULL;
    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;

    if (!g_subprocess_communicate_finish(request->proc,
                                         result,
                                         &stdout_bytes,
                                         &stderr_bytes,
                                         &subprocess_error)) {
        backend_async_return_process_error(task,
                                           request,
                                           subprocess_error,
                                           "Ошибка чтения ответа modman");
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    backend_async_request_stop_controls(request);

    gsize stdout_len = 0;
    gsize stderr_len = 0;
    const char *stdout_data = stdout_bytes != NULL ? g_bytes_get_data(stdout_bytes, &stdout_len) : "";
    const char *stderr_data = stderr_bytes != NULL ? g_bytes_get_data(stderr_bytes, &stderr_len) : "";

    char *stdout_text = g_strndup(stdout_data, stdout_len);
    char *stderr_text = g_strndup(stderr_data, stderr_len);

    if (stderr_text != NULL && stderr_text[0] != '\0') {
        g_debug("backend stderr: %s", stderr_text);
    }

    if (!g_subprocess_get_if_exited(request->proc)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_UNKNOWN,
                                "%s",
                                "Процесс modman завершился некорректно");
        g_free(stdout_text);
        g_free(stderr_text);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    gint status_code = g_subprocess_get_exit_status(request->proc);
    if (status_code != 0) {
        char *combined_error = NULL;
        char *clean_error = NULL;
        gchar *cmd_str = backend_format_argv_for_error((const char *const *)request->argv);

        if (stderr_text != NULL && stderr_text[0] != '\0' && stdout_text != NULL && stdout_text[0] != '\0') {
            combined_error = g_strdup_printf("%s\n%s", stderr_text, stdout_text);
        } else if (stderr_text != NULL && stderr_text[0] != '\0') {
            combined_error = g_strdup(stderr_text);
        } else if (stdout_text != NULL && stdout_text[0] != '\0') {
            combined_error = g_strdup(stdout_text);
        } else {
            combined_error = g_strdup("без stdout/stderr");
        }

        clean_error = backend_strip_ansi_sequences(combined_error);

        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_IO,
                                "%s завершился с кодом %d: %s",
                                cmd_str,
                                status_code,
                                clean_error);
        g_free(cmd_str);
        g_free(clean_error);
        g_free(combined_error);
        g_free(stdout_text);
        g_free(stderr_text);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    g_free(stderr_text);

    if (request->expect_list) {
        if (tsv_has_more_than_100_lines(stdout_text)) {
            request->stdout_for_parse = stdout_text;
            g_task_run_in_thread(task, backend_parse_tsv_in_thread);
            if (stdout_bytes != NULL) {
                g_bytes_unref(stdout_bytes);
            }
            if (stderr_bytes != NULL) {
                g_bytes_unref(stderr_bytes);
            }
            g_object_unref(task);
            return;
        }

        GError *parse_error = NULL;
        GPtrArray *items = parse_machine_tsv_output_for(stdout_text, request->tsv_format, &parse_error);
        g_free(stdout_text);

        if (items == NULL) {
            g_task_return_error(task, parse_error);
        } else {
            g_task_return_pointer(task, items, (GDestroyNotify)g_ptr_array_unref);
        }
    } else {
        g_free(stdout_text);
        g_task_return_boolean(task, TRUE);
    }

    if (stdout_bytes != NULL) {
        g_bytes_unref(stdout_bytes);
    }
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }
    g_object_unref(task);
}

static void backend_start_async_for(ModuleRepository *repo,
                                    const char *const *argv,
                                    gboolean expect_list,
                                    ModuleTsvFormat tsv_format,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    (void)repo;
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);

    BackendAsyncRequest *request = g_new0(BackendAsyncRequest, 1);
    request->expect_list = expect_list;
    request->tsv_format = tsv_format;
    request->argv = dup_argv(argv);
    request->op_cancellable = g_cancellable_new();

    if (cancellable != NULL) {
        request->parent_cancellable = g_object_ref(cancellable);
        request->parent_cancel_handler =
            g_cancellable_connect(cancellable,
                                  G_CALLBACK(backend_parent_cancel_cb),
                                  request->op_cancellable,
                                  NULL);
    }

    request->timeout_source_id =
        g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
                                   600,
                                   backend_timeout_cancel_cb,
                                   g_object_ref(request->op_cancellable),
                                   (GDestroyNotify)g_object_unref);

    g_task_set_task_data(task, request, (GDestroyNotify)backend_async_request_free);

    GError *subprocess_error = NULL;
    request->proc = g_subprocess_newv((const gchar *const *)request->argv,
                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                      &subprocess_error);
    if (request->proc == NULL) {
        backend_async_return_process_error(task,
                                           request,
                                           subprocess_error,
                                           "Не удалось запустить modman");
        g_object_unref(task);
        return;
    }

    g_subprocess_communicate_async(request->proc,
                                   NULL,
                                   request->op_cancellable,
                                   backend_communicate_done,
                                   task);
}

static void backend_start_async(ModuleRepository *repo,
                                const char *const *argv,
                                gboolean expect_list,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    backend_start_async_for(repo, argv, expect_list, MODULE_TSV_FORMAT_LOADED,
                            cancellable, callback, user_data);
}

void backend_list_loaded_async(ModuleRepository *repo,
                               gboolean loaded_before_user_session,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    const char *scope = loaded_before_user_session ? "before" : "after";
    gchar *cmd = g_strdup_printf("--list-loaded-%s", scope);
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        cmd,
        "--machine",
        NULL
    };

    backend_start_async_for(repo, argv, TRUE, MODULE_TSV_FORMAT_LOADED,
                            cancellable, callback, user_data);
    g_free(modman_bin);
    g_free(cmd);
}

GPtrArray *backend_list_loaded_finish(ModuleRepository *repo,
                                      GAsyncResult *result,
                                      GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}

void backend_list_local_async(ModuleRepository *repo,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "--list-local",
        "--machine",
        NULL
    };

    backend_start_async_for(repo, argv, TRUE, MODULE_TSV_FORMAT_LOCAL,
                            cancellable, callback, user_data);
    g_free(modman_bin);
}

GPtrArray *backend_list_local_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}

void backend_search_async(ModuleRepository *repo,
                          const char *query,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    if (!search_query_is_valid(query)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_search_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректный поисковый запрос");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-Ss",
        query,
        "--machine",
        NULL
    };

    backend_start_async_for(repo, argv, TRUE, MODULE_TSV_FORMAT_INET,
                            cancellable, callback, user_data);
    g_free(modman_bin);
}

GPtrArray *backend_search_finish(ModuleRepository *repo,
                                 GAsyncResult *result,
                                 GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}

void backend_load_async_with_modes(ModuleRepository *repo,
                                    const ModuleInfo *module,
                                    const char *const *pfsload_modes,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    gboolean is_local_path;

    if (module == NULL || module->name == NULL || module->name[0] == '\0') {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_load_async_with_modes,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для подключения");
        return;
    }

    is_local_path = strchr(module->name, '/') != NULL && g_str_has_suffix(module->name, ".pfs");
    if (!is_local_path && !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_load_async_with_modes,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для подключения");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    gchar *progress_flag = backend_progress_flag_dup();

    GPtrArray *argv_arr = g_ptr_array_new_with_free_func(NULL);
    g_ptr_array_add(argv_arr, modman_bin);
    if (progress_flag != NULL) {
        g_ptr_array_add(argv_arr, progress_flag);
    }
    if (pfsload_modes != NULL) {
        for (guint i = 0; pfsload_modes[i] != NULL; i++) {
            g_ptr_array_add(argv_arr, (gpointer)pfsload_modes[i]);
        }
    }
    g_ptr_array_add(argv_arr, (gpointer)"-S");
    g_ptr_array_add(argv_arr, (gpointer)module->name);
    g_ptr_array_add(argv_arr, NULL);

    gchar **pkexec_argv = backend_pkexec_argv_dup((const char *const *)argv_arr->pdata);
    g_ptr_array_unref(argv_arr);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE, cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(progress_flag);
    g_free(modman_bin);
}

void backend_load_async_with_mode(ModuleRepository *repo,
                                   const ModuleInfo *module,
                                   const char *pfsload_mode,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    const char *modes[2];
    guint n = 0;

    if (pfsload_mode != NULL) {
        modes[n++] = pfsload_mode;
    }
    modes[n] = NULL;

    backend_load_async_with_modes(repo, module,
                                   pfsload_mode != NULL ? modes : NULL,
                                   cancellable, callback, user_data);
}

void backend_load_async(ModuleRepository *repo,
                        const ModuleInfo *module,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    backend_load_async_with_mode(repo, module, NULL, cancellable, callback, user_data);
}

void backend_load_async_with_loadcmd(ModuleRepository *repo,
                                     const ModuleInfo *module,
                                     const char *load_cmd,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    gboolean is_local_path;

    if (module == NULL || module->name == NULL || module->name[0] == '\0') {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_load_async_with_loadcmd,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для подключения");
        return;
    }

    is_local_path = strchr(module->name, '/') != NULL && g_str_has_suffix(module->name, ".pfs");
    if (!is_local_path && !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_load_async_with_loadcmd,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для подключения");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    gchar *progress_flag = backend_progress_flag_dup();
    gchar *loadcmd_flag = (load_cmd != NULL && load_cmd[0] != '\0')
                          ? g_strdup_printf("--load-cmd=%s", load_cmd)
                          : NULL;

    GPtrArray *argv_arr = g_ptr_array_new_with_free_func(NULL);
    g_ptr_array_add(argv_arr, modman_bin);
    if (progress_flag != NULL) {
        g_ptr_array_add(argv_arr, progress_flag);
    }
    if (loadcmd_flag != NULL) {
        g_ptr_array_add(argv_arr, loadcmd_flag);
    }
    g_ptr_array_add(argv_arr, (gpointer)"-S");
    g_ptr_array_add(argv_arr, (gpointer)module->name);
    g_ptr_array_add(argv_arr, NULL);

    gchar **pkexec_argv = backend_pkexec_argv_dup((const char *const *)argv_arr->pdata);
    g_ptr_array_unref(argv_arr);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE, cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(loadcmd_flag);
    g_free(progress_flag);
    g_free(modman_bin);
}

gboolean backend_load_finish(ModuleRepository *repo,
                             GAsyncResult *result,
                             GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

void backend_download_async(ModuleRepository *repo,
                            const ModuleInfo *module,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    if (module == NULL || module->name == NULL || module->name[0] == '\0' ||
        !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_download_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для скачивания");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    gchar *progress_flag = backend_progress_flag_dup();
    GPtrArray *argv_arr = g_ptr_array_new_with_free_func(NULL);

    g_ptr_array_add(argv_arr, modman_bin);
    if (progress_flag != NULL) {
        g_ptr_array_add(argv_arr, progress_flag);
    }
    g_ptr_array_add(argv_arr, (gpointer)"-D");
    g_ptr_array_add(argv_arr, (gpointer)module->name);
    g_ptr_array_add(argv_arr, NULL);

    gchar **pkexec_argv = backend_pkexec_argv_dup((const char *const *)argv_arr->pdata);
    g_ptr_array_unref(argv_arr);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE, cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(progress_flag);
    g_free(modman_bin);
}

gboolean backend_download_finish(ModuleRepository *repo,
                                 GAsyncResult *result,
                                 GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

void backend_unload_async(ModuleRepository *repo,
                          const ModuleInfo *module,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    if (module == NULL || !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_unload_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для отключения");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "-R",
        module->name,
        NULL
    };
    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE, cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
}

gboolean backend_unload_finish(ModuleRepository *repo,
                               GAsyncResult *result,
                               GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static BackendRemoveLocalStatus backend_remove_local_status_from_kind(const gchar *kind)
{
    if (g_strcmp0(kind, "mounted") == 0) {
        return BACKEND_REMOVE_LOCAL_REFUSED_MOUNTED;
    }

    if (g_strcmp0(kind, "not_found") == 0) {
        return BACKEND_REMOVE_LOCAL_NOT_FOUND;
    }

    if (g_strcmp0(kind, "invalid_name") == 0) {
        return BACKEND_REMOVE_LOCAL_INVALID_NAME;
    }

    if (g_strcmp0(kind, "rm_failed") == 0) {
        return BACKEND_REMOVE_LOCAL_RM_FAILED;
    }

    return BACKEND_REMOVE_LOCAL_RM_FAILED;
}

static void backend_remove_local_result_free(BackendRemoveLocalResult *r)
{
    if (r == NULL) {
        return;
    }

    g_free(r->name);
    g_free(r->path);
    g_free(r->detail);
    g_slice_free(BackendRemoveLocalResult, r);
}

static void backend_remove_local_task_data_free(BackendRemoveLocalTaskData *d)
{
    if (d == NULL) {
        return;
    }

    if (d->results != NULL) {
        g_ptr_array_free(d->results, TRUE);
        d->results = NULL;
    }

    g_slice_free(BackendRemoveLocalTaskData, d);
}

static void backend_remove_local_communicate_done(GObject *source_object,
                                                  GAsyncResult *result,
                                                  gpointer user_data)
{
    GTask *task = G_TASK(user_data);
    BackendRemoveLocalTaskData *task_data =
        (BackendRemoveLocalTaskData *)g_task_get_task_data(task);
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *subprocess_error = NULL;
    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;
    gsize stdout_len = 0;
    gsize stderr_len = 0;
    const char *stdout_data = NULL;
    const char *stderr_data = NULL;
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gchar **lines = NULL;
    gint status_code = 0;
    guint usable_rows = 0;
    guint invalid_rows = 0;

    if (!g_subprocess_communicate_finish(proc,
                                         result,
                                         &stdout_bytes,
                                         &stderr_bytes,
                                         &subprocess_error)) {
        g_task_return_error(task, subprocess_error);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    if (stdout_bytes == NULL) {
        subprocess_error = g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                                       MODMAN_GUI_ERR_REMOVE_LOCAL_PROCESS_FAILED,
                                       "%s",
                                       "Не удалось прочитать stdout modman --remove-local");
        g_task_return_error(task, subprocess_error);
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    stdout_data = g_bytes_get_data(stdout_bytes, &stdout_len);
    if (stdout_data == NULL) {
        subprocess_error = g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                                       MODMAN_GUI_ERR_REMOVE_LOCAL_PROCESS_FAILED,
                                       "%s",
                                       "Пустой ответ modman --remove-local");
        g_task_return_error(task, subprocess_error);
        g_bytes_unref(stdout_bytes);
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    if (!g_subprocess_get_if_exited(proc)) {
        subprocess_error = g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                                       MODMAN_GUI_ERR_REMOVE_LOCAL_PROCESS_FAILED,
                                       "%s",
                                       "Процесс modman --remove-local завершился некорректно");
        g_task_return_error(task, subprocess_error);
        g_bytes_unref(stdout_bytes);
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    status_code = g_subprocess_get_exit_status(proc);
    stderr_data = stderr_bytes != NULL ? g_bytes_get_data(stderr_bytes, &stderr_len) : "";
    stderr_text = g_strndup(stderr_data, stderr_len);

    stdout_text = g_strndup(stdout_data, stdout_len);
    lines = g_strsplit(stdout_text, "\n", -1);

    for (guint i = 0; lines[i] != NULL; i++) {
        BackendRemoveLocalResult *entry;
        gchar **fields;
        gboolean row_is_valid = FALSE;

        if (lines[i][0] == '\0') {
            continue;
        }

        fields = g_strsplit(lines[i], "\t", 0);
        entry = g_slice_new0(BackendRemoveLocalResult);

        if (g_strcmp0(fields[0], "removed") == 0 &&
            fields[1] != NULL && fields[1][0] != '\0') {
            row_is_valid = TRUE;
            entry->status = BACKEND_REMOVE_LOCAL_REMOVED;
            entry->name = fields[1] != NULL ? g_strdup(fields[1]) : NULL;
            entry->path = fields[2] != NULL ? g_strdup(fields[2]) : NULL;
        } else if (g_strcmp0(fields[0], "error") == 0 &&
                   fields[1] != NULL && fields[1][0] != '\0' &&
                   fields[2] != NULL && fields[2][0] != '\0') {
            row_is_valid = TRUE;
            entry->status = backend_remove_local_status_from_kind(fields[1]);
            entry->name = fields[2] != NULL ? g_strdup(fields[2]) : NULL;
            entry->detail = fields[3] != NULL ? g_strdup(fields[3]) : NULL;
        } else {
            entry->status = BACKEND_REMOVE_LOCAL_RM_FAILED;
            entry->detail = g_strdup(lines[i]);
        }

        if (row_is_valid) {
            usable_rows++;
        } else {
            invalid_rows++;
        }

        if (task_data != NULL && task_data->results != NULL) {
            g_ptr_array_add(task_data->results, entry);
        } else {
            backend_remove_local_result_free(entry);
        }

        g_strfreev(fields);
    }

    if (status_code != 0 && usable_rows == 0) {
        const gchar *base_message =
            invalid_rows > 0
                ? "modman --remove-local завершился с ошибкой и вернул некорректный machine-вывод"
                : "modman --remove-local завершился с ошибкой без machine-вывода";
        subprocess_error = g_error_new(MODMAN_GUI_ERROR_DOMAIN,
                                       MODMAN_GUI_ERR_REMOVE_LOCAL_PROCESS_FAILED,
                                       "%s (код %d): %s",
                                       base_message,
                                       status_code,
                                       stderr_text != NULL && stderr_text[0] != '\0'
                                           ? stderr_text
                                           : "без stderr");
        g_task_return_error(task, subprocess_error);
        g_strfreev(lines);
        g_free(stdout_text);
        g_free(stderr_text);
        g_bytes_unref(stdout_bytes);
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    g_strfreev(lines);
    g_free(stdout_text);
    g_free(stderr_text);
    g_bytes_unref(stdout_bytes);
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }

    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}

void backend_remove_local_async(ModuleRepository *repo,
                                const char *const *names,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    gsize name_count = 0;
    gchar **argv;
    char *modman_bin;
    BackendRemoveLocalTaskData *task_data;
    GTask *task;
    GSubprocess *proc;
    GSubprocessLauncher *launcher;
    GError *subprocess_error = NULL;

    (void)repo;

    while (names != NULL && names[name_count] != NULL) {
        if (!module_name_is_valid(names[name_count])) {
            g_task_report_new_error(NULL,
                                    callback,
                                    user_data,
                                    backend_remove_local_async,
                                    MODMAN_GUI_ERROR_DOMAIN,
                                    MODMAN_GUI_ERR_INVALID_NAME,
                                    "Invalid module name: %s",
                                    names[name_count]);
            return;
        }
        name_count++;
    }

    modman_bin = backend_modman_bin_dup();
    argv = g_new0(gchar *, name_count + 5);
    argv[0] = g_strdup("pkexec");
    argv[1] = g_strdup(modman_bin);
    argv[2] = g_strdup("--machine");
    argv[3] = g_strdup("--remove-local");
    for (gsize i = 0; i < name_count; i++) {
        argv[i + 4] = g_strdup(names[i]);
    }

    task_data = g_slice_new0(BackendRemoveLocalTaskData);
    task_data->results = g_ptr_array_new_with_free_func((GDestroyNotify)backend_remove_local_result_free);

    task = g_task_new(NULL, cancellable, callback, user_data);
    g_task_set_task_data(task,
                         task_data,
                         (GDestroyNotify)backend_remove_local_task_data_free);

    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    backend_launcher_apply_runtime_env(launcher);
    proc = g_subprocess_launcher_spawnv(launcher,
                                        (const gchar *const *)argv,
                                        &subprocess_error);
    g_object_unref(launcher);
    if (proc == NULL) {
        g_task_return_error(task, subprocess_error);
        g_object_unref(task);
        g_free(modman_bin);
        g_strfreev(argv);
        return;
    }

    g_subprocess_communicate_async(proc,
                                   NULL,
                                   cancellable,
                                   backend_remove_local_communicate_done,
                                   task);
    g_object_unref(proc);

    g_free(modman_bin);
    g_strfreev(argv);
}

gboolean backend_remove_local_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GPtrArray **out_results,
                                     GError **error)
{
    BackendRemoveLocalTaskData *task_data;
    GPtrArray *results;
    gboolean all_removed = TRUE;

    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);

    task_data = (BackendRemoveLocalTaskData *)g_task_get_task_data(G_TASK(result));

    if (g_task_had_error(G_TASK(result))) {
        (void)g_task_propagate_pointer(G_TASK(result), error);
        if (out_results != NULL) {
            *out_results = NULL;
        }
        return FALSE;
    }

    results = task_data != NULL ? task_data->results : NULL;
    if (results != NULL) {
        for (guint i = 0; i < results->len; i++) {
            BackendRemoveLocalResult *entry =
                (BackendRemoveLocalResult *)g_ptr_array_index(results, i);

            if (entry != NULL && entry->status != BACKEND_REMOVE_LOCAL_REMOVED) {
                all_removed = FALSE;
                break;
            }
        }
    }

    if (out_results != NULL && task_data != NULL) {
        *out_results = g_steal_pointer(&task_data->results);
    }

    return all_removed;
}

void backend_remove_local_results_free(GPtrArray *results)
{
    if (results != NULL) {
        g_ptr_array_free(results, TRUE);
    }
}

/* TODO(pkg-24): cleanup orphan after autoload widget removal. */
void backend_autoload_state_async(ModuleRepository *repo,
                                  const ModuleInfo *module,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    char *helper_path;
    const char *argv[4];

    (void)repo;

    if (module == NULL || !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_autoload_state_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для проверки автозагрузки");
        return;
    }

    helper_path = backend_autoload_helper_dup();
    argv[0] = helper_path;
    argv[1] = "is-enabled";
    argv[2] = module->name;
    argv[3] = NULL;

    backend_autoload_start_async(argv, TRUE, cancellable, callback, user_data);
    g_free(helper_path);
}

gboolean backend_autoload_state_finish(ModuleRepository *repo,
                                       GAsyncResult *result,
                                       gboolean *enabled_out,
                                       GError **error)
{
    gboolean *enabled_ptr;

    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);

    enabled_ptr = g_task_propagate_pointer(G_TASK(result), error);
    if (enabled_ptr == NULL) {
        return FALSE;
    }

    if (enabled_out != NULL) {
        *enabled_out = *enabled_ptr;
    }

    g_free(enabled_ptr);
    return TRUE;
}

/* TODO(pkg-24): cleanup orphan after autoload widget removal. */
void backend_autoload_set_async(ModuleRepository *repo,
                                const ModuleInfo *module,
                                gboolean enabled,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    char *helper_path;
    const char *argv[4];

    (void)repo;

    if (module == NULL || !module_name_is_valid(module->name)) {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_autoload_set_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректное имя модуля для изменения автозагрузки");
        return;
    }

    helper_path = backend_autoload_helper_dup();
    argv[0] = helper_path;
    argv[1] = enabled ? "enable" : "disable";
    argv[2] = module->name;
    argv[3] = NULL;

    backend_autoload_start_async(argv, FALSE, cancellable, callback, user_data);
    g_free(helper_path);
}

gboolean backend_autoload_set_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static GPtrArray *parse_update_tsv_output(const char *stdout_text, GError **error)
{
    GPtrArray *items = g_ptr_array_new_with_free_func((GDestroyNotify)module_update_info_free);

    if (stdout_text == NULL || stdout_text[0] == '\0') {
        return items;
    }

    gchar **lines = g_strsplit(stdout_text, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0') {
            continue;
        }

        if (g_str_has_prefix(lines[i], "warn\t")) {
            continue;
        }

        GError *parse_error = NULL;
        ModuleUpdateInfo *update_info = module_update_info_from_tsv_line(lines[i], &parse_error);
        if (update_info == NULL) {
            g_set_error(error,
                        MODMAN_GUI_ERROR_DOMAIN,
                        MODMAN_GUI_ERR_PARSE,
                        "Некорректная строка update machine-вывода: %s",
                        parse_error != NULL ? parse_error->message : "unknown parse error");
            g_clear_error(&parse_error);
            g_strfreev(lines);
            g_ptr_array_unref(items);
            return NULL;
        }

        g_ptr_array_add(items, update_info);
    }

    g_strfreev(lines);
    return items;
}

typedef struct {
    GPtrArray *updates;
    char *stdout_for_parse;
} BackendCheckUpdatesTaskData;

static void backend_check_updates_task_data_free(BackendCheckUpdatesTaskData *d)
{
    if (d == NULL) {
        return;
    }

    if (d->updates != NULL) {
        g_ptr_array_unref(d->updates);
        d->updates = NULL;
    }

    g_free(d->stdout_for_parse);
    g_slice_free(BackendCheckUpdatesTaskData, d);
}

static void backend_check_updates_parse_in_thread(GTask *task,
                                                   gpointer source_object,
                                                   gpointer task_data,
                                                   GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    BackendCheckUpdatesTaskData *data = (BackendCheckUpdatesTaskData *)task_data;
    GError *parse_error = NULL;
    GPtrArray *items = parse_update_tsv_output(data->stdout_for_parse, &parse_error);

    if (items == NULL) {
        g_task_return_error(task, parse_error);
        return;
    }

    g_task_return_pointer(task, items, (GDestroyNotify)g_ptr_array_unref);
}

static void backend_check_updates_communicate_done(GObject *source_object,
                                                    GAsyncResult *result,
                                                    gpointer user_data)
{
    GTask *task = G_TASK(user_data);
    BackendCheckUpdatesTaskData *task_data =
        (BackendCheckUpdatesTaskData *)g_task_get_task_data(task);
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *subprocess_error = NULL;
    GBytes *stdout_bytes = NULL;
    GBytes *stderr_bytes = NULL;

    if (!g_subprocess_communicate_finish(proc,
                                         result,
                                         &stdout_bytes,
                                         &stderr_bytes,
                                         &subprocess_error)) {
        g_task_return_error(task, subprocess_error);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    if (!g_subprocess_get_if_exited(proc)) {
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_UNKNOWN,
                                "%s",
                                "Процесс modman --check-updates завершился некорректно");
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    gint status_code = g_subprocess_get_exit_status(proc);
    gsize stderr_len = 0;
    const char *stderr_data = stderr_bytes != NULL
        ? g_bytes_get_data(stderr_bytes, &stderr_len)
        : "";
    gchar *stderr_text = g_strndup(stderr_data, stderr_len);

    if (status_code != 0) {
        gchar *clean_error = backend_strip_ansi_sequences(
            stderr_text != NULL && stderr_text[0] != '\0' ? stderr_text : "без stderr");
        g_task_return_new_error(task,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_IO,
                                "modman --check-updates завершился с кодом %d: %s",
                                status_code,
                                clean_error);
        g_free(clean_error);
        g_free(stderr_text);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    g_free(stderr_text);

    gsize stdout_len = 0;
    const char *stdout_data = stdout_bytes != NULL
        ? g_bytes_get_data(stdout_bytes, &stdout_len)
        : "";
    gchar *stdout_text = g_strndup(stdout_data, stdout_len);

    if (tsv_has_more_than_100_lines(stdout_text)) {
        task_data->stdout_for_parse = stdout_text;
        g_task_run_in_thread(task, backend_check_updates_parse_in_thread);
        if (stdout_bytes != NULL) {
            g_bytes_unref(stdout_bytes);
        }
        if (stderr_bytes != NULL) {
            g_bytes_unref(stderr_bytes);
        }
        g_object_unref(task);
        return;
    }

    GError *parse_error = NULL;
    GPtrArray *items = parse_update_tsv_output(stdout_text, &parse_error);
    g_free(stdout_text);

    if (items == NULL) {
        g_task_return_error(task, parse_error);
    } else {
        g_task_return_pointer(task, items, (GDestroyNotify)g_ptr_array_unref);
    }

    if (stdout_bytes != NULL) {
        g_bytes_unref(stdout_bytes);
    }
    if (stderr_bytes != NULL) {
        g_bytes_unref(stderr_bytes);
    }
    g_object_unref(task);
}

void backend_check_updates_async(ModuleRepository *repo,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    (void)repo;

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "--machine",
        "--check-updates",
        NULL
    };

    BackendCheckUpdatesTaskData *task_data = g_slice_new0(BackendCheckUpdatesTaskData);
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);
    g_task_set_task_data(task,
                         task_data,
                         (GDestroyNotify)backend_check_updates_task_data_free);

    GError *subprocess_error = NULL;
    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    backend_launcher_apply_runtime_env(launcher);
    gchar **privileged_argv = backend_privileged_argv_dup(argv);
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher,
                                                     (const gchar *const *)privileged_argv,
                                                     &subprocess_error);
    g_object_unref(launcher);
    g_strfreev(privileged_argv);
    g_free(modman_bin);

    if (proc == NULL) {
        g_task_return_error(task, subprocess_error);
        g_object_unref(task);
        return;
    }

    g_subprocess_communicate_async(proc,
                                   NULL,
                                   cancellable,
                                   backend_check_updates_communicate_done,
                                   task);
    g_object_unref(proc);
}

GPtrArray *backend_check_updates_finish(ModuleRepository *repo,
                                         GAsyncResult *result,
                                         GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}

void backend_update_apply_async(ModuleRepository *repo,
                                 const char *update_id,
                                 gboolean confirm_system,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    if (update_id == NULL || update_id[0] == '\0') {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_update_apply_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректный идентификатор обновления");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();

    GPtrArray *argv_arr = g_ptr_array_new_with_free_func(NULL);
    g_ptr_array_add(argv_arr, modman_bin);
    g_ptr_array_add(argv_arr, (gpointer)"--machine");
    gchar *progress_flag = backend_progress_flag_dup();
    if (progress_flag != NULL) {
        g_ptr_array_add(argv_arr, progress_flag);
    }
    g_ptr_array_add(argv_arr, (gpointer)"--update");
    if (confirm_system) {
        g_ptr_array_add(argv_arr, (gpointer)"--confirm-system-updates");
    }
    g_ptr_array_add(argv_arr, (gpointer)update_id);
    g_ptr_array_add(argv_arr, NULL);

    gchar **pkexec_argv = backend_pkexec_argv_dup((const char *const *)argv_arr->pdata);
    g_ptr_array_unref(argv_arr);
    g_free(progress_flag);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE,
                        cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
}

gboolean backend_update_apply_finish(ModuleRepository *repo,
                                      GAsyncResult *result,
                                      GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

void backend_update_blacklist_add_async(ModuleRepository *repo,
                                         const char *update_id,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    if (update_id == NULL || update_id[0] == '\0') {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                backend_update_blacklist_add_async,
                                MODMAN_GUI_ERROR_DOMAIN,
                                MODMAN_GUI_ERR_INVALID_NAME,
                                "%s",
                                "Некорректный идентификатор обновления для blacklist");
        return;
    }

    char *modman_bin = backend_modman_bin_dup();
    const char *argv[] = {
        modman_bin,
        "--machine",
        "--update-blacklist-add",
        update_id,
        NULL
    };

    gchar **pkexec_argv = backend_pkexec_argv_dup(argv);

    backend_start_async(repo, (const char *const *)pkexec_argv, FALSE,
                        cancellable, callback, user_data);
    g_strfreev(pkexec_argv);
    g_free(modman_bin);
}

gboolean backend_update_blacklist_add_finish(ModuleRepository *repo,
                                              GAsyncResult *result,
                                              GError **error)
{
    (void)repo;
    g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

void backend_update_info_array_free(GPtrArray *updates)
{
    if (updates != NULL) {
        g_ptr_array_unref(updates);
    }
}
