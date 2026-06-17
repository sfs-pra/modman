#include "../include/capabilities.h"

#include <string.h>
#include <unistd.h>

#if __has_include(<glib.h>)
#include <glib.h>
#endif

static gboolean env_is_enabled(const char *name)
{
    const char *value = g_getenv(name);

    if (value == NULL || value[0] == '\0') {
        return FALSE;
    }

    return g_ascii_strcasecmp(value, "1") == 0 ||
           g_ascii_strcasecmp(value, "true") == 0 ||
           g_ascii_strcasecmp(value, "yes") == 0 ||
           g_ascii_strcasecmp(value, "on") == 0;
}

static gboolean probe_binary(const char *path_or_name)
{
    char *resolved;
    gboolean ok;

    if (path_or_name == NULL || path_or_name[0] == '\0') {
        return FALSE;
    }

    if (strchr(path_or_name, '/') != NULL) {
        return access(path_or_name, X_OK) == 0;
    }

    resolved = g_find_program_in_path(path_or_name);
    ok = (resolved != NULL);
    g_free(resolved);
    return ok;
}

static gboolean probe_proc_filesystem(const char *name)
{
    char *content = NULL;
    gsize content_len = 0U;
    gboolean found = FALSE;
    GError *error = NULL;

    if (name == NULL || name[0] == '\0') {
        return FALSE;
    }

    if (!g_file_get_contents("/proc/filesystems", &content, &content_len, &error)) {
        if (error != NULL) {
            g_error_free(error);
        }
        return FALSE;
    }

    {
        char **lines = g_strsplit(content, "\n", -1);
        guint i;

        for (i = 0U; lines != NULL && lines[i] != NULL; i++) {
            char **tokens = g_strsplit_set(lines[i], "\t ", -1);
            char *last = NULL;
            guint j;

            for (j = 0U; tokens != NULL && tokens[j] != NULL; j++) {
                if (tokens[j][0] != '\0') {
                    last = tokens[j];
                }
            }

            if (last != NULL && g_strcmp0(last, name) == 0) {
                found = TRUE;
                g_strfreev(tokens);
                break;
            }

            g_strfreev(tokens);
        }

        g_strfreev(lines);
    }

    g_free(content);
    return found;
}

static CapabilityLayeringMode parse_layering_mode(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return CAP_LAYERING_UNKNOWN;
    }

    if (g_ascii_strcasecmp(value, "aufs") == 0) {
        return CAP_LAYERING_AUFS;
    }

    if (g_ascii_strcasecmp(value, "overlay") == 0 ||
        g_ascii_strcasecmp(value, "overlayfs") == 0) {
        return CAP_LAYERING_OVERLAY;
    }

    if (g_ascii_strcasecmp(value, "none") == 0) {
        return CAP_LAYERING_NONE;
    }

    return CAP_LAYERING_UNKNOWN;
}

const char *capabilities_layering_mode_name(CapabilityLayeringMode mode)
{
    switch (mode) {
        case CAP_LAYERING_AUFS:
            return "aufs";
        case CAP_LAYERING_OVERLAY:
            return "overlay";
        case CAP_LAYERING_NONE:
            return "none";
        case CAP_LAYERING_UNKNOWN:
        default:
            return "unknown";
    }
}

void capabilities_detect(AppCapabilities *out_caps, const ModmanConf *conf)
{
    const char *layering_env;
    CapabilityLayeringMode mock_mode;

    if (out_caps == NULL) {
        return;
    }

    memset(out_caps, 0, sizeof(*out_caps));

    out_caps->has_modman = probe_binary(conf != NULL ? conf->modman_bin : NULL);
    out_caps->has_pfsinfo = probe_binary("pfsinfo");
    out_caps->has_systemd = probe_binary("systemctl") && access("/run/systemd/system", F_OK) == 0;
    out_caps->has_aufs = access("/sys/fs/aufs", F_OK) == 0 || probe_proc_filesystem("aufs");
    out_caps->has_overlayfs = access("/sys/module/overlay", F_OK) == 0 || probe_proc_filesystem("overlay");
    out_caps->has_pkexec = probe_binary("pkexec");
    out_caps->has_erofs = probe_proc_filesystem("erofs") || probe_binary("mkfs.erofs");

    if (env_is_enabled("CAPS_MOCK_NO_MODMAN")) {
        out_caps->has_modman = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_PFSINFO")) {
        out_caps->has_pfsinfo = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_SYSTEMD")) {
        out_caps->has_systemd = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_AUFS")) {
        out_caps->has_aufs = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_OVERLAYFS")) {
        out_caps->has_overlayfs = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_PKEXEC")) {
        out_caps->has_pkexec = FALSE;
    }
    if (env_is_enabled("CAPS_MOCK_NO_EROFS")) {
        out_caps->has_erofs = FALSE;
    }

    out_caps->layering_mode = CAP_LAYERING_NONE;
    if (conf != NULL && conf->layering_override != NULL && conf->layering_override[0] != '\0') {
        CapabilityLayeringMode override_mode = parse_layering_mode(conf->layering_override);
        if (override_mode == CAP_LAYERING_AUFS && out_caps->has_aufs) {
            out_caps->layering_mode = CAP_LAYERING_AUFS;
        } else if (override_mode == CAP_LAYERING_OVERLAY && out_caps->has_overlayfs) {
            out_caps->layering_mode = CAP_LAYERING_OVERLAY;
        }
    }

    if (out_caps->layering_mode == CAP_LAYERING_NONE) {
        if (out_caps->has_aufs) {
            out_caps->layering_mode = CAP_LAYERING_AUFS;
        } else if (out_caps->has_overlayfs) {
            out_caps->layering_mode = CAP_LAYERING_OVERLAY;
        }
    }

    layering_env = g_getenv("CAPS_MOCK_LAYERING_MODE");
    mock_mode = parse_layering_mode(layering_env);
    if (mock_mode != CAP_LAYERING_UNKNOWN) {
        out_caps->layering_mode = mock_mode;
    }
}

gboolean capabilities_supports_runtime_ops(const AppCapabilities *caps)
{
    if (caps == NULL) {
        return FALSE;
    }

    return caps->has_modman && caps->has_pfsinfo && caps->layering_mode != CAP_LAYERING_NONE;
}

gboolean capabilities_has_critical_missing(const AppCapabilities *caps)
{
    return !capabilities_supports_runtime_ops(caps);
}
