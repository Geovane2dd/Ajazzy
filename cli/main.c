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
        "  rate get                    Show the current report rate\n"
        "  rate set <125|250|500|1000> Set the report/polling rate\n"
        "  liftoff set <1|2>           Set lift-off distance (may also affect wake-on-motion)\n"
        "  power get                   Show sleep timer, lift-off level, idle light-off\n"
        "  power sleep <seconds>       Set the auto-sleep timer (0-2550s, rounded to 10s)\n"
        "  power light-idle-off <0|1>  Turn the light off when the mouse is idle\n"
        "  rgb rainbow <bri 0-4> <speed 0-4>  Turn on the rainbow effect (bri: 0/25/50/75/100%%)\n"
        "  rgb constant <bri 0-4>      Turn on solid/constant-colour light\n"
        "  rgb off                     Turn the RGB off\n"
        "  rgb get                     Show the current lighting effect\n"
        "  keys reset                  Reset ALL buttons to their default function\n"
        "  keys reset-slot <1-5>       Reset one button to its default function\n"
        "  keys set <1-5> <function>   Remap a button (volume-up, mute, forward, ...)\n"
        "  keys set-key <1-5> <key> [ctrl] [shift] [alt] [win]\n"
        "                               Remap a button to a keyboard key\n"
        "  keys set-fire <1-5> <interval 5-255> <number 0-255>\n"
        "                               Turn a button into a rapid-fire/auto-click button\n"
        "  keys list-functions         List the functions 'keys set' accepts\n"
        "  macro set <1-5> <delay ms> <action> [action...]\n"
        "                               Assign a press+release macro to a button (up to %d\n"
        "                               actions, %d+ is an experimental multi-packet write);\n"
        "                               each action is a key (a-z, 0-9, hex code) or a mouse\n"
        "                               button (lclick, rclick, mclick, btn4, btn5)\n"
        "  watch                       Dump raw incoming reports (debugging)\n"
        "  raw <32 bytes hex>          Send a raw report (debugging/research)\n",
        prog, AJAZZ_MACRO_MAX_CHUNKED_ACTIONS, AJAZZ_MACRO_MAX_STEPS / 2 + 1);
}

/* pass 0/NULL for `cap`/`feature_name` on commands that don't map to a
 * single capability (info, watch, raw) */
