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
    char mode[8];        /* "USB" or "2.4G", from the device table entry that
                             matched the interface ajazz_open() actually
                             picked. ajazz_open() prefers "USB" over "2.4G"
                             when both are present: SET/GET round-trips over
                             the 2.4G dongle aren't reliable once the mouse
                             is also on its charge cable, while USB stays
                             fine. */
    int charging;        /* 1 if a "USB"-mode hidraw node was present at
                             ajazz_open() time -- since mode is normally
                             "USB" too in that situation (see above), this
                             is mostly redundant with mode, but is scanned
                             independently so it stays correct even if the
                             preference above ever changes. */
} ajazz_dev_t;

/* Scans /dev/hidraw* for a known AJAZZ VID/PID whose report descriptor
 * advertises the vendor usage page (0xFFEF), which is the interface
 * (MI_02) the configuration protocol talks to. Returns 0 on success. */
int ajazz_open(ajazz_dev_t *dev);
void ajazz_close(ajazz_dev_t *dev);

/* Cheap re-check (directory scan only, no open()/query) for whether a
 * different interface is now the preferred one -- e.g. the mouse's USB
 * cable got plugged in after an existing "2.4G" connection was already
 * open, which doesn't make that 2.4G node disappear, so a caller who
 * only checks "is my current path still there" (like a naive
 * reconnect-on-unplug check) would never notice and would keep talking
 * to the dongle indefinitely. Returns 0 if no config interface is
 * present; otherwise fills path_out (>= 280 bytes) with the currently-
 * preferred path and *charging_out with whether a "USB" node exists. */
int ajazz_best_path(char *path_out, size_t len, int *charging_out);

/* Why can't we talk to the mouse? Looks at /sys/class/hidraw for AJAZZ
 * devices and tries to open their nodes, so "unplugged" and "plugged in but
 * your user may not touch /dev/hidraw*" can be told apart -- the second one
 * needs the udev rule, the first one needs a cable. */
typedef enum {
    AJAZZ_ACCESS_NO_DEVICE = 0, /* no known AJAZZ hidraw node at all */
    AJAZZ_ACCESS_OK = 1,        /* at least one node can be opened */
    AJAZZ_ACCESS_DENIED = 2,    /* nodes exist but none can be opened (permissions) */
} ajazz_access_t;
ajazz_access_t ajazz_probe_access(void);

/* The shell command that installs the udev rule and applies it to devices
 * that are already plugged in. Static string, never freed. */
const char *ajazz_udev_fix_command(void);

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
