#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../protocol/hid.h"
#include "../protocol/transport.h"
#include "../devices/device.h"

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <command> [args]\n\n"
        "Commands:\n"
        "  info                        Show device name, VID/PID and firmware id\n"
        "  battery                     Show battery level\n"
        "  dpi list                    List the 6 DPI stages\n"
        "  dpi set <1-6> <value>       Set one stage's DPI (100-26000)\n"
        "  dpi active <1-6>            Switch the active DPI stage\n"
        "  dpi reset                   Reset all 6 DPI stages to factory defaults\n"
        "                               (400/800/1200/1600/2400/3200)\n"
        "  dpicolor list                List the 6 DPI stages' indicator LED colours\n"
        "  dpicolor set <1-6> <RRGGBB> Set one stage's indicator LED colour\n"
        "  dpicolor reset               Reset all 6 indicator colours to factory defaults\n"
        "  rate get                    Show the current report rate\n"
        "  rate set <125|250|500|1000> Set the report/polling rate\n"
        "  sensor get                  Show lift-off, angle snap, ripple control, motion sync\n"
        "  sensor liftoff <1|2>        Lift-off distance in mm\n"
        "  sensor anglesnap <0|1>      Angle snapping\n"
        "  sensor ripple <0|1>         Ripple control\n"
        "  sensor motionsync <0|1>     Motion sync\n"
        "  power get                   Show sleep timer, wake on movement, idle light-off, debounce\n"
        "  power sleep <seconds>       Set the auto-sleep timer (0-2550s, rounded to 10s)\n"
        "  power wake <0|1>            Wake the mouse on movement\n"
        "  power light-idle-off <0|1>  Turn the light off when the mouse is idle\n"
        "  power debounce <ms>         Button debounce time (factory 8)\n"
        "  rgb steady <RRGGBB> [bri 0-4]            Solid colour\n"
        "  rgb breathing <bri> <speed> [RRGGBB..]   Breathing, up to 7 colours (default: multicolour)\n"
        "  rgb flow|neon|wave <bri> <speed>         Built-in effects\n"
        "  rgb off                     Turn the RGB off\n"
        "  rgb get                     Show the current lighting effect\n"
        "  keys reset                  Reset ALL buttons to their default function\n"
        "  keys reset-slot <1-6>       Reset one button to its default function\n"
        "  keys set <1-6> <function>   Remap a button (volume-up, mute, forward, ...)\n"
        "  keys set-key <1-6> <key> [ctrl] [shift] [alt] [win]\n"
        "                               Remap a button to a keyboard key\n"
        "  keys set-fire <1-6> <interval 5-255> <number 0-255>\n"
        "                               Turn a button into a rapid-fire/auto-click button\n"
        "  keys list-functions         List the functions 'keys set' accepts\n"
        "  macro set <1-6> <delay ms> <action> [action...]\n"
        "                               Assign a press+release macro to a button (up to %d\n"
        "                               actions, %d+ uses a multi-packet write);\n"
        "                               each action is a key (a-z, 0-9, hex code) or a mouse\n"
        "                               button (lclick, rclick, mclick, btn4, btn5)\n"
        "  reset factory [-y]          Restore DPI/colours/rate/lighting/sensor/keys/\n"
        "                               macros to factory defaults (asks to confirm\n"
        "                               unless -y is given)\n"
        "  watch                       Dump raw incoming reports (debugging)\n"
        "  raw <32 bytes hex>          Send a raw report (debugging/research)\n",
        prog, AJAZZ_MACRO_MAX_CHUNKED_ACTIONS, AJAZZ_MACRO_MAX_STEPS / 2 + 1);
}

/* pass 0/NULL for `cap`/`feature_name` on commands that don't map to a
 * single capability (info, watch, raw) */
static int open_device(ajazz_dev_t *dev, uint32_t cap, const char *feature_name)
{
    if (ajazz_open(dev) != 0) {
        if (ajazz_probe_access() == AJAZZ_ACCESS_DENIED) {
            fprintf(stderr,
                "The mouse is plugged in, but your user isn't allowed to open its device.\n"
                "Install the udev rule (one-off), then it works without sudo:\n\n%s\n\n",
                ajazz_udev_fix_command());
        } else {
            fprintf(stderr, "No AJAZZ mouse found (%s).\n"
                            "Check that the dongle/mouse is plugged in and turned on.\n",
                    strerror(errno));
        }
        return -1;
    }
    if (cap)
        ajazz_warn_if_unconfirmed(dev, cap, feature_name);
    return 0;
}

static void hexdump(const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++)
        printf("%02x ", b[i]);
    printf("\n");
}

