#ifndef AJAZZ_DEVICE_H
#define AJAZZ_DEVICE_H

#include <stdint.h>

/*
 * Shared type for every device module in this directory. Each mouse
 * gets its own devices/<model>.h file with a small table of these --
 * see devices/aj179v2max.h for the one model this project was actually
 * reverse engineered against, and registry.c for how the tables get
 * pulled together.
 *
 * The wire protocol (protocol/hid.c) is the same across the whole AJAZZ
 * lineup: they all go through the same DriverComm class and vendor HID
 * interface in the original Windows driver, and config.xml (shipped
 * inside that installer) lists identical command families for every
 * model. What differs per model is just which VID/PID pairs it shows up
 * as and, so far, nothing else we've had to special-case. So this table
 * is metadata for detection/naming plus an honesty flag (confirmed),
 * not a place to plug in different protocol code.
 */

/* one bit per feature we can actually test against real hardware */
typedef enum {
    AJAZZ_CAP_DPI     = 1u << 0, /* dpi table, active stage           */
    AJAZZ_CAP_RATE    = 1u << 1, /* report rate / polling rate        */
    AJAZZ_CAP_RGB     = 1u << 2, /* lighting effects                  */
    AJAZZ_CAP_SENSOR  = 1u << 3, /* lift-off distance, angle snapping */
    AJAZZ_CAP_KEYS    = 1u << 4, /* button remap (consumer/keyboard)  */
    AJAZZ_CAP_BATTERY = 1u << 5, /* battery level                     */
    AJAZZ_CAP_MACRO   = 1u << 6, /* macro content + button assignment */
} ajazz_capability_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char *dev_id;  /* reported by the device itself over the wire,
                           * needed because several models share a vid/pid */
    const char *name;
    const char *mode;    /* "USB" or "2.4G" */
    uint32_t confirmed;  /* AJAZZ_CAP_* bits actually verified on real
                           * hardware for this model -- 0 means "should
                           * work in theory, nobody's tried it yet" */
} ajazz_device_entry_t;

#endif
