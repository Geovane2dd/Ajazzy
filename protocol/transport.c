#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "transport.h"
#include "../devices/registry.h"

/* The AJ179 V2 MAX's configuration interface (MI_02) does not advertise a
 * vendor usage page in its report descriptor -- it uses Generic Desktop
 * with an undefined usage, and is identified instead by declaring a
 * 32-byte OUTPUT report: "Report Count(32) ... Output(...)", i.e. the
 * byte sequence 95 20 ... 91 in the descriptor. This matches the 32 byte
 * reports observed on the wire in the USB capture. */
static int is_config_interface(int fd)
{
    struct hidraw_report_descriptor rpt_desc;
    int size = 0;

    if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0)
        return 0;

    memset(&rpt_desc, 0, sizeof(rpt_desc));
    rpt_desc.size = size;
    if (ioctl(fd, HIDIOCGRDESC, &rpt_desc) < 0)
        return 0;

    for (int i = 0; i + 2 < size; i++) {
        if (rpt_desc.value[i] == 0x95 &&
            rpt_desc.value[i + 1] == AJAZZ_REPORT_LEN &&
            rpt_desc.value[i + 2] == 0x91)
            return 1;
    }
    return 0;
}

/* A single candidate config-interface hidraw node found during the scan
 * below, kept just long enough to pick the best one -- see ajazz_open(). */
typedef struct {
    char path[280];
    uint16_t vid;
    uint16_t pid;
    const char *name;
    const char *mode; /* "USB", "2.4G", or NULL if unlisted */
} ajazz_candidate_t;

/* Scans /dev for every present config-interface hidraw node. When the
 * mouse's charge cable is plugged in while its 2.4G dongle is also
 * still present, BOTH show up as separate nodes at once -- collecting
 * all of them (instead of stopping at the first match) is what lets
 * ajazz_pick_best() below choose between them and what lets
 * ajazz_scan_charging() detect "USB" presence independently of which
 * one ends up used. Returns the candidate count. */
static int ajazz_scan(ajazz_candidate_t *candidates, int max)
{
    DIR *d = opendir("/dev");
    if (!d) {
        perror("opendir /dev");
        return 0;
    }

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0)
            continue;
        if (n >= max)
            break;

        char path[280];
        snprintf(path, sizeof(path), "/dev/%s", ent->d_name);

        int fd = open(path, O_RDWR);
        if (fd < 0)
            continue;

        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            continue;
        }

        uint16_t vid = (uint16_t)info.vendor;
        uint16_t pid = (uint16_t)info.product;
        const char *name = ajazz_lookup_name(vid, pid);

        if (name && is_config_interface(fd)) {
            ajazz_candidate_t *c = &candidates[n++];
            snprintf(c->path, sizeof(c->path), "%s", path);
            c->vid = vid;
            c->pid = pid;
            c->name = name;
            c->mode = ajazz_lookup_mode(vid, pid);
        }
        close(fd);
    }
    closedir(d);
    return n;
}

/* Prefer "USB" over "2.4G" when both are present. With the charge cable
 * plugged in and the dongle still paired, DPI colour (family 0x04/query
 * 0x14) SETs ack over the dongle but never stick -- GET reads back
 * all-zero regardless. The exact same round-trip works fine over USB.
 * That's worth more than a perfectly accurate battery reading over
 * "2.4G": the battery is still readable over USB, just running slightly
 * high while charging, and that's already surfaced via dev.charging /
 * the "(charging)" label rather than left silently misleading. */
static const ajazz_candidate_t *ajazz_pick_best(const ajazz_candidate_t *candidates, int n)
{
    const ajazz_candidate_t *best = &candidates[0];
    for (int i = 1; i < n; i++) {
        if (best->mode && strcmp(best->mode, "USB") == 0)
            break; /* already have the preferred kind */
        if (candidates[i].mode && strcmp(candidates[i].mode, "USB") == 0)
            best = &candidates[i];
    }
    return best;
}

/* Lightweight check (no open()/query -- just the same directory scan
 * ajazz_open() does) for whether a DIFFERENT, now-preferred interface
 * has appeared since the last ajazz_open(), and for the live charging
 * state -- both need to be re-evaluated periodically even while an
 * existing connection is still technically alive, since a currently-
 * open "2.4G" node doesn't vanish just because the mouse's USB cable
 * gets plugged in afterward (see the comment on ensure_connected() in
 * gui/main.c, which is what actually calls this). Returns 0 if no
 * config interface is present at all; otherwise fills path_out (must be
 * at least 280 bytes) with the currently-best candidate's path and
 * *charging_out with whether any "USB" node exists. */