static int cmd_info(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, 0, NULL) != 0) return 1;

    printf("Device: %s (%s)\n", dev.name, dev.path);
    printf("VID:PID: %04X:%04X\n", dev.vid, dev.pid);

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000) == 0) {
        char id[5] = {0};
        memcpy(id, reply.payload, 4);
        printf("dev_id (firmware): %s\n", id);
        printf("raw payload: ");
        hexdump(reply.payload, AJAZZ_PAYLOAD_LEN);
    } else {
        printf("Could not read device info.\n");
    }

    if (dev.confirmed == 0) {
        printf("confirmed capabilities: none -- this exact model hasn't been\n"
               "individually tested, but should work since every AJAZZ mouse\n"
               "we've seen shares the same protocol. Please report back.\n");
    } else {
        printf("confirmed capabilities:%s%s%s%s%s%s%s\n",
               (dev.confirmed & AJAZZ_CAP_DPI)     ? " dpi"     : "",
               (dev.confirmed & AJAZZ_CAP_RATE)    ? " rate"    : "",
               (dev.confirmed & AJAZZ_CAP_RGB)     ? " rgb"     : "",
               (dev.confirmed & AJAZZ_CAP_SENSOR)  ? " sensor"  : "",
               (dev.confirmed & AJAZZ_CAP_KEYS)    ? " keys"    : "",
               (dev.confirmed & AJAZZ_CAP_BATTERY) ? " battery" : "",
               (dev.confirmed & AJAZZ_CAP_MACRO)   ? " macro"   : "");
    }

    ajazz_close(&dev);
    return 0;
}

static int cmd_battery(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_BATTERY, "battery") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000) == 0) {
        uint8_t raw = reply.payload[AJAZZ_BATTERY_PAYLOAD_OFFSET];
        printf("Battery: %d%% -- %s\n", raw, ajazz_battery_label(raw));
        /* dev.charging is scanned independently of which interface we
         * actually connected over -- see the comment on
         * ajazz_dev_t::charging in protocol/transport.h. Gated on
         * AJAZZ_CAP_BATTERY so a wired-only model (no battery at all)
         * never prints this. */
        if ((dev.confirmed & AJAZZ_CAP_BATTERY) && dev.charging)
            printf("Charging (connected over USB).\n");
    } else {
        printf("Could not read battery level.\n");
    }

    ajazz_close(&dev);
    return 0;
}

static int cmd_dpi_list(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_DPI_TABLE, &reply, 1000) == 0) {
        uint16_t dpi[AJAZZ_DPI_STAGES];
        ajazz_parse_dpi_table(&reply, dpi);
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
            printf("  Stage %d: %u DPI\n", i + 1, dpi[i]);
    } else {
        printf("Could not read the DPI table.\n");
    }

    ajazz_close(&dev);
    return 0;
}

static int cmd_dpi_set(int stage, int value)
{
    if (stage < 1 || stage > AJAZZ_DPI_STAGES) {
        fprintf(stderr, "Stage must be between 1 and %d\n", AJAZZ_DPI_STAGES);
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t reply;
    uint16_t dpi[AJAZZ_DPI_STAGES];

    if (ajazz_query(&dev, AJAZZ_QUERY_DPI_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current DPI table.\n");
        ajazz_close(&dev);
        return 1;
    }
    ajazz_parse_dpi_table(&reply, dpi);
    dpi[stage - 1] = (uint16_t)value;

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, dpi, (uint8_t)(stage - 1), (uint8_t)ajazz_dpi_table_count(&reply));

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Stage %d set to %d DPI.\n", stage, value);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_dpi_active(int stage)
{
    if (stage < 1 || stage > AJAZZ_DPI_STAGES) {
        fprintf(stderr, "Stage must be between 1 and %d\n", AJAZZ_DPI_STAGES);
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    /* Active stage is switched by rewriting the whole DPI table with the
     * new stage baked into payload[0] -- see ajazz_build_set_dpi_table(). */
    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_DPI_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current DPI table.\n");
        ajazz_close(&dev);
        return 1;
    }
    uint16_t dpi[AJAZZ_DPI_STAGES];
    ajazz_parse_dpi_table(&reply, dpi);

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, dpi, (uint8_t)(stage - 1), (uint8_t)ajazz_dpi_table_count(&reply));

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Active DPI stage set to %d.\n", stage);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

/* AJAZZ_DEFAULT_DPI -- factory values from the official installer's own
 * per-model config, see the comment in protocol/hid.h. */
static int cmd_dpi_reset(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t cur;
    int count = (ajazz_query(&dev, AJAZZ_QUERY_DPI_TABLE, &cur, 1000) == 0) ? ajazz_dpi_table_count(&cur) : 5;
    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, AJAZZ_DEFAULT_DPI, 0, (uint8_t)count);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("DPI reset to factory defaults (400/800/1200/1600/2400/3200).\n");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_dpicolor_list(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_DPI_COLOR_TABLE, &reply, 1000) == 0) {
        uint8_t colors[AJAZZ_DPI_STAGES][3];
        ajazz_parse_dpi_color_table(&reply, colors);
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
            printf("  Stage %d: #%02X%02X%02X\n", i + 1, colors[i][0], colors[i][1], colors[i][2]);
    } else {
        printf("Could not read the DPI colour table.\n");
    }

    ajazz_close(&dev);
    return 0;
}

