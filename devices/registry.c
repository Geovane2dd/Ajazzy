#include <string.h>
#include <stddef.h>

#include "registry.h"

/*
 * This is the one file you need to touch (besides adding your own
 * devices/<model>.h) to plug a new AJAZZ mouse into the project. Copy
 * an existing header like devices/aj159.h as a starting point -- the
 * vid/pid/dev_id values come straight out of config.xml inside the
 * official Windows installer, which you can pull with innoextract.
 * Leave `confirmed` at 0 until you've actually tested each feature
 * against your hardware.
 */

#include "aj139pro.h"
#include "aj139v2pro.h"
#include "aj159.h"
#include "aj159mc.h"
#include "aj159pmc.h"
#include "aj179.h"
#include "aj179pmc.h"
#include "aj179v2.h"
#include "aj179v2max.h"
#include "t500.h"

typedef struct {
    const ajazz_device_entry_t *entries;
    size_t count;
} ajazz_device_group_t;

static const ajazz_device_group_t groups[] = {
    { ajazz_device_aj139pro,   AJAZZ_DEVICE_AJ139PRO_COUNT },
    { ajazz_device_aj139v2pro, AJAZZ_DEVICE_AJ139V2PRO_COUNT },
    { ajazz_device_aj159,      AJAZZ_DEVICE_AJ159_COUNT },
    { ajazz_device_aj159mc,    AJAZZ_DEVICE_AJ159MC_COUNT },
    { ajazz_device_aj159pmc,   AJAZZ_DEVICE_AJ159PMC_COUNT },
    { ajazz_device_aj179,      AJAZZ_DEVICE_AJ179_COUNT },
    { ajazz_device_aj179pmc,   AJAZZ_DEVICE_AJ179PMC_COUNT },
    { ajazz_device_aj179v2,    AJAZZ_DEVICE_AJ179V2_COUNT },
    { ajazz_device_aj179v2max, AJAZZ_DEVICE_AJ179V2MAX_COUNT },
    { ajazz_device_t500,       AJAZZ_DEVICE_T500_COUNT },
};
#define N_GROUPS (sizeof(groups) / sizeof(groups[0]))

const char *ajazz_lookup_name(uint16_t vid, uint16_t pid)
{
    for (size_t g = 0; g < N_GROUPS; g++) {
        for (size_t i = 0; i < groups[g].count; i++) {
            const ajazz_device_entry_t *e = &groups[g].entries[i];
            if (e->vid == vid && e->pid == pid)
                return e->name;
        }
    }
    return NULL;
}

const char *ajazz_lookup_name_by_devid(const char *dev_id)
{
    for (size_t g = 0; g < N_GROUPS; g++) {
        for (size_t i = 0; i < groups[g].count; i++) {
            const ajazz_device_entry_t *e = &groups[g].entries[i];
            if (strcmp(e->dev_id, dev_id) == 0)
                return e->name;
        }
    }
    return NULL;
}

uint32_t ajazz_lookup_confirmed_caps(const char *dev_id)
{
    for (size_t g = 0; g < N_GROUPS; g++) {
        for (size_t i = 0; i < groups[g].count; i++) {
            const ajazz_device_entry_t *e = &groups[g].entries[i];
            if (strcmp(e->dev_id, dev_id) == 0)
                return e->confirmed;
        }
    }
    return 0;
}