static int open_device(ajazz_dev_t *dev, uint32_t cap, const char *feature_name)
{
    if (ajazz_open(dev) != 0) {
        fprintf(stderr, "No AJAZZ mouse found (%s).\n"
                        "Check that the dongle/mouse is plugged in and that you have\n"
                        "read/write access to /dev/hidraw* (see udev/71-ajazz.rules).\n",
                strerror(errno));
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
    if (ajazz_query(&dev, AJAZZ_QUERY_BATTERY, &reply, 1000) == 0) {
        printf("Battery: %s\n", ajazz_battery_label(reply.payload[0]));
        printf("(raw sensor value: %d -- the original driver shows a 4-bar icon,\n"
               "not a calibrated percentage, so these levels are an estimate)\n",
               reply.payload[0]);
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
    ajazz_build_set_dpi_table(&req, dpi, (uint8_t)(stage - 1));

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
    ajazz_build_set_dpi_table(&req, dpi, (uint8_t)(stage - 1));

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Active DPI stage set to %d.\n", stage);
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

static int cmd_liftoff_set(int level)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "lift-off/sensor settings") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current sensor settings.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_sensor_from_query(&req, &reply);
    ajazz_sensor_set_liftoff(&req, (uint8_t)level);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Lift-off set: level=%d (note: this byte may also control\n"
               "\"wake mouse on motion\" -- see protocol/hid.h)\n", level);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_power_sleep(int seconds)
{
    if (seconds < 0 || seconds > 2550) {
        fprintf(stderr, "Seconds must be between 0 and 2550 (42.5 minutes).\n");
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "sleep timer") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current sensor settings.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_sensor_from_query(&req, &reply);
    ajazz_sensor_set_sleep_seconds(&req, (uint16_t)seconds);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Auto-sleep timer set to %d seconds.\n", (seconds / 10) * 10);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_power_light_idle_off(int enabled)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "idle light-off toggle") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
        fprintf(stderr, "Could not read the current sensor settings.\n");
        ajazz_close(&dev);
        return 1;
    }

    ajazz_report_t req;
    ajazz_build_sensor_from_query(&req, &reply);
    ajazz_sensor_set_light_idle_off(&req, (uint8_t)enabled);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Turn light off when idle: %s\n", enabled ? "on" : "off");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_power_get(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_SENSOR, "sensor/power settings") != 0) return 1;

    ajazz_report_t reply;
    if (ajazz_query(&dev, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
        printf("Could not read the sensor/power settings.\n");
        ajazz_close(&dev);
        return 1;
    }

    printf("Auto-sleep timer: %d seconds\n", reply.payload[0] * 10);
    printf("Lift-off level: %d (note: this byte may also be \"wake mouse on\n"
           "motion\" -- see protocol/hid.h)\n", reply.payload[1]);
    printf("Turn light off when idle: %s\n", reply.payload[2] ? "on" : "off");

    ajazz_close(&dev);
    return 0;
}

static int cmd_rgb_rainbow(int brightness, int speed)
{
    if (brightness < 0 || brightness > 4 || speed < 0 || speed > 4) {
        fprintf(stderr, "Brightness and speed must be between 0 and 4.\n");
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RGB, "rgb lighting") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_set_rgb_rainbow(&req, (uint8_t)brightness, (uint8_t)speed);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Rainbow effect on (brightness=%d speed=%d).\n", brightness, speed);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_rgb_constant(int brightness)
{
    if (brightness < 0 || brightness > 4) {
        fprintf(stderr, "Brightness must be between 0 and 4.\n");
        return 1;
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RGB, "rgb lighting") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_set_rgb_constant(&req, (uint8_t)brightness);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("Constant light on (brightness=%d).\n", brightness);
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
}

static int cmd_rgb_off(void)
{
    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_RGB, "rgb lighting") != 0) return 1;

    ajazz_report_t req;
    ajazz_build_set_rgb_off(&req);

    if (ajazz_send_and_wait(&dev, &req, 1000) == 0)
        printf("RGB turned off.\n");
    else
        fprintf(stderr, "No confirmation from the device.\n");

    ajazz_close(&dev);
    return 0;
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

    ajazz_led_state_t st;
    if (ajazz_parse_led_mode(&reply, &st) != 0) {
        printf("Lighting: unrecognized state, raw opcode=0x%02x\n", st.effect);
        printf("raw payload: ");
        hexdump(reply.payload, AJAZZ_PAYLOAD_LEN);
        ajazz_close(&dev);
        return 0;
    }

    switch (st.effect) {
        case AJAZZ_LED_OFF:
            printf("Lighting: off\n");
            break;
        case AJAZZ_LED_CONSTANT:
            printf("Lighting: constant colour (brightness=%d)\n", st.brightness);
            break;
        case AJAZZ_LED_RAINBOW:
            printf("Lighting: rainbow cycle (brightness=%d speed=%d)\n", st.brightness, st.speed);
            break;
    }

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
    if (delay_ms < 0 || delay_ms > 255) {
        fprintf(stderr, "Delay must be between 0 and 255 ms.\n");
        return 1;
    }
    if (argc < 1 || argc > AJAZZ_MACRO_MAX_CHUNKED_ACTIONS) {
        fprintf(stderr, "A macro needs 1 to %d actions (each is a press+release).\n",
                AJAZZ_MACRO_MAX_CHUNKED_ACTIONS);
        return 1;
    }
    if (argc > AJAZZ_MACRO_MAX_STEPS / 2)
        fprintf(stderr, "Note: macros over %d actions use an experimental multi-packet\n"
                        "write -- see AJAZZ_MACRO_MAX_CHUNKED_STEPS in protocol/hid.h.\n",
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
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_DOWN, (uint8_t)delay_ms, (uint8_t)btn};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_UP,   (uint8_t)delay_ms, (uint8_t)btn};
        } else {
            uint8_t keycode;
            if (lookup_keycode(argv[i], &keycode) != 0) {
                fprintf(stderr, "Unknown action: %s\n", argv[i]);
                print_macro_actions();
                return 1;
            }
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_DOWN, (uint8_t)delay_ms, keycode};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_UP,   (uint8_t)delay_ms, keycode};
        }
    }

    ajazz_dev_t dev;
    if (open_device(&dev, AJAZZ_CAP_MACRO, "macro") != 0) return 1;

    ajazz_report_t chunks[(AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + AJAZZ_PAYLOAD_LEN) / AJAZZ_PAYLOAD_LEN + 1];
    int n_chunks = ajazz_build_macro_content_chunked(chunks, (int)(sizeof(chunks) / sizeof(chunks[0])), steps, n_steps);
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
    ajazz_key_slot_set_macro(&assign, slot_1based - 1);

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
    } else if (strcmp(argv[1], "rate") == 0 && argc >= 3) {
        if (strcmp(argv[2], "get") == 0)
            return cmd_rate_get();
        if (strcmp(argv[2], "set") == 0 && argc == 4)
            return cmd_rate_set(atoi(argv[3]));
    } else if (strcmp(argv[1], "liftoff") == 0 && argc == 4 && strcmp(argv[2], "set") == 0) {
        return cmd_liftoff_set(atoi(argv[3]));
    } else if (strcmp(argv[1], "power") == 0 && argc >= 3) {
        if (strcmp(argv[2], "get") == 0)
            return cmd_power_get();
        if (strcmp(argv[2], "sleep") == 0 && argc == 4)
            return cmd_power_sleep(atoi(argv[3]));
        if (strcmp(argv[2], "light-idle-off") == 0 && argc == 4)
            return cmd_power_light_idle_off(atoi(argv[3]));
    } else if (strcmp(argv[1], "rgb") == 0 && argc >= 3) {
        if (strcmp(argv[2], "rainbow") == 0 && argc == 5)
            return cmd_rgb_rainbow(atoi(argv[3]), atoi(argv[4]));
        if (strcmp(argv[2], "constant") == 0 && argc == 4)
            return cmd_rgb_constant(atoi(argv[3]));
        if (strcmp(argv[2], "off") == 0)
            return cmd_rgb_off();
        if (strcmp(argv[2], "get") == 0)
            return cmd_rgb_get();
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
    } else if (strcmp(argv[1], "watch") == 0) {
        return cmd_watch();
    } else if (strcmp(argv[1], "raw") == 0 && argc == 3) {
        return cmd_raw(argv[2]);
    }

    print_usage(argv[0]);
    return 1;
}
