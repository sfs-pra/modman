#if __has_include(<check.h>)
#include <check.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <unistd.h>

#include "../include/backend.h"
#include "../include/errors.h"

typedef struct {
    GMainLoop *loop;
    gboolean callback_fired;
    GPtrArray *items;
    GPtrArray *remove_results;
    BackendOpenFileInfo *open_file_info;
    gboolean ok;
    GError *error;
} AsyncResultProbe;

typedef struct {
    GMainLoop *loop;
    gboolean timed_out;
} LoopTimeoutCtx;

static gboolean quit_loop_on_timeout(gpointer user_data)
{
    LoopTimeoutCtx *ctx = (LoopTimeoutCtx *)user_data;
    ctx->timed_out = TRUE;
    g_main_loop_quit(ctx->loop);
    return G_SOURCE_REMOVE;
}

static void cleanup_probe(AsyncResultProbe *probe)
{
    if (probe->items != NULL) {
        g_ptr_array_unref(probe->items);
        probe->items = NULL;
    }

    if (probe->remove_results != NULL) {
        backend_remove_local_results_free(probe->remove_results);
        probe->remove_results = NULL;
    }

    if (probe->open_file_info != NULL) {
        backend_open_file_info_free(probe->open_file_info);
        probe->open_file_info = NULL;
    }

    if (probe->error != NULL) {
        g_clear_error(&probe->error);
    }

    if (probe->loop != NULL) {
        g_main_loop_unref(probe->loop);
        probe->loop = NULL;
    }
}

static gchar *make_fake_modman_success_script(void)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-success-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(tmp_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    const gchar *script =
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "have_machine=0\n"
        "for a in \"$@\"; do\n"
        "  if [[ \"$a\" == \"--machine\" ]]; then have_machine=1; fi\n"
        "done\n"
        "if [[ ${have_machine} -eq 0 ]]; then\n"
        "  echo 'missing --machine' >&2\n"
        "  exit 3\n"
        "fi\n"
        "if [[ \"$1\" == \"--list-loaded-after\" ]]; then\n"
        "  printf 'firefox-esr\\tloaded\\t/modules/firefox-esr.pfs\\tBrowser\\n'\n"
        "  printf 'libreoffice\\tloaded\\t/modules/libreoffice.pfs\\tOffice suite\\n'\n"
        "  printf 'gimp\\tloaded\\t/modules/gimp.pfs\\tGraphics editor\\n'\n"
        "  exit 0\n"
        "fi\n"
        "echo 'unsupported' >&2\n"
        "exit 4\n";

    gboolean ok = g_file_set_contents(tmp_path, script, -1, NULL);
    ck_assert(ok);
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);
    return tmp_path;
}

static gchar *make_fake_modman_five_field_script(void)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-five-field-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(tmp_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    const gchar *script =
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "have_machine=0\n"
        "for a in \"$@\"; do\n"
        "  if [[ \"$a\" == \"--machine\" ]]; then have_machine=1; fi\n"
        "done\n"
        "if [[ ${have_machine} -eq 0 ]]; then\n"
        "  echo 'missing --machine' >&2\n"
        "  exit 3\n"
        "fi\n"
        "if [[ \"$1\" == \"--list-loaded-after\" ]]; then\n"
        "  printf 'krita\\tloaded\\t/modules/krita.pfs\\tRaster graphics editor\\tgraphics\\n'\n"
        "  printf 'firefox\\tloaded\\t/modules/firefox.pfs\\tWeb browser\\tinternet\\n'\n"
        "  exit 0\n"
        "fi\n"
        "echo 'unsupported' >&2\n"
        "exit 4\n";

    gboolean ok = g_file_set_contents(tmp_path, script, -1, NULL);
    ck_assert(ok);
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);
    return tmp_path;
}

