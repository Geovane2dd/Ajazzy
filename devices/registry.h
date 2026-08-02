#ifndef AJAZZ_DEVICE_REGISTRY_H
#define AJAZZ_DEVICE_REGISTRY_H

#include "device.h"

/* Returns a display name for vid/pid, or NULL if it's not a known AJAZZ
 * device at all. Several models share a vid/pid, so this alone can't
 * tell them apart -- see ajazz_lookup_name_by_devid(). */
const char *ajazz_lookup_name(uint16_t vid, uint16_t pid);

/* dev_id comes from the device itself (AJAZZ_QUERY_DEVICE_INFO) and is
 * what actually disambiguates the exact model. */
const char *ajazz_lookup_name_by_devid(const char *dev_id);

/* AJAZZ_CAP_* bits confirmed working for this dev_id, or 0 if unknown
 * (either an unrecognised dev_id, or a listed-but-untested model). */
uint32_t ajazz_lookup_confirmed_caps(const char *dev_id);

#endif
