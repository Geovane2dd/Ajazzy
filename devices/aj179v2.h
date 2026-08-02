#ifndef AJAZZ_DEVICE_AJ179V2_H
#define AJAZZ_DEVICE_AJ179V2_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ179 V2 yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj179v2[] = {
    {0x248A, 0x5C2E, "N624", "AJ179 V2", "USB", 0},
    {0x248A, 0x5D2E, "N624", "AJ179 V2", "USB", 0},
    {0x248A, 0x5E2E, "N624", "AJ179 V2", "USB", 0},
    {0x248A, 0x5C2F, "N624", "AJ179 V2", "2.4G", 0},
    {0x249A, 0x5C2F, "N624", "AJ179 V2", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ179V2_COUNT (sizeof(ajazz_device_aj179v2) / sizeof(ajazz_device_aj179v2[0]))

#endif