static gchar *make_fake_modman_remove_local_script(const gchar *argv_dump_path)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-remove-local-XXXXXX", g_get_tmp_dir());
    gchar *quoted_dump_path;
    gchar *script;
    gint fd = g_mkstemp(tmp_path);

    ck_assert_int_ne(fd, -1);
    close(fd);

    quoted_dump_path = g_shell_quote(argv_dump_path);
    script = g_strdup_printf(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%%s\\n' \"$@\" > %s\n"
        "if [[ \"${1:-}\" == \"--machine\" && \"${2:-}\" == \"--remove-local\" ]]; then\n"
        "  shift 2\n"
        "  for name in \"$@\"; do\n"
        "    printf 'removed\\t%%s\\t/tmp/%%s.pfs\\n' \"$name\" \"$name\"\n"
        "  done\n"
        "  exit 0\n"
        "fi\n"
        "echo 'unsupported' >&2\n"
        "exit 9\n",
        quoted_dump_path);

    ck_assert(g_file_set_contents(tmp_path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);

    g_free(script);
    g_free(quoted_dump_path);
    return tmp_path;
}

static void on_list_loaded_done(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->items = backend_list_loaded_finish(NULL, result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_load_done(GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->ok = backend_load_finish(NULL, result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_remove_local_done(GObject *source_object,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->ok = backend_remove_local_finish(NULL,
                                            result,
                                            &probe->remove_results,
                                            &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_check_updates_done(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->items = backend_check_updates_finish(NULL, result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_update_apply_done(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->ok = backend_update_apply_finish(NULL, result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_open_file_info_done(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->open_file_info = backend_open_file_info_finish(result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_open_attach_done(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->ok = backend_open_attach_path_finish(result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static void on_open_unload_done(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    (void)source_object;

    AsyncResultProbe *probe = (AsyncResultProbe *)user_data;
    probe->callback_fired = TRUE;
    probe->ok = backend_open_unload_path_finish(result, &probe->error);
    g_main_loop_quit(probe->loop);
}

static gchar *make_fake_modman_file_info_script(const gchar *row, gint status_code)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-file-info-XXXXXX", g_get_tmp_dir());
    gchar *quoted_row;
    gchar *script;
    gint fd = g_mkstemp(tmp_path);

    ck_assert_int_ne(fd, -1);
    close(fd);

    quoted_row = g_shell_quote(row);
    script = g_strdup_printf(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "if [[ \"${1:-}\" == \"--machine\" && \"${2:-}\" == \"-Qp\" ]]; then\n"
        "  if [[ -n %s ]]; then\n"
        "    printf '%%s\\n' %s\n"
        "  fi\n"
        "  exit %d\n"
        "fi\n"
        "echo 'unsupported' >&2\n"
        "exit 9\n",
        quoted_row,
        quoted_row,
        status_code);

    ck_assert(g_file_set_contents(tmp_path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);

    g_free(script);
    g_free(quoted_row);
    return tmp_path;
}

static gchar *make_fake_modman_check_updates_script(void)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-check-updates-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(tmp_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    const gchar *script =
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "have_machine=0\n"
        "have_check=0\n"
        "for a in \"$@\"; do\n"
        "  [[ \"$a\" == \"--machine\" ]] && have_machine=1\n"
        "  [[ \"$a\" == \"--check-updates\" ]] && have_check=1\n"
        "done\n"
        "if [[ ${have_machine} -eq 0 || ${have_check} -eq 0 ]]; then\n"
        "  echo 'missing --machine or --check-updates' >&2\n"
        "  exit 3\n"
        "fi\n"
        "printf 'warn\\toptional_source\\textramod not available: /mnt/home/zz2601/_all/modules\\n'\n"
        "printf 'update\\t"
        "aabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccdd"
        "\\tfirefox-esr\\t1.0\\t2.0\\t/modules/firefox-esr.pfs"
        "\\tfirefox-esr-2.0.pfs\\thttps://repo.example.com\\tnormal\\t0\\n'\n"
        "printf 'update\\t"
        "1122334411223344112233441122334411223344112233441122334411223344"
        "\\tlibreoffice\\t7.5\\t7.6\\t/modules/libreoffice.pfs"
        "\\tlibreoffice-7.6.pfs\\thttps://repo.example.com\\tloaded\\t1\\tMajor update\\n'\n"
        "exit 0\n";

    gboolean ok = g_file_set_contents(tmp_path, script, -1, NULL);
    ck_assert(ok);
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);
    return tmp_path;
}

static gchar *make_fake_modman_update_apply_argv_script(const gchar *argv_dump_path)
{
    gchar *tmp_path = g_strdup_printf("%s/modman-fake-update-apply-XXXXXX", g_get_tmp_dir());
    gchar *quoted_dump_path;
    gchar *script;
    gint fd = g_mkstemp(tmp_path);

    ck_assert_int_ne(fd, -1);
    close(fd);

    quoted_dump_path = g_shell_quote(argv_dump_path);
    script = g_strdup_printf(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "printf '%%s\\n' \"$@\" > %s\n"
        "exit 0\n",
        quoted_dump_path);

    ck_assert(g_file_set_contents(tmp_path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(tmp_path, 0755), 0);

    g_free(script);
    g_free(quoted_dump_path);
    return tmp_path;
}

static gchar *make_fake_pkexec_dir(const gchar *argv_dump_path)
{
    GError *error = NULL;
    gchar *tmp_dir = g_dir_make_tmp("modman-fake-pkexec-XXXXXX", &error);
    ck_assert_msg(tmp_dir != NULL, "g_dir_make_tmp failed: %s", error != NULL ? error->message : "unknown");
    g_clear_error(&error);

    gchar *pkexec_path = g_build_filename(tmp_dir, "pkexec", NULL);
    gchar *script;

    if (argv_dump_path != NULL) {
        gchar *quoted_dump_path = g_shell_quote(argv_dump_path);
        script = g_strdup_printf(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "printf '%%s\n' \"$@\" > %s\n"
            "exec \"$@\"\n",
            quoted_dump_path);
        g_free(quoted_dump_path);
    } else {
        script = g_strdup(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "exec \"$@\"\n");
    }

    ck_assert(g_file_set_contents(pkexec_path, script, -1, NULL));
    ck_assert_int_eq(g_chmod(pkexec_path, 0755), 0);

    g_free(script);
    g_free(pkexec_path);
    return tmp_dir;
}

static gchar *prepend_path_for_fake_pkexec(const gchar *pkexec_dir)
{
    const gchar *old_path_env = g_getenv("PATH");
    gchar *old_path = old_path_env != NULL ? g_strdup(old_path_env) : NULL;
    gchar *new_path = g_strdup_printf("%s:%s", pkexec_dir, old_path_env != NULL ? old_path_env : "");

    ck_assert(g_setenv("PATH", new_path, TRUE));

    g_free(new_path);
    return old_path;
}

static void restore_path_after_fake_pkexec(gchar *old_path)
{
    if (old_path != NULL) {
        ck_assert(g_setenv("PATH", old_path, TRUE));
        g_free(old_path);
    } else {
        g_unsetenv("PATH");
    }
}

static void remove_fake_pkexec_dir(const gchar *pkexec_dir)
{
    gchar *pkexec_path = g_build_filename(pkexec_dir, "pkexec", NULL);

    g_remove(pkexec_path);
    g_rmdir(pkexec_dir);
    g_free(pkexec_path);
}

START_TEST(test_backend_list_loaded_async_returns_modules)
{
    gchar *fake_modman = make_fake_modman_success_script();
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_list_loaded_async(NULL, FALSE, NULL, on_list_loaded_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.error);
    ck_assert_ptr_nonnull(probe.items);
    ck_assert_uint_eq(probe.items->len, 3);

    ModuleInfo *first = (ModuleInfo *)g_ptr_array_index(probe.items, 0);
    ck_assert_ptr_nonnull(first);
    ck_assert_str_eq(first->name, "firefox-esr");
    ck_assert_str_eq(first->layer, "loaded");

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_backend_list_loaded_sync_parses_five_field_machine_rows)
{
    gchar *fake_modman = make_fake_modman_five_field_script();
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    GError *error = NULL;
    GPtrArray *items = backend_list_loaded_sync("after", &error);

    ck_assert_ptr_null(error);
    ck_assert_ptr_nonnull(items);
    ck_assert_uint_eq(items->len, 2);

    ModuleInfo *first = (ModuleInfo *)g_ptr_array_index(items, 0);
    ck_assert_ptr_nonnull(first);
    ck_assert_str_eq(first->name, "krita");
    ck_assert_str_eq(first->desc, "Raster graphics editor");
    ck_assert_str_eq(first->category, "graphics");

    ModuleInfo *second = (ModuleInfo *)g_ptr_array_index(items, 1);
    ck_assert_ptr_nonnull(second);
    ck_assert_str_eq(second->name, "firefox");
    ck_assert_str_eq(second->category, "internet");

    g_ptr_array_unref(items);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_backend_load_async_missing_binary_returns_structured_error)
{
    ck_assert(g_setenv("MODMAN_BIN", "/nonexistent/modman-binary", TRUE));

    ModuleInfo module = {0};
    module.name = "firefox-esr.pfs";

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_load_async(NULL, &module, NULL, on_load_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(!probe.ok);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_BACKEND_MISSING);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
}
END_TEST

START_TEST(test_remove_local_invalid_name_no_spawn)
{
    const char *names[] = {"bad name", NULL};

    ck_assert(g_setenv("MODMAN_BIN", "/nonexistent/modman-remove-local", TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_remove_local_async(NULL, names, NULL, on_remove_local_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(!probe.ok);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_INVALID_NAME);
    ck_assert_ptr_null(probe.remove_results);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
}
END_TEST

START_TEST(test_remove_local_argv_variadic)
{
    const char *names[] = {"foo", "bar", NULL};
    gchar *argv_dump_path = g_strdup_printf("%s/modman-remove-local-argv-XXXXXX", g_get_tmp_dir());
    gchar *fake_modman;
    gchar *argv_log = NULL;
    gchar **argv_lines;
    gsize argv_log_len = 0;
    gint fd = g_mkstemp(argv_dump_path);
    gint idx_machine = -1;
    gint idx_remove_local = -1;
    gint idx_foo = -1;
    gint idx_bar = -1;

    ck_assert_int_ne(fd, -1);
    close(fd);

    fake_modman = make_fake_modman_remove_local_script(argv_dump_path);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_remove_local_async(NULL, names, NULL, on_remove_local_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(probe.ok);
    ck_assert_ptr_null(probe.error);
    ck_assert_ptr_nonnull(probe.remove_results);
    ck_assert_uint_eq(probe.remove_results->len, 2);

    ck_assert(g_file_get_contents(argv_dump_path, &argv_log, &argv_log_len, NULL));
    (void)argv_log_len;
    argv_lines = g_strsplit(argv_log, "\n", -1);

    for (gint i = 0; argv_lines[i] != NULL; i++) {
        if (argv_lines[i][0] == '\0') {
            continue;
        }

        if (g_strcmp0(argv_lines[i], "--machine") == 0) {
            idx_machine = i;
        } else if (g_strcmp0(argv_lines[i], "--remove-local") == 0) {
            idx_remove_local = i;
        } else if (g_strcmp0(argv_lines[i], "foo") == 0) {
            idx_foo = i;
        } else if (g_strcmp0(argv_lines[i], "bar") == 0) {
            idx_bar = i;
        }
    }

    ck_assert(idx_machine >= 0);
    ck_assert(idx_remove_local > idx_machine);
    ck_assert(idx_foo > idx_remove_local);
    ck_assert(idx_bar > idx_remove_local);

    g_strfreev(argv_lines);
    g_free(argv_log);
    cleanup_probe(&probe);

    g_unsetenv("MODMAN_BIN");
    g_remove(argv_dump_path);
    g_remove(fake_modman);
    g_free(argv_dump_path);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_check_updates_async_parses_update_rows)
{
    gchar *pkexec_argv_path = g_strdup_printf("%s/modman-check-updates-pkexec-argv-XXXXXX", g_get_tmp_dir());
    gint pkexec_fd = g_mkstemp(pkexec_argv_path);
    ck_assert_int_ne(pkexec_fd, -1);
    close(pkexec_fd);

    gchar *fake_modman = make_fake_modman_check_updates_script();
    gchar *pkexec_dir = make_fake_pkexec_dir(pkexec_argv_path);
    gchar *old_path = prepend_path_for_fake_pkexec(pkexec_dir);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_check_updates_async(NULL, NULL, on_check_updates_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.error);
    ck_assert_ptr_nonnull(probe.items);
    ck_assert_uint_eq(probe.items->len, 2);

    ModuleUpdateInfo *first = (ModuleUpdateInfo *)g_ptr_array_index(probe.items, 0);
    ck_assert_ptr_nonnull(first);
    ck_assert_str_eq(first->name, "firefox-esr");
    ck_assert_str_eq(first->old_version, "1.0");
    ck_assert_str_eq(first->new_version, "2.0");
    ck_assert_str_eq(first->risk, "normal");
    ck_assert_str_eq(first->message, "");
    ck_assert(!first->reboot_required);

    ModuleUpdateInfo *second = (ModuleUpdateInfo *)g_ptr_array_index(probe.items, 1);
    ck_assert_ptr_nonnull(second);
    ck_assert_str_eq(second->name, "libreoffice");
    ck_assert_str_eq(second->risk, "loaded");
    ck_assert(second->reboot_required);

    gchar *pkexec_argv_log = NULL;
    gsize pkexec_argv_log_len = 0;
    ck_assert(g_file_get_contents(pkexec_argv_path, &pkexec_argv_log, &pkexec_argv_log_len, NULL));
    if (geteuid() == 0) {
        (void)pkexec_argv_log_len;
        ck_assert_str_eq(pkexec_argv_log, "");
    } else {
        (void)pkexec_argv_log_len;
        ck_assert_msg(g_str_has_prefix(pkexec_argv_log, fake_modman),
                      "expected pkexec to receive modman binary first, got: %s",
                      pkexec_argv_log);
        ck_assert(strstr(pkexec_argv_log, "\n--machine\n") != NULL);
        ck_assert(strstr(pkexec_argv_log, "\n--check-updates\n") != NULL);
        ck_assert(strstr(pkexec_argv_log, "\n-d\n") == NULL);
        ck_assert(strstr(pkexec_argv_log, "\n--download-dir\n") == NULL);
        ck_assert(strstr(pkexec_argv_log, "\nDOWNLOAD_DIR") == NULL);
    }

    backend_update_info_array_free(probe.items);
    probe.items = NULL;
    g_clear_error(&probe.error);
    g_main_loop_unref(probe.loop);

    g_unsetenv("MODMAN_BIN");
    restore_path_after_fake_pkexec(old_path);
    g_free(pkexec_argv_log);
    g_remove(pkexec_argv_path);
    remove_fake_pkexec_dir(pkexec_dir);
    g_free(pkexec_argv_path);
    g_free(pkexec_dir);
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_update_apply_argv_confirm_flag_present_when_requested)
{
    gchar *argv_dump_path = g_strdup_printf("%s/modman-update-apply-argv-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(argv_dump_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    gchar *fake_modman = make_fake_modman_update_apply_argv_script(argv_dump_path);
    gchar *pkexec_dir = make_fake_pkexec_dir(NULL);
    gchar *old_path = prepend_path_for_fake_pkexec(pkexec_dir);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    const char *update_id = "aabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccdd";
    backend_update_apply_async(NULL, update_id, TRUE, NULL, on_update_apply_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);

    gchar *argv_log = NULL;
    gsize argv_log_len = 0;
    ck_assert(g_file_get_contents(argv_dump_path, &argv_log, &argv_log_len, NULL));
    (void)argv_log_len;

    gchar **argv_lines = g_strsplit(argv_log, "\n", -1);
    gint idx_machine = -1;
    gint idx_update = -1;
    gint idx_confirm = -1;
    gint idx_id = -1;

    for (gint i = 0; argv_lines[i] != NULL; i++) {
        if (argv_lines[i][0] == '\0') {
            continue;
        }
        if (g_strcmp0(argv_lines[i], "--machine") == 0) {
            idx_machine = i;
        } else if (g_strcmp0(argv_lines[i], "--update") == 0) {
            idx_update = i;
        } else if (g_strcmp0(argv_lines[i], "--confirm-system-updates") == 0) {
            idx_confirm = i;
        } else if (g_strcmp0(argv_lines[i], update_id) == 0) {
            idx_id = i;
        }
    }

    ck_assert(idx_machine >= 0);
    ck_assert(idx_update > idx_machine);
    ck_assert(idx_confirm > idx_machine);
    ck_assert(idx_id > idx_update);

    g_strfreev(argv_lines);
    g_free(argv_log);
    g_clear_error(&probe.error);
    g_main_loop_unref(probe.loop);

    g_unsetenv("MODMAN_BIN");
    restore_path_after_fake_pkexec(old_path);
    remove_fake_pkexec_dir(pkexec_dir);
    g_free(pkexec_dir);
    g_remove(argv_dump_path);
    g_remove(fake_modman);
    g_free(argv_dump_path);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_update_apply_argv_confirm_flag_absent_when_not_requested)
{
    gchar *argv_dump_path = g_strdup_printf("%s/modman-update-apply-noconfirm-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(argv_dump_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    gchar *fake_modman = make_fake_modman_update_apply_argv_script(argv_dump_path);
    gchar *pkexec_dir = make_fake_pkexec_dir(NULL);
    gchar *old_path = prepend_path_for_fake_pkexec(pkexec_dir);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    const char *update_id = "aabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccdd";
    backend_update_apply_async(NULL, update_id, FALSE, NULL, on_update_apply_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);

    gchar *argv_log = NULL;
    gsize argv_log_len = 0;
    ck_assert(g_file_get_contents(argv_dump_path, &argv_log, &argv_log_len, NULL));
    (void)argv_log_len;

    gchar **argv_lines = g_strsplit(argv_log, "\n", -1);
    gboolean found_confirm = FALSE;

    for (gint i = 0; argv_lines[i] != NULL; i++) {
        if (g_strcmp0(argv_lines[i], "--confirm-system-updates") == 0) {
            found_confirm = TRUE;
            break;
        }
    }

    ck_assert(!found_confirm);

    g_strfreev(argv_lines);
    g_free(argv_log);
    g_clear_error(&probe.error);
    g_main_loop_unref(probe.loop);

    g_unsetenv("MODMAN_BIN");
    restore_path_after_fake_pkexec(old_path);
    remove_fake_pkexec_dir(pkexec_dir);
    g_free(pkexec_dir);
    g_remove(argv_dump_path);
    g_remove(fake_modman);
    g_free(argv_dump_path);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_update_apply_empty_id_returns_invalid_name_error)
{
    ck_assert(g_setenv("MODMAN_BIN", "/nonexistent/modman-update-apply", TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_update_apply_async(NULL, "", FALSE, NULL, on_update_apply_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(!probe.ok);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_INVALID_NAME);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
}
END_TEST

START_TEST(test_open_file_info_async_parses_typed_row)
{
    gchar *fake_modman = make_fake_modman_file_info_script(
        "file\t/tmp/sample.pfs\tsample\tsquashfs\tloaded", 0);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/sample.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.error);
    ck_assert_ptr_nonnull(probe.open_file_info);
    ck_assert_str_eq(probe.open_file_info->path, "/tmp/sample.pfs");
    ck_assert_str_eq(probe.open_file_info->name, "sample");
    ck_assert_str_eq(probe.open_file_info->format, "squashfs");
    ck_assert_str_eq(probe.open_file_info->loaded_state, "loaded");
    ck_assert(probe.open_file_info->is_loaded);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_file_info_async_parses_modules_and_dependencies_for_details)
{
    gchar *fake_modman = make_fake_modman_file_info_script(
        "file\t/tmp/local.pfs\tlocal\tsquashfs\tlocal\t12M\tzstd 18\talpha beta\tgtk3 libfoo", 0);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/local.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.error);
    ck_assert_ptr_nonnull(probe.open_file_info);
    ck_assert_str_eq(probe.open_file_info->path, "/tmp/local.pfs");
    ck_assert_str_eq(probe.open_file_info->name, "local");
    ck_assert_str_eq(probe.open_file_info->size, "12M");
    ck_assert_str_eq(probe.open_file_info->format_detail, "zstd 18");
    ck_assert_str_eq(probe.open_file_info->modules, "alpha\nbeta");
    ck_assert_str_eq(probe.open_file_info->dependencies, "gtk3\nlibfoo");

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_file_info_async_rejects_file_row_on_nonzero_exit)
{
    gchar *fake_modman = make_fake_modman_file_info_script(
        "file\t/tmp/local.pfs\tlocal\tsquashfs\tlocal\t12M\tzstd 18\talpha beta\tgtk3 libfoo", 1);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/local.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.open_file_info);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_IO);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_file_info_async_rejects_malformed_tsv)
{
    gchar *fake_modman = make_fake_modman_file_info_script(
        "file\t/tmp/sample.pfs\tsample", 0);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/sample.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.open_file_info);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_PARSE);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_file_info_async_reads_error_row_on_nonzero_exit)
{
    gchar *fake_modman = make_fake_modman_file_info_script(
        "error\tmissing", 1);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/sample.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.open_file_info);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_IO);
    ck_assert(strstr(probe.error->message, "missing") != NULL);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_file_info_async_reports_empty_nonzero_exit_as_io_error)
{
    gchar *fake_modman = make_fake_modman_file_info_script("", 1);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);

    LoopTimeoutCtx timeout_ctx = {
        .loop = probe.loop,
        .timed_out = FALSE,
    };
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_file_info_async("/tmp/sample.pfs", NULL, on_open_file_info_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert_ptr_null(probe.open_file_info);
    ck_assert_ptr_nonnull(probe.error);
    ck_assert_uint_eq(probe.error->domain, MODMAN_GUI_ERROR_DOMAIN);
    ck_assert_int_eq(probe.error->code, MODMAN_GUI_ERR_IO);
    ck_assert(strstr(probe.error->message, "modman") != NULL);
    ck_assert(strstr(probe.error->message, "кодом 1") != NULL);

    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    g_remove(fake_modman);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_attach_path_async_uses_machine_absolute_path)
{
    gchar *argv_dump_path = g_strdup_printf("%s/modman-open-attach-argv-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(argv_dump_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    gchar *fake_modman = make_fake_modman_update_apply_argv_script(argv_dump_path);
    gchar *pkexec_dir = make_fake_pkexec_dir(NULL);
    gchar *old_path = prepend_path_for_fake_pkexec(pkexec_dir);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);
    LoopTimeoutCtx timeout_ctx = {.loop = probe.loop, .timed_out = FALSE};
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_attach_path_async("/tmp/demo-1.0.pfs", NULL, on_open_attach_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(probe.ok);
    ck_assert_ptr_null(probe.error);

    gchar *argv_log = NULL;
    gsize argv_log_len = 0;
    ck_assert(g_file_get_contents(argv_dump_path, &argv_log, &argv_log_len, NULL));
    (void)argv_log_len;
    ck_assert(strstr(argv_log, "\n--machine\n") != NULL);
    ck_assert(strstr(argv_log, "\n-S\n") != NULL);
    ck_assert(strstr(argv_log, "\n/tmp/demo-1.0.pfs\n") != NULL);

    g_free(argv_log);
    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    restore_path_after_fake_pkexec(old_path);
    remove_fake_pkexec_dir(pkexec_dir);
    g_free(pkexec_dir);
    g_remove(argv_dump_path);
    g_remove(fake_modman);
    g_free(argv_dump_path);
    g_free(fake_modman);
}
END_TEST

START_TEST(test_open_unload_path_async_derives_name_without_path)
{
    gchar *argv_dump_path = g_strdup_printf("%s/modman-open-unload-argv-XXXXXX", g_get_tmp_dir());
    gint fd = g_mkstemp(argv_dump_path);
    ck_assert_int_ne(fd, -1);
    close(fd);

    gchar *fake_modman = make_fake_modman_update_apply_argv_script(argv_dump_path);
    gchar *pkexec_dir = make_fake_pkexec_dir(NULL);
    gchar *old_path = prepend_path_for_fake_pkexec(pkexec_dir);
    ck_assert(g_setenv("MODMAN_BIN", fake_modman, TRUE));

    AsyncResultProbe probe = {0};
    probe.loop = g_main_loop_new(NULL, FALSE);
    LoopTimeoutCtx timeout_ctx = {.loop = probe.loop, .timed_out = FALSE};
    g_timeout_add_seconds(5, quit_loop_on_timeout, &timeout_ctx);

    backend_open_unload_path_async("/tmp/foo-1.0.pfs", NULL, on_open_unload_done, &probe);
    g_main_loop_run(probe.loop);

    ck_assert(!timeout_ctx.timed_out);
    ck_assert(probe.callback_fired);
    ck_assert(probe.ok);
    ck_assert_ptr_null(probe.error);

    gchar *argv_log = NULL;
    gsize argv_log_len = 0;
    ck_assert(g_file_get_contents(argv_dump_path, &argv_log, &argv_log_len, NULL));
    (void)argv_log_len;
    ck_assert(strstr(argv_log, "\n--machine\n") != NULL);
    ck_assert(strstr(argv_log, "\n-R\n") != NULL);
    ck_assert(strstr(argv_log, "\nfoo-1.0\n") != NULL);
    ck_assert(strstr(argv_log, "/tmp/foo-1.0.pfs") == NULL);

    g_free(argv_log);
    cleanup_probe(&probe);
    g_unsetenv("MODMAN_BIN");
    restore_path_after_fake_pkexec(old_path);
    remove_fake_pkexec_dir(pkexec_dir);
    g_free(pkexec_dir);
    g_remove(argv_dump_path);
    g_remove(fake_modman);
    g_free(argv_dump_path);
    g_free(fake_modman);
}
END_TEST

static Suite *backend_suite(void)
{
    Suite *suite = suite_create("backend");
    TCase *tc = tcase_create("async");

    tcase_add_test(tc, test_backend_list_loaded_async_returns_modules);
    tcase_add_test(tc, test_backend_list_loaded_sync_parses_five_field_machine_rows);
    tcase_add_test(tc, test_backend_load_async_missing_binary_returns_structured_error);
    tcase_add_test(tc, test_remove_local_invalid_name_no_spawn);
    tcase_add_test(tc, test_remove_local_argv_variadic);
    tcase_add_test(tc, test_check_updates_async_parses_update_rows);
    tcase_add_test(tc, test_update_apply_argv_confirm_flag_present_when_requested);
    tcase_add_test(tc, test_update_apply_argv_confirm_flag_absent_when_not_requested);
    tcase_add_test(tc, test_update_apply_empty_id_returns_invalid_name_error);
    tcase_add_test(tc, test_open_file_info_async_parses_typed_row);
    tcase_add_test(tc, test_open_file_info_async_parses_modules_and_dependencies_for_details);
    tcase_add_test(tc, test_open_file_info_async_rejects_file_row_on_nonzero_exit);
    tcase_add_test(tc, test_open_file_info_async_rejects_malformed_tsv);
    tcase_add_test(tc, test_open_file_info_async_reads_error_row_on_nonzero_exit);
    tcase_add_test(tc, test_open_file_info_async_reports_empty_nonzero_exit_as_io_error);
    tcase_add_test(tc, test_open_attach_path_async_uses_machine_absolute_path);
    tcase_add_test(tc, test_open_unload_path_async_derives_name_without_path);
    suite_add_tcase(suite, tc);

    return suite;
}

int main(void)
{
    int failed = 0;
    Suite *suite = backend_suite();
    SRunner *runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return failed == 0 ? 0 : 1;
}

#else

int main(void)
{
    return 0;
}

#endif