int ajazz_best_path(char *path_out, size_t len, int *charging_out)
{
    ajazz_candidate_t candidates[8];
    int n = ajazz_scan(candidates, (int)(sizeof(candidates) / sizeof(candidates[0])));
    if (n == 0)
        return 0;

    int charging = 0;
    for (int i = 0; i < n; i++) {
        if (candidates[i].mode && strcmp(candidates[i].mode, "USB") == 0)
            charging = 1;
    }
    if (charging_out)
        *charging_out = charging;

    const ajazz_candidate_t *best = ajazz_pick_best(candidates, n);
    snprintf(path_out, len, "%s", best->path);
    return 1;
}

int ajazz_open(ajazz_dev_t *dev)
{
    ajazz_candidate_t candidates[8];
    int n_candidates = ajazz_scan(candidates, (int)(sizeof(candidates) / sizeof(candidates[0])));

    if (n_candidates > 0) {
        const ajazz_candidate_t *best = ajazz_pick_best(candidates, n_candidates);
        int charging = 0;
        for (int i = 0; i < n_candidates; i++) {
            if (candidates[i].mode && strcmp(candidates[i].mode, "USB") == 0)
                charging = 1;
        }

        int fd = open(best->path, O_RDWR);
        if (fd >= 0) {
            dev->fd = fd;
            dev->vid = best->vid;
            dev->pid = best->pid;
            snprintf(dev->path, sizeof(dev->path), "%s", best->path);
            snprintf(dev->name, sizeof(dev->name), "%s", best->name);
            snprintf(dev->mode, sizeof(dev->mode), "%s", best->mode ? best->mode : "");
            dev->charging = charging;

            dev->dev_id[0] = '\0';
            dev->confirmed = 0;

            /* several models share the same vid/pid; ask the device
             * itself for its dev_id to get the exact model name and
             * find out which features (if any) are confirmed to work
             * on it. */
            ajazz_report_t reply;
            if (ajazz_query(dev, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000) == 0) {
                memcpy(dev->dev_id, reply.payload, 4);
                dev->dev_id[4] = '\0';

                const char *precise = ajazz_lookup_name_by_devid(dev->dev_id);
                if (precise)
                    strncpy(dev->name, precise, sizeof(dev->name) - 1);

                dev->confirmed = ajazz_lookup_confirmed_caps(dev->dev_id);
            }

            return 0;
        }
    }

    errno = ENODEV;
    return -1;
}

void ajazz_close(ajazz_dev_t *dev)
{
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

int ajazz_send(ajazz_dev_t *dev, const ajazz_report_t *r)
{
    ssize_t n = write(dev->fd, r, AJAZZ_REPORT_LEN);
    return (n == AJAZZ_REPORT_LEN) ? 0 : -1;
}

int ajazz_recv(ajazz_dev_t *dev, ajazz_report_t *r, int timeout_ms)
{
    struct pollfd pfd = { .fd = dev->fd, .events = POLLIN };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0)
        return -1;

    ssize_t n = read(dev->fd, r, AJAZZ_REPORT_LEN);
    return (n == AJAZZ_REPORT_LEN) ? 0 : -1;
}

int ajazz_query(ajazz_dev_t *dev, uint8_t query_id, ajazz_report_t *reply, int timeout_ms)
{
    ajazz_report_t req;
    ajazz_build_query(&req, query_id);

    if (ajazz_send(dev, &req) != 0)
        return -1;

    /* the interrupt IN endpoint also carries mouse movement reports on a
     * different report id, so drain until we see our query id echoed back
     * or we time out. */
    for (;;) {
        if (ajazz_recv(dev, reply, timeout_ms) != 0)
            return -1;
        if (reply->family == query_id)
            return 0;
    }
}

int ajazz_send_and_wait(ajazz_dev_t *dev, const ajazz_report_t *r, int timeout_ms)
{
    ajazz_report_t reply;

    if (ajazz_send(dev, r) != 0)
        return -1;

    for (;;) {
        if (ajazz_recv(dev, &reply, timeout_ms) != 0)
            return -1;
        if (reply.family == r->family)
            return 0;
    }
}

void ajazz_warn_if_unconfirmed(const ajazz_dev_t *dev, uint32_t cap, const char *feature_name)
{
    if (dev->confirmed & cap)
        return;

    fprintf(stderr,
        "note: %s hasn't been confirmed to work on a %s (dev_id %s) --\n"
        "it should work since every AJAZZ mouse we've seen shares the same\n"
        "protocol, but this exact model hasn't been tested yet. If it works\n"
        "(or doesn't), consider opening a PR to update devices/*.h.\n",
        feature_name, dev->name[0] ? dev->name : "this device",
        dev->dev_id[0] ? dev->dev_id : "unknown");
}
