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

int ajazz_open(ajazz_dev_t *dev)
{
    DIR *d = opendir("/dev");
    if (!d) {
        perror("opendir /dev");
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0)
            continue;

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
            dev->fd = fd;
            dev->vid = vid;
            dev->pid = pid;
            strncpy(dev->path, path, sizeof(dev->path) - 1);
            strncpy(dev->name, name, sizeof(dev->name) - 1);

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

            closedir(d);
            return 0;
        }

        close(fd);
    }

    closedir(d);
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