/* AJAZZ_DEFAULT_DPI_COLOR -- factory values from the official
 * installer's own per-model config, see the comment in protocol/hid.h. */
static int cmd_dpicolor_reset(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, AJAZZ_DEFAULT_DPI_COLOR);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("DPI colours reset to factory defaults.\n");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_dpicolor_set(int stage, const char *hexcolor)
{
    if (stage < 1 || stage > AJAZZ_DPI_STAGES) {
        fprintf(stderr, "Stage must be between 1 and %d\n", AJAZZ_DPI_STAGES);
        return 1;
    }
    if (strlen(hexcolor) != 6 || strspn(hexcolor, "0123456789abcdefABCDEF") != 6) {
        fprintf(stderr, "Colour must be 6 hex digits, e.g. ff00aa\n");
        return 1;
    }
    unsigned int rgb = (unsigned int)strtoul(hexcolor, NULL, 16);

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_DPI, "dpi") != 0) return 1;

    ajazz_report_t reply;
    uint8_t colors[AJAZZ_DPI_STAGES][3];
    if (ajazz_query(&dev, AJAZZ_QUERY_DPI_COLOR_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current DPI colour table.\n");
        ajazz_close(&dev);
        return 1;
    }
    ajazz_parse_dpi_color_table(&reply, colors);
    colors[stage - 1][0] = (uint8_t)(rgb >> 16);
    colors[stage - 1][1] = (uint8_t)(rgb >> 8);
    colors[stage - 1][2] = (uint8_t)rgb;

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, colors);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Stage %d colour set to #%06X.\n", stage, rgb);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_rate_get(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RATE, "report rate") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_REPORT_RATE, &reply, 1000) == 0) {
        int hz = ajazz_report_rate_hz(reply.payload[0]);
        if (hz > 0)
            printf("Report rate: %d Hz\n", hz);
        else
            printf("Report rate: unrecognized raw value 0x%02x\n", reply.payload[0]);
    } else {
        printf("Could not read the report rate.\n");
    }

    ajazz_close(&dev);
    return 0;
}

static int cmd_rate_set(int hz)
{
    int idx;
    switch (hz) {
        case 125: idx = 0; break;
        case 250: idx = 1; break;
        case 500: idx = 2; break;
        case 1000: idx = 3; break;
        default:
            fprintf(stderr, "Accepted values: 125, 250, 500, 1000\n");
            return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RATE, "report rate") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_set_report_rate(&req, (uint8_t)idx);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Report rate set to %d Hz.\n", hz);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

/* ---- sensor performance (family 0x06) ---- */

static int sensor_modify(const char *what, int (*edit)(ajazz_sensor_t *, int), int value)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "sensor settings") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR_PERF, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current sensor settings.\n");
        ajazz_close(&dev);
        return 1;
    }
    ajazz_sensor_t s;
    ajazz_parse_sensor_perf(&reply, &s);
    if (edit(&s, value) != 0) {
        fprintf(stderr, "Invalid value for %s.\n", what);
        ajazz_close(&dev);
        return 1;
    }
    ajazz_report_t req;
    ajazz_build_set_sensor_perf(&req, &s);
    int rc = 0;
    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("%s set to %d.\n", what, value);
    else {
        fprintf(stderr, "No confirmation from the device.\n");
        rc = 1;
    }
    ajazz_close(&dev);
    return rc;
}

static int edit_liftoff(ajazz_sensor_t *s, int v) { if (v != 1 && v != 2) return -1; s->lift_off = (uint8_t)v; return 0; }
static int edit_angle(ajazz_sensor_t *s, int v)   { s->angle_snap = v ? 1 : 0; return 0; }
static int edit_ripple(ajazz_sensor_t *s, int v)  { s->ripple_control = v ? 1 : 0; return 0; }
static int edit_msync(ajazz_sensor_t *s, int v)   { s->motion_sync = v ? 1 : 0; return 0; }

static int cmd_sensor_get(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "sensor settings") != 0) return 1;
    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR_PERF, &reply, 1000) != 0) {
        printf("Could not read the sensor settings.\n");
        ajazz_close(&dev);
        return 1;
    }
    ajazz_sensor_t s;
    ajazz_parse_sensor_perf(&reply, &s);
    printf("Lift-off distance: %d mm\n", s.lift_off);
    printf("Angle snapping:    %s\n", s.angle_snap ? "on" : "off");
    printf("Ripple control:    %s\n", s.ripple_control ? "on" : "off");
    printf("Motion sync:       %s\n", s.motion_sync ? "on" : "off");
    ajazz_close(&dev);
    return 0;
}

/* ---- power / buttons (family 0x07) ---- */

