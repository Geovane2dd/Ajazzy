#ifndef AJAZZ_PROTOCOL_H
#define AJAZZ_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define AJAZZ_REPORT_LEN 32
#define AJAZZ_PAYLOAD_LEN 27
#define AJAZZ_DPI_STAGES 6

/* Factory-default DPI value and indicator colour per stage. Pulled straight
 * from the official installer's per-model config files (device/mouse_*.xml's
 * <dpi_info>), not guessed. All 10 models the installer ships a config for
 * (aj139pro, aj139v2pro, aj159, aj159mc, aj159pmc, aj179, aj179pmc, aj179v2,
 * aj179v2max, t500) list the same six values and colours, so one shared
 * table is enough. */
extern const uint16_t AJAZZ_DEFAULT_DPI[AJAZZ_DPI_STAGES];
extern const uint8_t AJAZZ_DEFAULT_DPI_COLOR[AJAZZ_DPI_STAGES][3];

/* Live battery raw level, inside the 0x10 (device info) query reply's
 * payload. See the battery note below. */
#define AJAZZ_BATTERY_PAYLOAD_OFFSET 9

/*
 * Wire format. Captured with USBPcap off the official Windows driver
 * (AJAZZ Driver (X) 1.0.7.3) talking to an AJ179 V2 MAX, dev_id N625, over
 * its 2.4G dongle (interface MI_02). Reports are 32 bytes, sent as an
 * interrupt OUT report and echoed back on interrupt IN.
 *
 * byte0  family   - command group ("what setting")
 * byte1  0x00     - always zero
 * byte2  mode     - 0x01 for SET, 0x00 for GET/query
 * byte3  opcode   - sub-command within the family
 * byte4..30       - payload (27 bytes)
 * byte31 checksum - sum(byte4..30) & 0xFF
 *
 * GET queries are sent as [queryId, 0x00, 0x00, 0x00, 0...0]. The device
 * replies with the same queryId in byte0, mode=0x01, and the opcode byte
 * set to how many payload bytes are actually meaningful (same convention
 * AJAZZ_FAM_MACRO uses on the SET side). Query IDs: 0x10 device info, 0x11
 * key table, 0x12 report rate, 0x13 DPI table (active stage included),
 * 0x14 per-DPI-stage LED colour table, 0x15 RGB effect state, 0x16
 * sensor performance, 0x17 power/debounce, 0x20 battery (unreliable, see below).
 * There's no dedicated "active DPI stage" query; read it out of 0x13's
 * payload.
 *
 * Battery: query 0x20's payload isn't a live reading. It comes back stuck
 * on a stale value, or with opcode 0 (no data at all). The real,
 * live-updating level sits at AJAZZ_BATTERY_PAYLOAD_OFFSET (payload[9],
 * wire byte 13) of the 0x10 device info reply, which this driver already
 * queries for dev_id. That byte tracks the mouse's own unsolicited
 * "c0 <flag> <raw> 00..." status push (sent on dongle reconnect and on
 * charge-cable plug/unplug) and matches the official driver's on-screen
 * bar count. See docs/reverse-engineering.md.
 *
 * Key table (family 0x09 SET / query 0x11 GET, opcode 0x0f): a 27-byte
 * payload as nine 3-byte slots, [marker][lo][hi]. Only the first five map
 * to physical buttons; a slot's factory default is [0x10][1<<N][0x00]. A
 * remapped button gets [0x80][lo][hi], a 16-bit little-endian USB HID
 * Consumer Page usage code (0x00E9 Volume Up, 0x00EA Volume Down, 0x00E2
 * Mute, 0x0223 AC Home, etc.). Checked against real button behaviour, not
 * just the wire echo. Slots 5+ are preserved byte-for-byte rather than
 * interpreted; they never change no matter what the first five slots do. */

typedef struct {
    uint8_t family;
    uint8_t reserved0;
    uint8_t mode;
    uint8_t opcode;
    uint8_t payload[AJAZZ_PAYLOAD_LEN];
    uint8_t checksum;
} __attribute__((packed)) ajazz_report_t;

