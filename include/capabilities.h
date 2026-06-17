#ifndef MODMAN_CAPABILITIES_H
#define MODMAN_CAPABILITIES_H

#if __has_include(<glib.h>)
#include <glib.h>
#else
typedef int gboolean;
#endif

#include "model.h"

typedef enum CapabilityLayeringMode {
    CAP_LAYERING_UNKNOWN = 0,
    CAP_LAYERING_AUFS,
    CAP_LAYERING_OVERLAY,
    CAP_LAYERING_NONE
} CapabilityLayeringMode;

typedef struct AppCapabilities {
    gboolean has_modman;
    gboolean has_pfsinfo;
    gboolean has_systemd;
    gboolean has_aufs;
    gboolean has_overlayfs;
    gboolean has_pkexec;
    gboolean has_erofs;
    CapabilityLayeringMode layering_mode;
} AppCapabilities;

void capabilities_detect(AppCapabilities *out_caps, const ModmanConf *conf);
gboolean capabilities_supports_runtime_ops(const AppCapabilities *caps);
gboolean capabilities_has_critical_missing(const AppCapabilities *caps);
const char *capabilities_layering_mode_name(CapabilityLayeringMode mode);

#endif