static int power_modify(const char *what, int value, void (*apply)(ajazz_report_t *, int))
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "power settings") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_POWER, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current power settings.\n");
        ajazz_close(&dev);
        return 1;
    }
    ajazz_report_t req;
    ajazz_build_power_from_query(&req, &reply);
    apply(&req, value);
    int rc = 0;
    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("%s set to %d.\n", what, value);
    else {
        fprintf(stderr, "No confirmation from the device.\n");
        rc = 1;
    }
    ajazz_close(&dev);
    return rc;
}

static void ap_sleep(ajazz_report_t *r, int v)  { ajazz_power_set_sleep_seconds(r, (uint16_t)v); }
static void ap_wake(ajazz_report_t *r, int v)   { ajazz_power_set_wake_on_motion(r, (uint8_t)v); }
static void ap_idle(ajazz_report_t *r, int v)   { ajazz_power_set_light_idle_off(r, (uint8_t)v); }
static void ap_deb(ajazz_report_t *r, int v)    { ajazz_power_set_debounce_ms(r, (uint8_t)v); }

static int cmd_power_get(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "power settings") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_POWER, &reply, 1000) != 0) {
        printf("Could not read the power settings.\n");
        ajazz_close(&dev);
        return 1;
    }
    printf("Auto-sleep timer:         %d seconds\n", reply.payload[0] * 10);
    printf("Wake on movement:         %s\n", reply.payload[1] ? "on" : "off");
    printf("Light off when idle:      %s\n", reply.payload[2] ? "on" : "off");
    printf("Button debounce:          %d ms\n", reply.payload[3]);
    ajazz_close(&dev);
    return 0;
}

/* ---- lighting ---- */

static int parse_rgb(const char *s, uint8_t out[3])
{
    if (*s == '#') s++;
    if (strlen(s) != 6) return -1;
    unsigned v;
    if (sscanf(s, "%x", &v) != 1) return -1;
    out[0] = (uint8_t)(v >> 16);
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)v;
    return 0;
}

static int rgb_send(const ajazz_report_t *req, const char *label)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RGB, "rgb lighting") != 0) return 1;
    int rc = 0;
    if (ajazz_send_and_wait(&dev, req, 1000) == 0)
        printf("%s\n", label);
    else {
        fprintf(stderr, "No confirmation from the device.\n");
        rc = 1;
    }
    ajazz_close(&dev);
    return rc;
}

static int bs_ok(int b, int s)
{
    if (b < 0 || b > 4 || s < 0 || s > 4) {
        fprintf(stderr, "Brightness and speed must be between 0 and 4.\n");
        return 0;
    }
    return 1;
}

/* rgb steady <RRGGBB> <bri> ; rgb breathing <bri> <speed> [RRGGBB ...] ;
 * rgb flow|neon|wave <bri> <speed> */
static int cmd_rgb_set(int argc, char **argv)
{
    const char *mode = argv[0];
    ajazz_report_t req;
    if (strcmp(mode, "steady") == 0 || strcmp(mode, "constant") == 0) {
        uint8_t rgb[3] = {0xff, 0x00, 0x00};
        int bri = 4;
        if (argc >= 2 && parse_rgb(argv[1], rgb) != 0) { fprintf(stderr, "Colour must be RRGGBB.\n"); return 1; }
        if (argc >= 3) bri = atoi(argv[2]);
        if (!bs_ok(bri, 0)) return 1;
        ajazz_build_set_light_steady(&req, (uint8_t)bri, 0, rgb);
        return rgb_send(&req, "Steady colour set.");
    }
    if (strcmp(mode, "breathing") == 0 || strcmp(mode, "rainbow") == 0) {
        int bri = argc >= 2 ? atoi(argv[1]) : 4;
        int spd = argc >= 3 ? atoi(argv[2]) : 2;
        if (!bs_ok(bri, spd)) return 1;
        uint8_t pal[AJAZZ_LIGHT_PALETTE][3];
        memcpy(pal, AJAZZ_LIGHT_DEFAULT_PALETTE, sizeof(pal));
        int n = argc - 3;
        if (n > 0) {
            if (n > AJAZZ_LIGHT_PALETTE) n = AJAZZ_LIGHT_PALETTE;
            for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++)
                if (parse_rgb(argv[3 + (i < n ? i : n - 1)], pal[i]) != 0) {
                    fprintf(stderr, "Colours must be RRGGBB.\n");
                    return 1;
                }
        }
        ajazz_build_set_light_breathing(&req, (uint8_t)bri, (uint8_t)spd, pal);
        return rgb_send(&req, "Breathing effect set.");
    }
    uint8_t m = 0;
    if (strcmp(mode, "flow") == 0) m = AJAZZ_LIGHT_FLOW;
    else if (strcmp(mode, "neon") == 0) m = AJAZZ_LIGHT_NEON;
    else if (strcmp(mode, "wave") == 0) m = AJAZZ_LIGHT_WAVE;
    else { fprintf(stderr, "Unknown lighting mode '%s'.\n", mode); return 1; }
    int bri = argc >= 2 ? atoi(argv[1]) : 4;
    int spd = argc >= 3 ? atoi(argv[2]) : 2;
    if (!bs_ok(bri, spd)) return 1;
    ajazz_build_set_light_simple(&req, m, (uint8_t)bri, (uint8_t)spd);
    return rgb_send(&req, "Lighting effect set.");
}

