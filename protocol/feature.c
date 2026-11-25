#define _GNU_SOURCE
#include "feature.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <linux/hidraw.h>

/* Command bytes (frame byte 2), from the >= 200 branch of each setter. */
enum {
    CMD_KEY       = 0x04, /* one button per frame: idx, 3-byte function code */
    CMD_DPI       = 0x05, /* ((active+1)<<4)|count, 0x3F, 6x u16 LE (dpi/50-1) */
    CMD_DPI_COLOR = 0x06, /* 6x RGB */
    CMD_RATE      = 0x07, /* interval ms (8/4/2/1) */
    CMD_SLEEP     = 0x0B, /* sleep (same raw unit as the old path), light-off-on-move */
    CMD_WAKE      = 0x0C, /* wake on move */
    CMD_RESET     = 0x0F, /* 0xFF = factory reset */
    CMD_IDENTIFY  = 0x20,
};

static void frame(uint8_t f[AJAZZ_FEATURE_LEN], uint8_t cmd, uint8_t len, uint8_t *seq)
{
    memset(f, 0, AJAZZ_FEATURE_LEN);
    f[0] = AJAZZ_FEATURE_ID;
    f[1] = 0x01;
    f[2] = cmd;
    f[4] = (*seq)++ % 0xFF;
    f[5] = 0x01; /* chunk 0 of 1 */
    f[6] = len;
}

int ajazz_feature_is_interface(const uint8_t *d, int size)
{
    int has_id = 0;
    for (int i = 0; i + 2 < size; i++) {
        if (d[i] == 0x85 && d[i + 1] == AJAZZ_FEATURE_ID)
            has_id = 1;
        if (has_id && d[i] == 0x95 && d[i + 1] == 0x20 && d[i + 2] == 0xb1)
            return 1;
    }
    return 0;
}

int ajazz_feature_translate(const ajazz_report_t *r, const ajazz_report_t *cached,
    uint8_t out[][AJAZZ_FEATURE_LEN], int max, uint8_t *seq)
{
    const uint8_t *p = r->payload;
    int n = 0;

    switch (r->family) {
    case AJAZZ_FAM_REPORT_RATE:
        frame(out[n], CMD_RATE, 1, seq);
        out[n++][7] = p[0];
        return n;

    case AJAZZ_FAM_DPI_TABLE: {
        frame(out[n], CMD_DPI, 0x12, seq);
        out[n][7] = (uint8_t)((((p[0] >> 4) + 1) << 4) | (p[0] & 0x0F));
        out[n][8] = 0x3F;
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
            int dpi = (p[1 + i * 4] | (p[2 + i * 4] << 8)) * 100; /* old layout is dpi/100 */
            int raw = dpi / 50 - 1;  /* encoder for sensor code 0 (FUN_0041e230) */
            if (raw < 0)
                raw = 0;
            out[n][9 + i * 2] = (uint8_t)(raw & 0xFF);
            out[n][10 + i * 2] = (uint8_t)(raw >> 8);
        }
        return n + 1;
    }

    case AJAZZ_FAM_DPI_COLOR:
        frame(out[n], CMD_DPI_COLOR, 0x18, seq);
        memcpy(&out[n][7], p, AJAZZ_DPI_STAGES * 3);
        return n + 1;

    case AJAZZ_FAM_SENSOR:
        /* old payload: [0] sleep, [1] wake on motion, [2] light off when idle, [3] debounce.
         * There's no debounce command on this protocol. */
        if (cached && cached->payload[3] != p[3]) {
            errno = ENOTSUP;
            return -1;
        }
        frame(out[n], CMD_SLEEP, 2, seq);
        out[n][7] = p[0];
        out[n++][8] = p[2];
        if (n < max) {
            frame(out[n], CMD_WAKE, 1, seq);
            out[n++][7] = p[1];
        }
        return n;

    case AJAZZ_FAM_KEY_TABLE:
        for (int slot = 0; slot < AJAZZ_KEY_SLOTS && n < max; slot++) {
            const uint8_t *code = &p[slot * 3];
            if (cached && memcmp(code, &cached->payload[slot * 3], 3) == 0)
                continue;
            frame(out[n], CMD_KEY, 4, seq);
            out[n][7] = (uint8_t)slot;
            memcpy(&out[n][8], code, 3);
            n++;
        }
        return n;

    case AJAZZ_FAM_FACTORY_RESET:
        frame(out[n], CMD_RESET, 1, seq);
        out[n][7] = 0xFF;
        return n + 1;

    default:
        /* lighting: no command in the 1.0.5.3 driver for these models.
         * sensor perf: lift-off/ripple share cmd 0x07 with the report rate
         * there, so sending it would change the polling rate.
         * macros: different, not yet decoded step format. */
        errno = ENOTSUP;
        return -1;
    }
}