/* Command families (byte0) used by SET commands */
enum {
    AJAZZ_FAM_REPORT_RATE  = 0x02, /* opcode 0x01: polling interval, see below */
    AJAZZ_FAM_DPI_TABLE    = 0x03, /* opcode 0x25: all 6 DPI stages, active
                                       stage in payload[0]'s high nibble */
    AJAZZ_FAM_DPI_COLOR    = 0x04, /* opcode 0x12: per-DPI-stage indicator
                                       LED colour table, see below */
    AJAZZ_FAM_MISC         = 0x05, /* opcode 0x05 constant-light brightness,
                                       0x18 rainbow/breathing, 0x02 off */
    AJAZZ_FAM_SENSOR_PERF  = 0x06, /* opcode 0x05: lift-off, angle snap, ripple,
                                       motion sync, see below */
    AJAZZ_FAM_SENSOR       = 0x07, /* opcode 0x04: sleep timer, wake on motion,
                                       light-off when idle, button debounce */
    AJAZZ_FAM_MACRO        = 0x08, /* opcode = length of used payload bytes */
    AJAZZ_FAM_KEY_TABLE    = 0x09, /* opcode 0x0f: button/key mapping table */
    AJAZZ_FAM_FACTORY_RESET = 0x0f, /* opcode 0x01, payload[0]=0xff: restores
                                        everything (DPI, colours, report
                                        rate, lighting, sensor, buttons,
                                        macros) to factory defaults */
};

/* Restores the whole mouse to factory defaults in one shot; the same
 * command the official driver's "Restore Factory Settings" button sends.
 * Reconstructed via Ghidra decompilation
 * (DeviceMainDlg::SetMouseRestoreFactoryData) rather than observed on the
 * wire, so treat it as decompiled-but-unverified. It should work, and
 * real-hardware reports either way are welcome. No per-section undo once
 * it's sent. */
void ajazz_build_factory_reset(ajazz_report_t *r);

/* Per-DPI-stage indicator LED colour, SET side. Reconstructed via Ghidra
 * decompilation (DeviceMainDlg::SetMouseDPIColorData) rather than a wire
 * capture, but confirmed against real hardware: the colour round-trips
 * through dpicolor set/list and matches the mouse's actual LED. Family
 * 0x04, mode 0x01, opcode 0x12 (18 = AJAZZ_DPI_STAGES * 3). Payload is 6
 * back-to-back {R,G,B} triples, one per DPI stage in order, the mirror
 * image of AJAZZ_QUERY_DPI_COLOR_TABLE's (0x14) reply shape. */
void ajazz_build_set_dpi_colors(ajazz_report_t *r, const uint8_t colors[AJAZZ_DPI_STAGES][3]);

/* Parses a 0x14 query reply's payload (6 back-to-back {R,G,B} triples, one
 * per DPI stage) into a plain array. Unlike the SET command above, this
 * GET side has been seen on the wire; see the protocol comment block near
 * the top of this file. */
void ajazz_parse_dpi_color_table(const ajazz_report_t *r, uint8_t colors[AJAZZ_DPI_STAGES][3]);

/* Report rate: family 0x02, opcode 0x01, payload[0] = interval in ms
 * (8/4/2/1 for 125/250/500/1000 Hz). Report rate only -- use
 * AJAZZ_FAM_DPI_TABLE for DPI stage, they're independent commands. */

#define AJAZZ_KEY_SLOTS 6

/* A macro fits in one report: 27-byte payload = 1 marker byte + up to 6
 * four-byte steps (24 bytes), 2 bytes spare. Longer macros need a
 * multi-report chunked transfer, see AJAZZ_MACRO_MAX_CHUNKED_STEPS. */
#define AJAZZ_MACRO_MAX_STEPS 6

