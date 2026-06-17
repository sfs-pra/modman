#ifndef MODMAN_MODEL_H
#define MODMAN_MODEL_H

#if __has_include(<glib.h>)
#include <glib.h>
#else
typedef int gboolean;
typedef char gchar;
typedef void *gpointer;
typedef struct _GPtrArray GPtrArray;
typedef struct _GError GError;
#endif

typedef struct ModuleInfo {
    char *name;
    char *layer;
    char *path;
    char *desc;
    char *category;
    char *version;
    char *repo;
    double size_mb;
    gboolean is_local;
    gboolean is_autoload;
    gboolean has_update;
    gboolean has_error;
    char *mount_mode;   /* "upper" | "lower" | NULL — mode from --list-loaded-* */
    char *mount_in_ram; /* "yes" | "no" | NULL — RAM-backed module via pfsload -r */
} ModuleInfo;

typedef struct ModuleUpdateInfo {
    char *id;
    char *name;
    char *old_version;
    char *new_version;
    char *installed_path;
    char *candidate_filename;
    char *repo;
    char *risk;
    gboolean reboot_required;
    char *message;
} ModuleUpdateInfo;

/* Runtime config loaded from modman.conf with safe defaults. */
typedef struct ModmanConf {
    char *modman_bin;
    char *download_dir;
    char *cache_file;
    char *aufs_initrd_prefix;
    char *aufs_system_path;
    char *cmd_load;
    char *cmd_unload;
    char *ifs_sep;
    char *layering_override;
    gchar *pfsload_allow_lower;        /* full command string or NULL */
    gchar *pfsload_allow_toram;        /* full command string or NULL */
    gchar *pfsload_allow_lower_toram;  /* full command string or NULL */
    gchar *pfsload_allow_upper_toram;  /* full command string or NULL */
} ModmanConf;

typedef struct ModuleRepository ModuleRepository;

typedef void (*ModuleInfoFreeFunc)(ModuleInfo *module);
typedef gboolean (*ModuleInfoPredicate)(const ModuleInfo *module, gpointer user_data);

ModuleRepository *module_repo_new(void);
void module_repo_free(ModuleRepository *repo);
gboolean module_repo_add(ModuleRepository *repo, ModuleInfo *module);
ModuleInfo *module_repo_find_by_name(ModuleRepository *repo, const char *name);
GPtrArray *module_repo_filter(ModuleRepository *repo, const char *query, gboolean match_desc);
void module_repo_clear(ModuleRepository *repo);

/* TSV layout produced by modman --machine differs per command:
 *   LOADED: name \t layer \t path \t (empty)
 *   LOCAL:  name \t size \t date  \t path
 *   INET:   name \t size \t date  \t desc \t category
 * Use the *_from_tsv_line_for() variant matching the originating command
 * so fields land in the correct ModuleInfo slots. */
typedef enum {
    MODULE_TSV_FORMAT_LOADED = 0,
    MODULE_TSV_FORMAT_LOCAL,
    MODULE_TSV_FORMAT_INET
} ModuleTsvFormat;

ModuleInfo *module_info_from_tsv_line(const char *line, GError **error);
ModuleInfo *module_info_from_tsv_line_for(const char *line,
                                          ModuleTsvFormat format,
                                          GError **error);
ModuleUpdateInfo *module_update_info_from_tsv_line(const char *line, GError **error);
void module_update_info_free(ModuleUpdateInfo *update_info);
char *tsv_escape(const char *input);
char *tsv_unescape(const char *input, GError **error);
double parse_size_mb(const char *str);

#endif
