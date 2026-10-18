#ifndef AJAZZ_DEVICE_REGISTRY_H
#define AJAZZ_DEVICE_REGISTRY_H

#include "device.h"

/* Returns a display name for vid/pid, or NULL if it's not a known AJAZZ
 * device at all. Several models share a vid/pid, so this alone can't
 * tell them apart -- see ajazz_lookup_name_by_devid(). */
const char *ajazz_lookup_name(uint16_t vid, uint16_t pid);

/* "USB" or "2.4G" for a known vid/pid, or NULL if unrecognised. */
const char *ajazz_lookup_mode(uint16_t vid, uint16_t pid);

/* dev_id comes from the device itself (AJAZZ_QUERY_DEVICE_INFO) and is
 * what actually disambiguates the exact model. */
const char *ajazz_lookup_name_by_devid(const char *dev_id);

/* AJAZZ_CAP_* bits confirmed working for this dev_id, or 0 if unknown
 * (either an unrecognised dev_id, or a listed-but-untested model). */
uint32_t ajazz_lookup_confirmed_caps(const char *dev_id);

/* Picture to show for a model: "aj179", "aj139pro", "aj159" or "generic"
 * (file gui/images/mouse_<name>.png). `name` is the model name from the
 * device table. */
const char *ajazz_lookup_image(const char *model_name);

/* Bitmask of lighting modes (1 << AJAZZ_LIGHT_*) the model's firmware
 * actually runs. The official driver's per-model device/mouse_*.xml has
 * <light_N enable="0|1">; for every model whose file ships in the installer
 * (AJ139 PRO, AJ159, AJ159 MC, AJ179) only off, breathing and steady are
 * enabled -- flowing, neon and colour wave are accepted by the firmware but
 * do nothing visible. Models we have no file for get the same safe set. */
uint32_t ajazz_lookup_light_modes(const char *dev_id);

#endif