/* Chunked writes for macros past AJAZZ_MACRO_MAX_STEPS. The byte stream
 * `[key_index, step0, step1, ...]` isn't step-aligned (a step's bytes can
 * straddle a chunk boundary) and gets split into AJAZZ_PAYLOAD_LEN-byte
 * pieces. Each chunk's mode byte is (chunk_index << 4) | total_chunks
 * (0-based, 1-based, so a 2-chunk transfer sends mode 0x02 then 0x12), and
 * opcode is that chunk's byte count. Confirmed via Ghidra decompilation of
 * the official driver's DeviceMainDlg::SetMouseKeyMacroData(int,int)
 * overload -- a wire capture alone can't pin down the total_chunks half of
 * that formula, since any single capture only ever shows one chunk count
 * (a mode byte of 0x02/0x12 for a 2-chunk transfer looks identical to a
 * hardcoded low nibble of 0x02 unless a transfer with a different chunk
 * count also gets captured).
 *
 * The mode byte's nibbles cap both chunk_index and total_chunks at 15, so
 * the wire format tops out at 15 chunks * AJAZZ_PAYLOAD_LEN bytes minus
 * the 1-byte header, divided into 4-byte steps. The constants below
 * reflect that ceiling. The mouse's actual onboard macro memory limit is
 * untested past what's been tried by hand -- if the device rejects or
 * silently drops a very long macro, that's a firmware limit below this
 * ceiling, not a bug here. */
#define AJAZZ_MACRO_MAX_CHUNKS 15
#define AJAZZ_MACRO_MAX_CHUNKED_STEPS \
    (((AJAZZ_MACRO_MAX_CHUNKS * AJAZZ_PAYLOAD_LEN) - 1) / 4)
#define AJAZZ_MACRO_MAX_CHUNKED_ACTIONS (AJAZZ_MACRO_MAX_CHUNKED_STEPS / 2)

/* Standard USB HID keyboard report modifier bits, for
 * ajazz_key_slot_set_keyboard() */
enum {
    AJAZZ_MOD_LCTRL  = 0x01,
    AJAZZ_MOD_LSHIFT = 0x02,
    AJAZZ_MOD_LALT   = 0x04,
    AJAZZ_MOD_LGUI   = 0x08,
    AJAZZ_MOD_RCTRL  = 0x10,
    AJAZZ_MOD_RSHIFT = 0x20,
    AJAZZ_MOD_RALT   = 0x40,
    AJAZZ_MOD_RGUI   = 0x80,
};

/* Common USB HID Consumer Page usage codes, for ajazz_key_slot_set_consumer() */
enum {
    AJAZZ_USAGE_VOLUME_UP    = 0x00E9,
    AJAZZ_USAGE_VOLUME_DOWN  = 0x00EA,
    AJAZZ_USAGE_MUTE         = 0x00E2,
    AJAZZ_USAGE_PLAY_PAUSE   = 0x00CD,
    AJAZZ_USAGE_NEXT_TRACK   = 0x00B5,
    AJAZZ_USAGE_PREV_TRACK   = 0x00B6,
    AJAZZ_USAGE_STOP         = 0x00B7,
    AJAZZ_USAGE_AC_HOME      = 0x0223,
    AJAZZ_USAGE_AC_BACK      = 0x0224,
    AJAZZ_USAGE_AC_FORWARD   = 0x0225,
    AJAZZ_USAGE_AC_REFRESH   = 0x0227,
    AJAZZ_USAGE_AC_BOOKMARKS = 0x022A,
};

/* Query IDs. Names match the firmware's own debug strings, found in the
 * Windows driver's bundled OTA image: "cmd get all key", "cmd get
 * reportrate", "cmd get dpi value", "cmd get dpi color", "cmd get led
 * mode", "cmd get Sensor advanced par", in that order. */
enum {
    AJAZZ_QUERY_DEVICE_INFO  = 0x10,
    AJAZZ_QUERY_KEY_TABLE    = 0x11,

    /* Report rate: setting 125, 500, and 1000 Hz in turn produces
     * payload[0] values of 8, 2, and 1 (interval in ms). There's no
     * dedicated "active DPI stage" query; read that from
     * AJAZZ_QUERY_DPI_TABLE's high nibble instead
     * (ajazz_dpi_table_active_stage()). */
    AJAZZ_QUERY_REPORT_RATE  = 0x12,

    AJAZZ_QUERY_DPI_TABLE    = 0x13,

    /* Per-DPI-stage LED colour table, not a custom palette -- see "The
     * DPI table" in docs/reverse-engineering.md. Reply's opcode equals
     * colour_count*3, followed by that many raw R,G,B bytes, no header. */
    AJAZZ_QUERY_DPI_COLOR_TABLE = 0x14,

