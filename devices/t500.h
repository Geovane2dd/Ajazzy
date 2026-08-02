#ifndef AJAZZ_DEVICE_T500_H
#define AJAZZ_DEVICE_T500_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real T500 yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_t500[] = {
    {0x248A, 0x5C2E, "T500", "T500", "USB", 0},
    {0x248A, 0x5D2E, "T500", "T500", "USB", 0},
    {0x248A, 0x5E2E, "T500", "T500", "USB", 0},
    {0x248A, 0x5C2F, "T500", "T500", "2.4G", 0},
    {0x249A, 0x5C2F, "T500", "T500", "2.4G", 0},
};
#define AJAZZ_DEVICE_T500_COUNT (sizeof(ajazz_device_t500) / sizeof(ajazz_device_t500[0]))

#endif
