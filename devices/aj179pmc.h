#ifndef AJAZZ_DEVICE_AJ179PMC_H
#define AJAZZ_DEVICE_AJ179PMC_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ179P MC yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj179pmc[] = {
    {0x248A, 0x5C2E, "179P", "AJ179P MC", "USB", 0},
    {0x248A, 0x5D2E, "179P", "AJ179P MC", "USB", 0},
    {0x248A, 0x5E2E, "179P", "AJ179P MC", "USB", 0},
    {0x248A, 0x5C2F, "179P", "AJ179P MC", "2.4G", 0},
    {0x249A, 0x5C2F, "179P", "AJ179P MC", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ179PMC_COUNT (sizeof(ajazz_device_aj179pmc) / sizeof(ajazz_device_aj179pmc[0]))

#endif