/* ---- device I/O and the settings cache ---- */

#define CACHE_SLOTS 8 /* queries 0x10..0x17 */

static uint8_t g_seq = 1;

static int set_feature(int fd, const uint8_t f[AJAZZ_FEATURE_LEN])
{
    uint8_t buf[AJAZZ_FEATURE_LEN];
    memcpy(buf, f, sizeof(buf));
    return ioctl(fd, HIDIOCSFEATURE(AJAZZ_FEATURE_LEN), buf) < 0 ? -1 : 0;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void as_reply(ajazz_report_t *r, uint8_t query_id)
{
    r->family = query_id;
    r->mode = 0x01;
    if (r->opcode == 0)
        r->opcode = 1;
}

static void cache_defaults(ajazz_report_t c[CACHE_SLOTS])
{
    memset(c, 0, sizeof(ajazz_report_t) * CACHE_SLOTS);
    ajazz_build_reset_keys(&c[AJAZZ_QUERY_KEY_TABLE - 0x10]);
    ajazz_build_set_report_rate(&c[AJAZZ_QUERY_REPORT_RATE - 0x10], 3); /* 1000 Hz */
    ajazz_build_set_dpi_table(&c[AJAZZ_QUERY_DPI_TABLE - 0x10], AJAZZ_DEFAULT_DPI, 0, AJAZZ_DPI_STAGES);
    ajazz_build_set_dpi_colors(&c[AJAZZ_QUERY_DPI_COLOR_TABLE - 0x10], AJAZZ_DEFAULT_DPI_COLOR);
    ajazz_build_set_rgb_off(&c[AJAZZ_QUERY_LED_MODE - 0x10]);
    ajazz_sensor_t s = { .lift_off = 1 };
    ajazz_build_set_sensor_perf(&c[AJAZZ_QUERY_SENSOR_PERF - 0x10], &s);
    ajazz_report_t *pw = &c[AJAZZ_QUERY_POWER - 0x10];
    pw->opcode = 0x04;
    pw->payload[0] = 30; /* mouse_aj159.xml sleep_light */
    pw->payload[3] = 8;  /* factory debounce */
    for (int q = 0x11; q <= 0x17; q++)
        as_reply(&c[q - 0x10], (uint8_t)q);
}

static void cache_path(char *out, size_t len, const char *key)
{
    const char *state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    if (state && *state)
        snprintf(out, len, "%s/ajazzy/%s.bin", state, key);
    else
        snprintf(out, len, "%s/.local/state/ajazzy/%s.bin", home ? home : ".", key);
}

static void cache_load(const char *key, ajazz_report_t c[CACHE_SLOTS])
{
    char path[512];
    cache_path(path, sizeof(path), key);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(c, sizeof(ajazz_report_t), CACHE_SLOTS, f);
        fclose(f);
        if (n == CACHE_SLOTS)
            return;
    }
    cache_defaults(c);
}

static void cache_save(const char *key, const ajazz_report_t c[CACHE_SLOTS])
{
    char path[512], tmp[520];
    cache_path(path, sizeof(path), key);
    char *slash = strrchr(path, '/');
    *slash = '\0';
    char *parent = strrchr(path, '/');
    *parent = '\0';
    mkdir(path, 0755); /* .../state */
    *parent = '/';
    mkdir(path, 0755); /* .../state/ajazzy */
    *slash = '/';
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return;
    int ok = fwrite(c, sizeof(ajazz_report_t), CACHE_SLOTS, f) == CACHE_SLOTS;
    ok = (fclose(f) == 0) && ok;
    if (ok)
        rename(tmp, path);
    else
        unlink(tmp);
}

