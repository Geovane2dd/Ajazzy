#ifndef AJAZZ_DEVICE_AJ139V2PRO_H
#define AJAZZ_DEVICE_AJ139V2PRO_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ139 V2 PRO yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj139v2pro[] = {
    {0x248A, 0x5C2E, "M139", "AJ139 V2 PRO", "USB", 0},
    {0x248A, 0x5E2E, "M139", "AJ139 V2 PRO", "USB", 0},
    {0x248A, 0x5C2F, "M139", "AJ139 V2 PRO", "2.4G", 0},
    {0x249A, 0x5C2F, "M139", "AJ139 V2 PRO", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ139V2PRO_COUNT (sizeof(ajazz_device_aj139v2pro) / sizeof(ajazz_device_aj139v2pro[0]))

#endif