    /* RGB effect state ("cmd get led mode"). Cycling through
     * off/rainbow/constant and reading back after each change gives a
     * stable opcode per effect: 0x01 off, 0x05 constant, 0x18 rainbow.
     * See ajazz_parse_light(). */
    AJAZZ_QUERY_LED_MODE     = 0x15,

    AJAZZ_QUERY_SENSOR_PERF  = 0x16, /* lift-off, angle snap, ripple, motion sync */
    AJAZZ_QUERY_POWER        = 0x17, /* sleep, wake-on-motion, idle light-off, debounce */
    AJAZZ_QUERY_BATTERY      = 0x20,
};

void ajazz_finalize(ajazz_report_t *r);

void ajazz_build_query(ajazz_report_t *r, uint8_t query_id);

/* Writes all 6 DPI stages and switches the active one in a single report;
 * that's how the real driver changes DPI stage, there's no separate "just
 * switch stage" command. To change only the active stage, query
 * AJAZZ_QUERY_DPI_TABLE, parse it with ajazz_parse_dpi_table(), and call
 * this back with the same dpi[] values and a new active_stage_0based. */
void ajazz_build_set_dpi_table(ajazz_report_t *r, const uint16_t dpi[AJAZZ_DPI_STAGES], uint8_t active_stage_0based,
    uint8_t count_nibble);
/* payload[0]'s low nibble is the original driver's "DPI count" (the number
 * of stages the DPI button cycles through). A freshly-reset V2 MAX reports 5
 * here even though 6 values are stored, so callers should read-modify-write
 * it (ajazz_dpi_table_count()) unless the user explicitly changes it. */
int ajazz_dpi_table_count(const ajazz_report_t *r);
void ajazz_build_set_report_rate(ajazz_report_t *r, uint8_t rate_index);
/*
 * Lighting (family 0x05). Decompiled from the official driver; verified on
 * an AJ179 V2 MAX by sending each mode and reading query 0x15 back.
 *
 * Every mode starts payload = [mode, (brightness << 4) | speed, ...]:
 *
 *   mode 0      opcode 0x02  off (readback opcode 0x01)
 *   mode 1      opcode 0x03  "Flowing" (stream) -- brightness + speed only
 *   mode 2      opcode 0x18  "Breathing": payload[2] = colour count (7),
 *                            then 7 x RGB. Colours are cycled with a
 *                            breathing fade, so 7 different colours give
 *                            the multi-colour effect the old code called
 *                            "rainbow", and 7 identical ones a single-colour
 *                            breath.
 *   mode 3      opcode 0x05  "Steady": payload[2..4] = RGB
 *   mode 4      opcode 0x02  "Neon"            -- brightness + speed only
 *   mode 5      opcode 0x02  "Colourful wave"  -- brightness + speed only
 *
 * brightness and speed are 0-4 (nibbles).
 */
enum {
    AJAZZ_LIGHT_OFF       = 0,
    AJAZZ_LIGHT_FLOW      = 1,
    AJAZZ_LIGHT_BREATHING = 2,
    AJAZZ_LIGHT_STEADY    = 3,
    AJAZZ_LIGHT_NEON      = 4,
    AJAZZ_LIGHT_WAVE      = 5,
};
#define AJAZZ_LIGHT_PALETTE 7

void ajazz_build_set_light_simple(ajazz_report_t *r, uint8_t mode, uint8_t brightness, uint8_t speed);
void ajazz_build_set_light_steady(ajazz_report_t *r, uint8_t brightness, uint8_t speed, const uint8_t rgb[3]);
void ajazz_build_set_light_breathing(ajazz_report_t *r, uint8_t brightness, uint8_t speed,
    const uint8_t palette[AJAZZ_LIGHT_PALETTE][3]);
/* Default 7-colour palette (red, green, blue, cyan, yellow, magenta, white). */
extern const uint8_t AJAZZ_LIGHT_DEFAULT_PALETTE[AJAZZ_LIGHT_PALETTE][3];

void ajazz_build_set_rgb_off(ajazz_report_t *r);

typedef struct {
    uint8_t mode;       /* AJAZZ_LIGHT_* */
    uint8_t brightness; /* 0-4 */
    uint8_t speed;      /* 0-4 */
    uint8_t rgb[3];     /* steady colour */
    uint8_t palette[AJAZZ_LIGHT_PALETTE][3]; /* breathing colours */
} ajazz_light_state_t;

