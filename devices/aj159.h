#ifndef AJAZZ_DEVICE_AJ159_H
#define AJAZZ_DEVICE_AJ159_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ159 yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj159[] = {
    {0x248A, 0x5C2E, "M620", "AJ159", "USB", 0},
    {0x248A, 0x5D2E, "M620", "AJ159", "USB", 0},
    {0x248A, 0x5E2E, "M620", "AJ159", "USB", 0},
    {0x248A, 0x5C2F, "M620", "AJ159", "2.4G", 0},
    {0x249A, 0x5C2F, "M620", "AJ159", "2.4G", 0},
    {0x248A, 0x5C2E, "P630", "AJ159", "USB", 0},
    {0x248A, 0x5D2E, "P630", "AJ159", "USB", 0},
    {0x248A, 0x5E2E, "P630", "AJ159", "USB", 0},
    {0x248A, 0x5C2F, "P630", "AJ159", "2.4G", 0},
    {0x249A, 0x5C2F, "P630", "AJ159", "2.4G", 0},
    {0x248A, 0x5B2E, "M620", "AJ159", "USB", 0},
    {0x248A, 0x5B2F, "M620", "AJ159", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ159_COUNT (sizeof(ajazz_device_aj159) / sizeof(ajazz_device_aj159[0]))

#endif
