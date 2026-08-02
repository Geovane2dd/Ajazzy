#ifndef AJAZZ_DEVICE_AJ179_H
#define AJAZZ_DEVICE_AJ179_H

#include "device.h"

/* Listed in the original driver's config.xml alongside the AJ179 V2 MAX,
 * and almost certainly uses the exact same protocol -- but nobody has
 * confirmed that against a real AJ179 yet. If you have one,
 * please try it and open a PR updating the `confirmed` flags here. */
static const ajazz_device_entry_t ajazz_device_aj179[] = {
    {0x248A, 0x5C2E, "M179", "AJ179", "USB", 0},
    {0x248A, 0x5D2E, "M179", "AJ179", "USB", 0},
    {0x248A, 0x5E2E, "M179", "AJ179", "USB", 0},
    {0x248A, 0x5C2F, "M179", "AJ179", "2.4G", 0},
    {0x249A, 0x5C2F, "M179", "AJ179", "2.4G", 0},
    {0x248A, 0x5C2E, "P179", "AJ179", "USB", 0},
    {0x248A, 0x5D2E, "P179", "AJ179", "USB", 0},
    {0x248A, 0x5E2E, "P179", "AJ179", "USB", 0},
    {0x248A, 0x5C2F, "P179", "AJ179", "2.4G", 0},
    {0x249A, 0x5C2F, "P179", "AJ179", "2.4G", 0},
    {0x248A, 0x5B2E, "M184", "AJ179", "USB", 0},
    {0x248A, 0x5B2F, "M184", "AJ179", "2.4G", 0},
};
#define AJAZZ_DEVICE_AJ179_COUNT (sizeof(ajazz_device_aj179) / sizeof(ajazz_device_aj179[0]))

#endif
