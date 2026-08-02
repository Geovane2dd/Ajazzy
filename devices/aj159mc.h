#ifndef AJAZZ_DEVICE_AJ159MC_H
#define AJAZZ_DEVICE_AJ159MC_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ159 MC yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj159mc[] = {
    {0x248A, 0x5C2E, "M630", "AJ159 MC", "USB", 0},
    {0x248A, 0x5D2E, "M630", "AJ159 MC", "USB", 0},
    {0x248A, 0x5E2E, "M630", "AJ159 MC", "USB", 0},
    {0x248A, 0x5C2F, "M630", "AJ159 MC", "2.4G", 0},
    {0x249A, 0x5C2F, "M630", "AJ159 MC", "2.4G", 0},
    {0x248A, 0x5B2E, "M620", "AJ159 MC", "USB", 0},
    {0x248A, 0x5B2F, "M620", "AJ159 MC", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ159MC_COUNT (sizeof(ajazz_device_aj159mc) / sizeof(ajazz_device_aj159mc[0]))

#endif
