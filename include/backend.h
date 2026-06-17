#ifndef MODMAN_BACKEND_H
#define MODMAN_BACKEND_H

#if __has_include(<gio/gio.h>)
#include <gio/gio.h>
#else
typedef int gboolean;
typedef void *gpointer;
typedef struct _GError GError;
typedef struct _GPtrArray GPtrArray;
typedef struct _GAsyncResult GAsyncResult;
typedef struct _GCancellable GCancellable;
typedef void (*GAsyncReadyCallback)(void *source_object, GAsyncResult *result, gpointer user_data);
typedef char gchar;
#endif

#include "model.h"

typedef enum {
    BACKEND_REMOVE_LOCAL_REMOVED = 0,
    BACKEND_REMOVE_LOCAL_REFUSED_MOUNTED,
    BACKEND_REMOVE_LOCAL_NOT_FOUND,
    BACKEND_REMOVE_LOCAL_INVALID_NAME,
    BACKEND_REMOVE_LOCAL_RM_FAILED,
} BackendRemoveLocalStatus;

typedef struct {
    gchar *name;
    gchar *path;
    gchar *detail;
    BackendRemoveLocalStatus status;
} BackendRemoveLocalResult;

typedef struct {
    gchar *path;
    gchar *name;
    gchar *format;
    gchar *loaded_state;
    gchar *size;
    gchar *format_detail;
    gchar *modules;
    gchar *dependencies;
    gboolean is_loaded;
} BackendOpenFileInfo;

ModuleRepository *module_repository_new(const ModmanConf *conf);
void module_repository_free(ModuleRepository *repo);

gboolean modman_conf_load(ModmanConf *out_conf, GError **error);
void modman_conf_free(ModmanConf *conf);
void modman_conf_clear(ModmanConf *conf);
void backend_set_runtime_conf(const ModmanConf *conf);
void backend_reset_runtime_conf(void);

gboolean backend_call_sync(const char *const *argv,
                           char **stdout_out,
                           char **stderr_out,
                           int *exit_status,
                           GError **error);

GPtrArray *backend_list_loaded_sync(const char *filter, GError **error);
GPtrArray *backend_list_local_sync(GError **error);
GPtrArray *backend_search_sync(const char *query, GError **error);
gboolean backend_load_sync(const char *name, GError **error);
gboolean backend_unload_sync(const char *name, GError **error);
gboolean backend_sync_db_sync(GError **error);

BackendOpenFileInfo *backend_open_file_info_sync(const char *absolute_path,
                                                 GError **error);
void backend_open_file_info_free(BackendOpenFileInfo *info);
gchar *backend_open_unload_name_from_path(const char *absolute_path,
                                          GError **error);

void backend_sync_db_async(ModuleRepository *repo,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
gboolean backend_sync_db_finish(ModuleRepository *repo,
                                 GAsyncResult *result,
                                 GError **error);

void backend_open_file_info_async(const char *absolute_path,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
BackendOpenFileInfo *backend_open_file_info_finish(GAsyncResult *result,
                                                   GError **error);

void backend_open_attach_path_async(const char *absolute_path,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
void backend_open_attach_path_with_modes_async(const char *absolute_path,
                                               gboolean lower_layer,
                                               gboolean copy_to_ram,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean backend_open_attach_path_finish(GAsyncResult *result,
                                          GError **error);

void backend_open_unload_path_async(const char *absolute_path,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean backend_open_unload_path_finish(GAsyncResult *result,
                                         GError **error);

void backend_get_deps_async(const char *module_name,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data);

gchar **backend_get_deps_finish(GAsyncResult *result,
                                GError **error);

void backend_list_loaded_async(ModuleRepository *repo,
                               gboolean loaded_before_user_session,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
GPtrArray *backend_list_loaded_finish(ModuleRepository *repo,
                                      GAsyncResult *result,
                                      GError **error);

void backend_list_local_async(ModuleRepository *repo,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
GPtrArray *backend_list_local_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GError **error);

void backend_search_async(ModuleRepository *repo,
                          const char *query,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data);
GPtrArray *backend_search_finish(ModuleRepository *repo,
                                 GAsyncResult *result,
                                 GError **error);

void backend_load_async_with_modes(ModuleRepository *repo,
                                    const ModuleInfo *module,
                                    const char *const *pfsload_modes,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
void backend_load_async_with_mode(ModuleRepository *repo,
                                   const ModuleInfo *module,
                                   const char *pfsload_mode,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
void backend_load_async(ModuleRepository *repo,
                        const ModuleInfo *module,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
void backend_load_async_with_loadcmd(ModuleRepository *repo,
                                     const ModuleInfo *module,
                                     const char *load_cmd,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean backend_load_finish(ModuleRepository *repo,
                             GAsyncResult *result,
                             GError **error);

void backend_download_async(ModuleRepository *repo,
                            const ModuleInfo *module,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
gboolean backend_download_finish(ModuleRepository *repo,
                                 GAsyncResult *result,
                                 GError **error);

void backend_unload_async(ModuleRepository *repo,
                          const ModuleInfo *module,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data);
gboolean backend_unload_finish(ModuleRepository *repo,
                               GAsyncResult *result,
                               GError **error);

void backend_autoload_state_async(ModuleRepository *repo,
                                  const ModuleInfo *module,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
gboolean backend_autoload_state_finish(ModuleRepository *repo,
                                       GAsyncResult *result,
                                       gboolean *enabled_out,
                                       GError **error);

void backend_autoload_set_async(ModuleRepository *repo,
                                const ModuleInfo *module,
                                gboolean enabled,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean backend_autoload_set_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GError **error);

void backend_remove_local_async(ModuleRepository *repo,
                                const char *const *names,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean backend_remove_local_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GPtrArray **out_results,
                                     GError **error);
void backend_remove_local_results_free(GPtrArray *results);

/* Update operations.
 *
 * backend_check_updates_async  — runs modman --machine --check-updates directly
 *   when already root, otherwise through pkexec, so GUI results match sudo/root
 *   CLI checks; finish returns GPtrArray of ModuleUpdateInfo*.
 *
 * backend_update_apply_async   — runs pkexec modman --machine --update
 *   [--confirm-system-updates] <id>; finish returns gboolean.
 *   Pass confirm_system=TRUE only when the caller has explicitly acknowledged
 *   that system-layer modules will be replaced.
 *
 * backend_update_blacklist_add_async — runs pkexec modman --machine
 *   --update-blacklist-add <id>; finish returns gboolean.
 */
void backend_check_updates_async(ModuleRepository *repo,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
GPtrArray *backend_check_updates_finish(ModuleRepository *repo,
                                        GAsyncResult *result,
                                        GError **error);
void backend_update_apply_async(ModuleRepository *repo,
                                const char *update_id,
                                gboolean confirm_system,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean backend_update_apply_finish(ModuleRepository *repo,
                                     GAsyncResult *result,
                                     GError **error);
void backend_update_blacklist_add_async(ModuleRepository *repo,
                                        const char *update_id,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean backend_update_blacklist_add_finish(ModuleRepository *repo,
                                             GAsyncResult *result,
                                             GError **error);
void backend_update_info_array_free(GPtrArray *updates);

#endif
