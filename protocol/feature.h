#ifndef AJAZZ_FEATURE_H
#define AJAZZ_FEATURE_H

/*
 * Second wire protocol: models with device_type >= 200 in the official
 * driver's config.xml (e.g. AJ159 dev_id M620 on dongle 248a:5b2f "SSCYPL").
 * Their MI_02 is the firmware-update interface (vendor page 0xFFEF, 64-byte
 * report 5) -- never send settings there. Settings go to MI_01 as a 33-byte
 * FEATURE report, ID 0x0C (descriptor "85 0c ... 95 20 b1"):
 *
 *   0C 01 <cmd> 00 <seq> <chunk> <len> <payload, zero padded>
 *
 * No checksum and no ack. The only read is cmd 0x20 (identify: dev_id at
 * bytes 8..11, battery % at 18, mouse-online flag at 21) answered through
 * GET_FEATURE. Decompiled from AJAZZ Driver (X) 1.0.5.3 (setters gated on
 * device_type >= 0xc9, sent by HidD_SetFeature(h, buf, 0x21)).
 *
 * Because the mouse can't report its settings, the rest of Ajazzy's GET
 * queries are answered from a local cache of the last values written,
 * stored in the old 32-byte reply layout so the CLI/GUI work unchanged.
 */

#include <stdint.h>
#include "hid.h"

#define AJAZZ_FEATURE_LEN 33
#define AJAZZ_FEATURE_ID 0x0C

/* Translates one old-style SET report into feature frames. Returns the
 * number of frames written to out (at most max), or -1 if this protocol has
 * no equivalent (errno = ENOTSUP). `cached` is the cached reply for the
 * matching query (may be NULL); key-table writes only send changed slots. */
int ajazz_feature_translate(const ajazz_report_t *r, const ajazz_report_t *cached,
    uint8_t out[][AJAZZ_FEATURE_LEN], int max, uint8_t *seq);

/* 1 if this hidraw report descriptor is the feature-0x0C settings interface. */
int ajazz_feature_is_interface(const uint8_t *desc, int size);

int ajazz_feature_send(int fd, const char *key, const ajazz_report_t *r);
int ajazz_feature_query(int fd, const char *key, uint8_t query_id, ajazz_report_t *reply);

#endif
