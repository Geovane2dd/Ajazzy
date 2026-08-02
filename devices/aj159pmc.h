#ifndef AJAZZ_DEVICE_AJ159PMC_H
#define AJAZZ_DEVICE_AJ159PMC_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ159P MC yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj159pmc[] = {
    {0x248A, 0x5C2E, "N620", "AJ159P MC", "USB", 0},
    {0x248A, 0x5D2E, "N620", "AJ159P MC", "USB", 0},
    {0x248A, 0x5E2E, "N620", "AJ159P MC", "USB", 0},
    {0x248A, 0x5C2F, "N620", "AJ159P MC", "2.4G", 0},
    {0x249A, 0x5C2F, "N620", "AJ159P MC", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ159PMC_COUNT (sizeof(ajazz_device_aj159pmc) / sizeof(ajazz_device_aj159pmc[0]))

#endif