static int cmd_rgb_off(void)
{
    ajazz_report_t req;
    ajazz_build_set_rgb_off(&req);
    return rgb_send(&req, "RGB turned off.");
}

static int cmd_rgb_get(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RGB, "rgb lighting") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_LED_MODE, &reply, 1000) != 0) {
        printf("Could not read the lighting state.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_light_state_t st;
    if (ajazz_parse_light(&reply, &st) != 0) {
        printf("Lighting: unrecognized state, raw opcode=0x%02x\n", reply.opcode);
        printf("raw payload: ");
        hexdump(reply.payload, AJAZZ_PAYLOAD_LEN);
        ajazz_close(&dev);
        return 0;
    }

    static const char *names[] = {"off", "flow", "breathing", "steady colour", "neon", "colourful wave"};
    printf("Lighting: %s", names[st.mode]);
    if (st.mode != AJAZZ_LIGHT_OFF)
        printf(" (brightness=%d speed=%d)", st.brightness, st.speed);
    if (st.mode == AJAZZ_LIGHT_STEADY)
        printf(" colour=%02x%02x%02x", st.rgb[0], st.rgb[1], st.rgb[2]);
    printf("\n");
    if (st.mode == AJAZZ_LIGHT_BREATHING) {
        printf("  palette:");
        for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++)
            printf(" %02x%02x%02x", st.palette[i][0], st.palette[i][1], st.palette[i][2]);
        printf("\n");
    }

    ajazz_close(&dev);
    return 0;
}

/* Wire command reconstructed from decompiling the official driver (see
 * the comment on AJAZZ_FAM_FACTORY_RESET in protocol/hid.h) -- requires
 * typing "yes" since there's no per-section undo once sent. */
static int cmd_factory_reset(int assume_yes)
{
    if (!assume_yes) {
        printf("This resets DPI, DPI colours, report rate, lighting, sensor/power,\n"
               "button mapping and macros to factory defaults. Type 'yes' to continue: ");
        fflush(stdout);
        char line[16] = {0};
        if (!fgets(line, sizeof(line), stdin) || strncmp(line, "yes", 3) != 0) {
            printf("Cancelled.\n");
            return 0;
        }
    }

    ajazz_dev_t dev;
    if (open_device(&dev, 0, NULL) != 0) return 1;

    ajazz_report_t req;
    ajazz_build_factory_reset(&req);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Factory reset sent.\n");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_keys_reset(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_KEYS, "button remapping") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_reset_keys(&req);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Button mapping reset to default.\n"
               "Note: if the original Windows driver still shows the old mapping,\n"
               "that's just because it reads from its own local database instead of\n"
               "the mouse -- the reset really did happen on the hardware.\n");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

typedef struct {
    const char *name;
    uint16_t usage;
} named_usage_t;

static const named_usage_t named_usages[] = {
    {"volume-up",   AJAZZ_USAGE_VOLUME_UP},
    {"volume-down", AJAZZ_USAGE_VOLUME_DOWN},
    {"mute",        AJAZZ_USAGE_MUTE},
    {"play-pause",  AJAZZ_USAGE_PLAY_PAUSE},
    {"next-track",  AJAZZ_USAGE_NEXT_TRACK},
    {"prev-track",  AJAZZ_USAGE_PREV_TRACK},
    {"stop",        AJAZZ_USAGE_STOP},
    {"home",        AJAZZ_USAGE_AC_HOME},
    {"back",        AJAZZ_USAGE_AC_BACK},
    {"forward",     AJAZZ_USAGE_AC_FORWARD},
    {"refresh",     AJAZZ_USAGE_AC_REFRESH},
    {"bookmarks",   AJAZZ_USAGE_AC_BOOKMARKS},
};

#define N_NAMED_USAGES (sizeof(named_usages) / sizeof(named_usages[0]))

static void print_key_functions(void)
{
    fprintf(stderr, "Available functions: ");
    for (size_t i = 0; i < N_NAMED_USAGES; i++)
        fprintf(stderr, "%s ", named_usages[i].name);
    fprintf(stderr, "(or a hex code like 0x00e9)\n");
}

static int lookup_usage(const char *s, uint16_t *out)
{
    for (size_t i = 0; i < N_NAMED_USAGES; i++) {
        if (strcmp(s, named_usages[i].name) == 0) {
            *out = named_usages[i].usage;
            return 0;
        }
    }
    char *end;
    long v = strtol(s, &end, 0);
    if (*end == '\0' && v >= 0 && v <= 0xFFFF) {
        *out = (uint16_t)v;
        return 0;
    }
    return -1;
}