/* Decodes a query 0x15 reply. Returns 0 on success, -1 for an unknown shape. */
int ajazz_parse_light(const ajazz_report_t *r, ajazz_light_state_t *out);

/*
 * Power / button settings (family 0x07, opcode 0x04; query 0x17).
 * Decompiled: payload = [sleep, move_wakeup, move_closelight, button_respondtime].
 *
 *   payload[0] auto-sleep, in units of 10 seconds
 *   payload[1] wake the mouse on movement (0/1)
 *   payload[2] turn the light off when the mouse is idle (0/1)
 *   payload[3] button debounce in milliseconds (factory 8)
 *
 * Earlier versions of this project treated payload[1] as "lift-off
 * distance" -- it is the wake-on-motion flag; lift-off lives in family 0x06.
 * Writes are still read-modify-write so unknown bytes survive.
 */
void ajazz_build_power_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply);
void ajazz_power_set_sleep_seconds(ajazz_report_t *r, uint16_t seconds);
void ajazz_power_set_wake_on_motion(ajazz_report_t *r, uint8_t enabled);
void ajazz_power_set_light_idle_off(ajazz_report_t *r, uint8_t enabled);
void ajazz_power_set_debounce_ms(ajazz_report_t *r, uint8_t ms);

/*
 * Sensor performance (family 0x06, opcode 0x05; query 0x16).
 * payload = [lift_off, angle_snap, ripple_control, motion_sync].
 * lift_off is 1 or 2 (mm). Names are the official driver's checkboxes.
 */
typedef struct {
    uint8_t lift_off;      /* 1 or 2 mm */
    uint8_t angle_snap;
    uint8_t ripple_control;
    uint8_t motion_sync;
} ajazz_sensor_t;
void ajazz_parse_sensor_perf(const ajazz_report_t *r, ajazz_sensor_t *out);
void ajazz_build_set_sensor_perf(ajazz_report_t *r, const ajazz_sensor_t *s);

/* Resets the key table to factory defaults. Confirmed against real
 * hardware: a button that had been custom-mapped away from its default
 * returned to it after this command. The official Windows driver's own UI
 * can look stale afterward, though -- it reads from its local SQLite
 * database rather than the device, so a reset issued from outside that
 * application won't show up there even though the hardware genuinely
 * changed.
 *
 * Remapping to an arbitrary function isn't implemented; only a handful of
 * example codes have turned up in captures, not a full
 * function-id-to-byte codebook. */
void ajazz_build_reset_keys(ajazz_report_t *r);

/* Turns a query 0x11 reply into a ready-to-edit SET report. Follow with
 * ajazz_key_slot_set_consumer() or similar to change one slot. */
void ajazz_build_key_table_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply);

/* Remaps button `slot` (0-based) to a USB HID Consumer Page usage code.
 * `r` must already hold a full key table (from
 * ajazz_build_key_table_from_query()); only this slot changes. */
void ajazz_key_slot_set_consumer(ajazz_report_t *r, int slot, uint16_t consumer_usage);

/* Remaps button `slot` to a keyboard key: `modifiers` is the usual
 * keyboard-report bitmask (bit0 LCtrl, bit1 LShift, bit2 LAlt, bit3 LGui,
 * bits 4-7 the right-hand versions), `keycode` a USB HID Keyboard Page
 * usage (e.g. 0x04 = 'A'). */
void ajazz_key_slot_set_keyboard(ajazz_report_t *r, int slot, uint8_t modifiers, uint8_t keycode);

/* Remaps button `slot` to one of the mouse's own built-in functions (the
 * official driver's "Mouse Features" list). Encodings come from its
 * button-table writer, not from captures; the ones marked "unverified" have
 * not been physically exercised. */