static int query_for_family(uint8_t family)
{
    switch (family) {
    case AJAZZ_FAM_REPORT_RATE: return AJAZZ_QUERY_REPORT_RATE;
    case AJAZZ_FAM_DPI_TABLE:   return AJAZZ_QUERY_DPI_TABLE;
    case AJAZZ_FAM_DPI_COLOR:   return AJAZZ_QUERY_DPI_COLOR_TABLE;
    case AJAZZ_FAM_SENSOR:      return AJAZZ_QUERY_POWER;
    case AJAZZ_FAM_KEY_TABLE:   return AJAZZ_QUERY_KEY_TABLE;
    default:                    return -1;
    }
}

int ajazz_feature_send(int fd, const char *key, const ajazz_report_t *r)
{
    ajazz_report_t cache[CACHE_SLOTS];
    cache_load(key, cache);

    int q = query_for_family(r->family);
    uint8_t frames[AJAZZ_KEY_SLOTS + 1][AJAZZ_FEATURE_LEN];
    int n = ajazz_feature_translate(r, q >= 0 ? &cache[q - 0x10] : NULL, frames,
        (int)(sizeof(frames) / sizeof(frames[0])), &g_seq);
    if (n < 0)
        return -1;

    for (int i = 0; i < n; i++) {
        if (set_feature(fd, frames[i]) != 0)
            return -1;
        sleep_ms(20); /* the official driver paces frames; there's no ack to wait on */
    }

    if (r->family == AJAZZ_FAM_FACTORY_RESET) {
        cache_defaults(cache);
    } else if (q >= 0) {
        cache[q - 0x10] = *r;
        as_reply(&cache[q - 0x10], (uint8_t)q);
    }
    cache_save(key, cache);
    return 0;
}

/* Identify: SET then GET_FEATURE; a reply is valid once byte 8 is non-zero. */
static int identify(int fd, uint8_t out[AJAZZ_FEATURE_LEN])
{
    for (int attempt = 0; attempt < 4; attempt++) {
        uint8_t f[AJAZZ_FEATURE_LEN];
        frame(f, CMD_IDENTIFY, 0, &g_seq);
        f[5] = 0x00;
        if (set_feature(fd, f) != 0)
            return -1;
        sleep_ms(16);
        memset(out, 0, AJAZZ_FEATURE_LEN);
        out[0] = AJAZZ_FEATURE_ID;
        if (ioctl(fd, HIDIOCGFEATURE(AJAZZ_FEATURE_LEN), out) < 0)
            return -1;
        if (out[8] != 0)
            return 0;
        sleep_ms(100);
    }
    return -1;
}

int ajazz_feature_query(int fd, const char *key, uint8_t query_id, ajazz_report_t *reply)
{
    memset(reply, 0, sizeof(*reply));

    if (query_id == AJAZZ_QUERY_DEVICE_INFO || query_id == AJAZZ_QUERY_BATTERY) {
        uint8_t id[AJAZZ_FEATURE_LEN];
        if (identify(fd, id) != 0 || id[21] == 0) /* byte 21 = mouse linked to the dongle */
            return -1;
        memcpy(reply->payload, &id[8], 4);                 /* dev_id, e.g. "M620" */
        reply->payload[AJAZZ_BATTERY_PAYLOAD_OFFSET] = id[18]; /* battery % */
        reply->opcode = 0x10;
        as_reply(reply, query_id);
        return 0;
    }

    if (query_id < 0x10 || query_id >= 0x10 + CACHE_SLOTS) {
        errno = ENOTSUP;
        return -1;
    }
    ajazz_report_t cache[CACHE_SLOTS];
    cache_load(key, cache);
    *reply = cache[query_id - 0x10];
    return 0;
}
