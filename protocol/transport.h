#ifndef AJAZZ_TRANSPORT_H
#define AJAZZ_TRANSPORT_H

#include <stdint.h>
#include "hid.h"

typedef struct {
    int fd;
    uint16_t vid;
    uint16_t pid;
    char path[280];
    char name[256];
    char dev_id[8];
    uint32_t confirmed;  /* AJAZZ_CAP_* bits actually tested for this model, 0 if unknown */
} ajazz_dev_t;

/* Scans /dev/hidraw* for a known AJAZZ VID/PID whose report descriptor
 * advertises the vendor usage page (0xFFEF), which is the interface
 * (MI_02) the configuration protocol talks to. Returns 0 on success. */
int ajazz_open(ajazz_dev_t *dev);
void ajazz_close(ajazz_dev_t *dev);

/* Sends a 32 byte report and returns 0 on success. */
int ajazz_send(ajazz_dev_t *dev, const ajazz_report_t *r);

/* Reads one 32 byte report with a timeout (milliseconds). Returns 0 on
 * success, -1 on error/timeout. */
int ajazz_recv(ajazz_dev_t *dev, ajazz_report_t *r, int timeout_ms);

/* Sends a query and waits for the matching reply (same family byte). */
int ajazz_query(ajazz_dev_t *dev, uint8_t query_id, ajazz_report_t *reply, int timeout_ms);

/* Sends a SET-style report and waits for the device's ack echo. */
int ajazz_send_and_wait(ajazz_dev_t *dev, const ajazz_report_t *r, int timeout_ms);

/* prints a one-line warning to stderr if `cap` isn't in dev->confirmed --
 * doesn't block anything, just makes clear you're off the beaten path */
void ajazz_warn_if_unconfirmed(const ajazz_dev_t *dev, uint32_t cap, const char *feature_name);

#endif