typedef enum {
    AJAZZ_BTN_LEFT = 1,      /* [0x10, 0x01] */
    AJAZZ_BTN_MIDDLE,        /* [0x10, 0x04] */
    AJAZZ_BTN_RIGHT,         /* [0x10, 0x02] */
    AJAZZ_BTN_BACK,          /* [0x10, 0x08] */
    AJAZZ_BTN_FORWARD,       /* [0x10, 0x10] */
    AJAZZ_BTN_DOUBLE_CLICK,  /* [0x30, 0x32, 0x03] (unverified) */
    AJAZZ_BTN_SCROLL_UP,     /* [0x20, 0x01] (unverified) */
    AJAZZ_BTN_SCROLL_DOWN,   /* [0x20, 0xff] (unverified) */
    AJAZZ_BTN_SCROLL_LEFT,   /* [0x10, 0x00, 0xf5] (unverified) */
    AJAZZ_BTN_SCROLL_RIGHT,  /* [0x10, 0x00, 0xf6] (unverified) */
    AJAZZ_BTN_DPI_LOOP,      /* [0x40, 0x01] -- factory default of the DPI button */
    AJAZZ_BTN_DPI_UP,        /* [0x40, 0x02] (unverified) */
    AJAZZ_BTN_DPI_DOWN,      /* [0x40, 0x03] (unverified) */
    AJAZZ_BTN_DISABLED,      /* [0x60, 0x00] */
} ajazz_button_fn_t;
void ajazz_key_slot_set_function(ajazz_report_t *r, int slot, ajazz_button_fn_t fn);

/* System shortcuts from the official driver's "System Shortcut" list. */
typedef enum {
    AJAZZ_SYS_BRIGHTNESS_UP = 1,
    AJAZZ_SYS_BRIGHTNESS_DOWN,
    AJAZZ_SYS_CALCULATOR,
    AJAZZ_SYS_MY_COMPUTER,
    AJAZZ_SYS_WEBSITE,
    AJAZZ_SYS_MAIL,
    AJAZZ_SYS_SHOW_DESKTOP,   /* Win+D  */
    AJAZZ_SYS_SWITCH_APP,     /* Win+Tab */
    AJAZZ_SYS_COPY,           /* Ctrl+C */
    AJAZZ_SYS_PASTE,          /* Ctrl+V */
    AJAZZ_SYS_CUT,            /* Ctrl+X */
    AJAZZ_SYS_MEDIA_PLAYER,   /* consumer 0x0183 */
} ajazz_system_fn_t;
void ajazz_key_slot_set_system(ajazz_report_t *r, int slot, ajazz_system_fn_t fn);

/* Restores button `slot` to its native/default function. */
void ajazz_key_slot_reset_default(ajazz_report_t *r, int slot);

/* Rapid-fire/auto-click: repeats a click every `interval` (5-255) for
 * `number` repeats (0-255) while held. Confirmed structurally against one
 * wire capture (interval=5, number=1); edge values such as number=0 are
 * untested. */
void ajazz_key_slot_set_fire(ajazz_report_t *r, int slot, uint8_t interval, uint8_t number);

/*
 * Macros. One AJAZZ_FAM_MACRO SET report: mode 0x01, opcode = number of
 * payload bytes used (not a fixed id). payload[0] is the 0-based button index the macro belongs to (the early captures were all button 4, hence the constant 0x03 this file used to claim),
 * followed by up to AJAZZ_MACRO_MAX_STEPS four-byte steps:
 * [kind, delay_lo, delay_hi, value].
 *   kind     = AJAZZ_MACRO_MOUSE_DOWN/UP or AJAZZ_MACRO_KEY_DOWN/UP
 *   delay_ms = pause after this step, in ms (16-bit, see
 *              ajazz_macro_step_t below)
 *   value    = mouse button bit flag (1/2/4/...) for mouse steps, or a
 *              USB HID Keyboard Page usage code for key steps
 *
 * Assigning that macro to a button is a second write to the key table:
 * the slot's marker becomes 0x90 with a constant trailing [0x23, 0x01].
 * That pair never varied across captures with different macros, so the
 * device plays back whichever macro content was written most recently for
 * that button rather than looking one up by id. Write the macro content
 * before assigning it.
 */
typedef enum {
    AJAZZ_MACRO_MOUSE_DOWN = 0x10,
    AJAZZ_MACRO_MOUSE_UP   = 0x90,
    AJAZZ_MACRO_KEY_DOWN   = 0x30,
    AJAZZ_MACRO_KEY_UP     = 0xb0,
    /* Modifier keys (Ctrl/Shift/Alt/Win) are their own step kind in the
     * official driver; value is the HID modifier bit (AJAZZ_MOD_*). */
    AJAZZ_MACRO_MOD_DOWN   = 0x20,
    AJAZZ_MACRO_MOD_UP     = 0xa0,
} ajazz_macro_step_kind_t;