static int lookup_keycode(const char *s, uint8_t *out)
{
    if (strlen(s) == 1) {
        char c = s[0];
        if (c >= 'a' && c <= 'z') { *out = (uint8_t)(0x04 + (c - 'a')); return 0; }
        if (c >= 'A' && c <= 'Z') { *out = (uint8_t)(0x04 + (c - 'A')); return 0; }
        if (c >= '1' && c <= '9') { *out = (uint8_t)(0x1E + (c - '1')); return 0; }
        if (c == '0') { *out = 0x27; return 0; }
    }
    char *end;
    long v = strtol(s, &end, 0);
    if (*end == '\0' && v >= 0 && v <= 0xFF) {
        *out = (uint8_t)v;
        return 0;
    }
    return -1;
}

static int cmd_keys_set_key(int slot_1based, const char *key, int argc, char **argv)
{
    if (slot_1based < 1 || slot_1based > AJAZZ_KEY_SLOTS) {
        fprintf(stderr, "Slot must be between 1 and %d\n", AJAZZ_KEY_SLOTS);
        return 1;
    }

    uint8_t keycode;
    if (lookup_keycode(key, &keycode) != 0) {
        fprintf(stderr, "Unknown key: %s (use a letter a-z, a digit 0-9, or a hex code)\n", key);
        return 1;
    }

    uint8_t mods = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "ctrl") == 0) mods |= AJAZZ_MOD_LCTRL;
        else if (strcmp(argv[i], "shift") == 0) mods |= AJAZZ_MOD_LSHIFT;
        else if (strcmp(argv[i], "alt") == 0) mods |= AJAZZ_MOD_LALT;
        else if (strcmp(argv[i], "win") == 0) mods |= AJAZZ_MOD_LGUI;
        else {
            fprintf(stderr, "Unknown modifier: %s (use ctrl, shift, alt, win)\n", argv[i]);
            return 1;
        }
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_KEYS, "button remapping") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current button table.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    ajazz_key_slot_set_keyboard(&req, slot_1based - 1, mods, keycode);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Button %d remapped to key 0x%02x (mods 0x%02x).\n", slot_1based, keycode, mods);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_keys_set_fire(int slot_1based, int interval, int number)
{
    if (slot_1based < 1 || slot_1based > AJAZZ_KEY_SLOTS) {
        fprintf(stderr, "Slot must be between 1 and %d\n", AJAZZ_KEY_SLOTS);
        return 1;
    }
    if (interval < 5 || interval > 255) {
        fprintf(stderr, "Interval must be between 5 and 255.\n");
        return 1;
    }
    if (number < 0 || number > 255) {
        fprintf(stderr, "Number must be between 0 and 255.\n");
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_KEYS, "button remapping") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current button table.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    ajazz_key_slot_set_fire(&req, slot_1based - 1, (uint8_t)interval, (uint8_t)number);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Button %d set to rapid-fire (interval=%d number=%d).\n", slot_1based, interval, number);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_keys_set(int slot_1based, const char *function)
{
    if (slot_1based < 1 || slot_1based > AJAZZ_KEY_SLOTS) {
        fprintf(stderr, "Slot must be between 1 and %d\n", AJAZZ_KEY_SLOTS);
        return 1;
    }

    uint16_t usage;
    if (lookup_usage(function, &usage) != 0) {
        fprintf(stderr, "Unknown function: %s\n", function);
        print_key_functions();
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_KEYS, "button remapping") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current button table.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    ajazz_key_slot_set_consumer(&req, slot_1based - 1, usage);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Button %d remapped (code 0x%04x).\n", slot_1based, usage);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_keys_reset_slot(int slot_1based)
{
    if (slot_1based < 1 || slot_1based > AJAZZ_KEY_SLOTS) {
        fprintf(stderr, "Slot must be between 1 and %d\n", AJAZZ_KEY_SLOTS);
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_KEYS, "button remapping") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current button table.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    ajazz_key_slot_reset_default(&req, slot_1based - 1);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Button %d reset to default.\n", slot_1based);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static const named_usage_t named_mouse_buttons[] = {
    {"lclick", 1},
    {"rclick", 2},
    {"mclick", 4},
    {"btn4",   8},
    {"btn5",   16},
};
#define N_NAMED_MOUSE_BUTTONS (sizeof(named_mouse_buttons) / sizeof(named_mouse_buttons[0]))

static void print_macro_actions(void)
{
    fprintf(stderr, "Available actions: ");
    for (size_t i = 0; i < N_NAMED_MOUSE_BUTTONS; i++)
        fprintf(stderr, "%s ", named_mouse_buttons[i].name);
    fprintf(stderr, "or a keyboard key (letter a-z, digit 0-9, or hex HID code)\n");
}

/* Each action becomes a press+release pair (down, delay, up, delay) -- every
 * captured macro used exactly that shape, so this is what the CLI builds. */
static int cmd_macro_set(int slot_1based, int delay_ms, int argc, char **argv)
{
    if (slot_1based < 1 || slot_1based > AJAZZ_KEY_SLOTS) {
        fprintf(stderr, "Slot must be between 1 and %d\n", AJAZZ_KEY_SLOTS);
        return 1;
    }
    if (delay_ms < 0 || delay_ms > 65535) {
        fprintf(stderr, "Delay must be between 0 and 65535 ms.\n");
        return 1;
    }
    if (argc < 1 || argc > AJAZZ_MACRO_MAX_CHUNKED_ACTIONS) {
        fprintf(stderr, "A macro needs 1 to %d actions (each is a press+release).\n",
                AJAZZ_MACRO_MAX_CHUNKED_ACTIONS);
        return 1;
    }
    if (argc > AJAZZ_MACRO_MAX_STEPS / 2)
        fprintf(stderr, "Note: macros over %d actions use a multi-packet write -- see\n"
                        "AJAZZ_MACRO_MAX_CHUNKED_STEPS in protocol/hid.h.\n",
                AJAZZ_MACRO_MAX_STEPS / 2);

    ajazz_macro_step_t steps[AJAZZ_MACRO_MAX_CHUNKED_STEPS];
    int n_steps = 0;
    for (int i = 0; i < argc; i++) {
        uint16_t btn = 0;
        int is_button = 0;
        for (size_t j = 0; j < N_NAMED_MOUSE_BUTTONS; j++) {
            if (strcmp(argv[i], named_mouse_buttons[j].name) == 0) {
                btn = named_mouse_buttons[j].usage;
                is_button = 1;
                break;
            }
        }

        if (is_button) {
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_DOWN, (uint16_t)delay_ms, (uint8_t)btn};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_UP,   (uint16_t)delay_ms, (uint8_t)btn};
        } else {
            uint8_t keycode;
            if (lookup_keycode(argv[i], &keycode) != 0) {
                fprintf(stderr, "Unknown action: %s\n", argv[i]);
                print_macro_actions();
                return 1;
            }
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_DOWN, (uint16_t)delay_ms, keycode};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_UP,   (uint16_t)delay_ms, keycode};
        }
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_MACRO, "macro") != 0) return 1;

    ajazz_report_t chunks[(AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + AJAZZ_PAYLOAD_LEN) / AJAZZ_PAYLOAD_LEN + 1];
    int n_chunks = ajazz_build_macro_content_chunked(chunks, (int)(sizeof(chunks) / sizeof(chunks[0])), (uint8_t)(slot_1based - 1), steps, n_steps);
    if (n_chunks < 0) {
        fprintf(stderr, "Macro is too long to encode.\n");
        ajazz_close(&dev);
        return 1;
    }
    for (int i = 0; i < n_chunks; i++) {
        if (ajazz_send_and_wait(&dev, &chunks[i], 1000) != 0) {
            fprintf(stderr, "No confirmation writing macro content (chunk %d/%d).\n", i + 1, n_chunks);
            ajazz_close(&dev);
            return 1;
        }
    }

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current button table.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t assign;
    ajazz_build_key_table_from_query(&assign, &reply);
    ajazz_key_slot_set_macro(&assign, slot_1based - 1, AJAZZ_MACRO_ONCE, 0);

    if (ajazz_send_and_wait(&dev, &assign, 1000) == 0)
        printf("Button %d now plays a %d-action macro (%dms delay between steps).\n",
               slot_1based, argc, delay_ms);
    else
        fprintf(stderr, "No confirmation assigning the macro to the button.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_watch(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, 0, NULL) != 0) return 1;

    printf("Listening on %s (Ctrl+C to quit)...\n", dev.path);
    ajazz_report_t r;
    while (ajazz_recv(&dev, &r, -1) == 0) {
        printf("family=%02x mode=%02x opcode=%02x payload=", r.family, r.mode, r.opcode);
        hexdump(r.payload, AJAZZ_PAYLOAD_LEN);
    }

    ajazz_close(&dev);
    return 0;
}