typedef struct {
    uint8_t kind;       /* an ajazz_macro_step_kind_t value */
    uint16_t delay_ms;  /* pause after this step, in milliseconds. 16-bit
                            little-endian on the wire (bytes 1-2 of the
                            step), not a 1-byte delay plus a constant byte.
                            Confirmed via Ghidra decompilation of the
                            official driver's DeviceMainDlg::GetMouseMacroData */
    uint8_t value;      /* mouse button bit, or USB HID keyboard usage code */
} ajazz_macro_step_t;

/* Builds the macro-content SET report for up to AJAZZ_MACRO_MAX_STEPS
 * steps. Extra steps are silently dropped; check the count yourself if
 * you need to report that clearly. */
void ajazz_build_macro_content(ajazz_report_t *r, uint8_t key_index, const ajazz_macro_step_t *steps, int n_steps);

/* Same idea for up to AJAZZ_MACRO_MAX_CHUNKED_STEPS steps: a single report
 * if it fits, chunked writes otherwise. Reconstructed via Ghidra
 * decompilation of the official driver; confirmed on the wire only up to
 * a couple of chunks so far, see the comment on
 * AJAZZ_MACRO_MAX_CHUNKED_STEPS. `out` needs room for at least
 * (AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + 1 + AJAZZ_PAYLOAD_LEN - 1) /
 * AJAZZ_PAYLOAD_LEN reports. Send them in order. Returns the report
 * count, or -1 if n_steps is over the limit. */
int ajazz_build_macro_content_chunked(ajazz_report_t *out, int max_out, uint8_t key_index, const ajazz_macro_step_t *steps, int n_steps);

/* Marks button `slot` (0-based) as macro-triggering. `r` must already
 * hold a full key table. Send the macro content first, see above. */
typedef enum {
    AJAZZ_MACRO_ONCE = 0,        /* play once per press */
    AJAZZ_MACRO_WHILE_HELD = 1,  /* repeat while the button is held */
    AJAZZ_MACRO_UNTIL_CLICK = 2, /* repeat until any click */
    AJAZZ_MACRO_N_TIMES = 3,     /* repeat `loop_count` times */
} ajazz_macro_loop_t;

/* Slot bytes are [0x90, key_index | flags, count] where flags are 0x10
 * (repeat while held), 0x40 (repeat until any click) or 0x20 (repeat N
 * times, count in byte 2). The earlier constant [0x23, 0x01] was simply
 * "button 4, 1 repeat" from the captures. */
void ajazz_key_slot_set_macro(ajazz_report_t *r, int slot, ajazz_macro_loop_t loop, uint8_t loop_count);

/* Parses a DPI table payload (query 0x13 reply, or a family 0x03 report). */
void ajazz_parse_dpi_table(const ajazz_report_t *r, uint16_t dpi_out[AJAZZ_DPI_STAGES]);

/* 0-based active DPI stage, from a DPI table payload's high nibble. No
 * separate query exists for this. */
int ajazz_dpi_table_active_stage(const ajazz_report_t *r);

/* Inverse of ajazz_build_set_report_rate()'s interval table; turns a
 * report-rate payload[0] value back into Hz. Returns 0 if it doesn't
 * match one of the four known intervals. */
int ajazz_report_rate_hz(uint8_t interval_ms);

/* Takes the raw byte at AJAZZ_BATTERY_PAYLOAD_OFFSET in a 0x10 device info
 * reply (not the broken 0x20 query, see the note above). It's a plain
 * 0-100 percentage, split into even quarters. Confirmed via Ghidra
 * decompilation of the official Windows driver (MUI.dll,
 * DUI::BatteryCtrl::DrawControl @ 0x1f9b0), which compares
 * this->m_battery_value against 0x19/0x32/0x4b (25/50/75) to pick how
 * many of the 4 bars to fill. See the battery section in
 * docs/reverse-engineering.md. */
const char *ajazz_battery_label(uint8_t raw);

#endif