static int cmd_raw(const char *hex)
{
    if (strlen(hex) != AJAZZ_REPORT_LEN * 2) {
        fprintf(stderr, "Expected exactly %d bytes of hex (%d characters).\n",
                AJAZZ_REPORT_LEN, AJAZZ_REPORT_LEN * 2);
        return 1;
    }

    ajazz_report_t r;
    uint8_t *b = (uint8_t *)&r;
    for (int i = 0; i < AJAZZ_REPORT_LEN; i++) {
        unsigned v;
        if (sscanf(hex + i * 2, "%2x", &v) != 1) {
            fprintf(stderr, "Invalid hex at position %d\n", i);
            return 1;
        }
        b[i] = (uint8_t)v;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, 0, NULL) != 0) return 1;

    if (ajazz_send(&dev, &r) != 0) {
        fprintf(stderr, "Failed to send.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t reply;
    if (ajazz_recv(&dev, &reply, 1000) == 0) {
        printf("Reply: ");
        hexdump((uint8_t *)&reply, AJAZZ_REPORT_LEN);
    } else {
        printf("(no reply)\n");
    }

    ajazz_close(&dev);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        return cmd_info();
    } else if (strcmp(argv[1], "battery") == 0) {
        return cmd_battery();
    } else if (strcmp(argv[1], "dpi") == 0 && argc >= 3) {
        if (strcmp(argv[2], "list") == 0)
            return cmd_dpi_list();
        if (strcmp(argv[2], "set") == 0 && argc == 5)
            return cmd_dpi_set(atoi(argv[3]), atoi(argv[4]));
        if (strcmp(argv[2], "active") == 0 && argc == 4)
            return cmd_dpi_active(atoi(argv[3]));
        if (strcmp(argv[2], "reset") == 0)
            return cmd_dpi_reset();
    } else if (strcmp(argv[1], "dpicolor") == 0 && argc >= 3) {
        if (strcmp(argv[2], "list") == 0)
            return cmd_dpicolor_list();
        if (strcmp(argv[2], "set") == 0 && argc == 5)
            return cmd_dpicolor_set(atoi(argv[3]), argv[4]);
        if (strcmp(argv[2], "reset") == 0)
            return cmd_dpicolor_reset();
    } else if (strcmp(argv[1], "rate") == 0 && argc >= 3) {
        if (strcmp(argv[2], "get") == 0)
            return cmd_rate_get();
        if (strcmp(argv[2], "set") == 0 && argc == 4)
            return cmd_rate_set(atoi(argv[3]));
    } else if (strcmp(argv[1], "sensor") == 0 && argc >= 3) {
        if (strcmp(argv[2], "get") == 0)
            return cmd_sensor_get();
        if (argc == 4 && strcmp(argv[2], "liftoff") == 0)
            return sensor_modify("Lift-off distance (mm)", edit_liftoff, atoi(argv[3]));
        if (argc == 4 && strcmp(argv[2], "anglesnap") == 0)
            return sensor_modify("Angle snapping", edit_angle, atoi(argv[3]));
        if (argc == 4 && strcmp(argv[2], "ripple") == 0)
            return sensor_modify("Ripple control", edit_ripple, atoi(argv[3]));
        if (argc == 4 && strcmp(argv[2], "motionsync") == 0)
            return sensor_modify("Motion sync", edit_msync, atoi(argv[3]));
    } else if (strcmp(argv[1], "power") == 0 && argc >= 3) {
        if (strcmp(argv[2], "get") == 0)
            return cmd_power_get();
        if (argc == 4 && strcmp(argv[2], "sleep") == 0)
            return power_modify("Auto-sleep (s)", atoi(argv[3]), ap_sleep);
        if (argc == 4 && strcmp(argv[2], "wake") == 0)
            return power_modify("Wake on movement", atoi(argv[3]), ap_wake);
        if (argc == 4 && strcmp(argv[2], "light-idle-off") == 0)
            return power_modify("Light off when idle", atoi(argv[3]), ap_idle);
        if (argc == 4 && strcmp(argv[2], "debounce") == 0)
            return power_modify("Button debounce (ms)", atoi(argv[3]), ap_deb);
    } else if (strcmp(argv[1], "rgb") == 0 && argc >= 3) {
        if (strcmp(argv[2], "off") == 0)
            return cmd_rgb_off();
        if (strcmp(argv[2], "get") == 0)
            return cmd_rgb_get();
        return cmd_rgb_set(argc - 2, argv + 2);
    } else if (strcmp(argv[1], "keys") == 0 && argc >= 3) {
        if (strcmp(argv[2], "reset") == 0 && argc == 3)
            return cmd_keys_reset();
        if (strcmp(argv[2], "reset-slot") == 0 && argc == 4)
            return cmd_keys_reset_slot(atoi(argv[3]));
        if (strcmp(argv[2], "set") == 0 && argc == 5)
            return cmd_keys_set(atoi(argv[3]), argv[4]);
        if (strcmp(argv[2], "set-key") == 0 && argc >= 5)
            return cmd_keys_set_key(atoi(argv[3]), argv[4], argc - 5, argv + 5);
        if (strcmp(argv[2], "set-fire") == 0 && argc == 6)
            return cmd_keys_set_fire(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
        if (strcmp(argv[2], "list-functions") == 0) {
            print_key_functions();
            return 0;
        }
    } else if (strcmp(argv[1], "macro") == 0 && argc >= 3) {
        if (strcmp(argv[2], "set") == 0 && argc >= 6)
            return cmd_macro_set(atoi(argv[3]), atoi(argv[4]), argc - 5, argv + 5);
    } else if (strcmp(argv[1], "reset") == 0 && argc >= 3 && strcmp(argv[2], "factory") == 0) {
        return cmd_factory_reset(argc == 4 && strcmp(argv[3], "-y") == 0);
    } else if (strcmp(argv[1], "watch") == 0) {
        return cmd_watch();
    } else if (strcmp(argv[1], "raw") == 0 && argc == 3) {
        return cmd_raw(argv[2]);
    }

    print_usage(argv[0]);
    return 1;
}
