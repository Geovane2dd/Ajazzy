#ifndef AJAZZ_PROTOCOL_H
#define AJAZZ_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define AJAZZ_REPORT_LEN 32
#define AJAZZ_PAYLOAD_LEN 27
#define AJAZZ_DPI_STAGES 6

/*
 * Wire format, reverse engineered from USBPcap captures of the Windows
 * driver (AJAZZ Driver (X) 1.0.7.3) talking to an AJ179 V2 MAX (dev_id
 * N625) over its 2.4G dongle, interface MI_02. Reports are 32 bytes,
 * sent as an interrupt OUT report and echoed back on interrupt IN.
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
 * AJAZZ_FAM_MACRO uses on the SET side). Query IDs: 0x10 device info,
 * 0x11 key table, 0x12 report rate, 0x13 DPI table (active stage
 * included), 0x14 per-DPI-stage LED colour table, 0x15 RGB effect state,
 * 0x16 unknown, 0x17 sensor/lift-off, 0x20 battery. No dedicated "active
 * DPI stage" query -- read it out of 0x13's payload.
 *
 * Key table (family 0x09 SET / query 0x11 GET, opcode 0x0f): 27-byte
 * payload as nine 3-byte slots [marker][lo][hi]. Only the first five
 * slots map to physical buttons; a slot's factory default is
 * [0x10][1<<N][0x00]. A remapped button gets [0x80][lo][hi], a 16-bit
 * little-endian USB HID Consumer Page usage code (0x00E9 Volume Up,
 * 0x00EA Volume Down, 0x00E2 Mute, 0x0223 AC Home, etc) -- checked
 * against real button behaviour, not just the wire echo. Slots 5+ never
 * changed in any capture, so they're preserved byte-for-byte rather than
 * interpreted.
 */

typedef struct {
    uint8_t family;
    uint8_t reserved0;
    uint8_t mode;
    uint8_t opcode;
    uint8_t payload[AJAZZ_PAYLOAD_LEN];
    uint8_t checksum;
} __attribute__((packed)) ajazz_report_t;

/* command families (byte0) used by SET commands */
enum {
    AJAZZ_FAM_REPORT_RATE  = 0x02, /* opcode 0x01: polling interval, see below */
    AJAZZ_FAM_DPI_TABLE    = 0x03, /* opcode 0x25: all 6 DPI stages, active
                                       stage in payload[0]'s high nibble */
    AJAZZ_FAM_MISC         = 0x05, /* opcode 0x05 constant-light brightness,
                                       0x18 rainbow/breathing, 0x02 off */
    AJAZZ_FAM_SENSOR       = 0x07, /* opcode 0x04: sleep timer, lift-off,
                                       idle light-off, see below */
    AJAZZ_FAM_MACRO        = 0x08, /* opcode = length of used payload bytes */
    AJAZZ_FAM_KEY_TABLE    = 0x09, /* opcode 0x0f: button/key mapping table */
};

/* Report rate: family 0x02, opcode 0x01, payload[0] = interval in ms
 * (8/4/2/1 for 125/250/500/1000 Hz). This family is report rate only --
 * use AJAZZ_FAM_DPI_TABLE for DPI stage, they're independent commands. */

#define AJAZZ_KEY_SLOTS 5

/* A macro fits in one report: 27-byte payload = 1 marker byte + up to 6
 * four-byte steps (24 bytes), 2 bytes spare. Longer macros need a
 * multi-report chunked transfer -- see AJAZZ_MACRO_MAX_CHUNKED_STEPS. */
#define AJAZZ_MACRO_MAX_STEPS 6

/* Chunked writes for macros past AJAZZ_MACRO_MAX_STEPS: mode becomes
 * (chunk_index << 4) | 0x02 and opcode is the byte count in that chunk.
 * Only ever seen two chunks in a capture, so anything past that is
 * extrapolation -- ajazz_build_macro_content_chunked() is experimental
 * beyond AJAZZ_MACRO_MAX_STEPS. The cap below is a nibble's worth of
 * chunk index, comfortably more than anyone needs, kept low to stay
 * close to what was actually observed. */
#define AJAZZ_MACRO_MAX_CHUNKED_ACTIONS 10
#define AJAZZ_MACRO_MAX_CHUNKED_STEPS (AJAZZ_MACRO_MAX_CHUNKED_ACTIONS * 2)

/* standard USB HID keyboard report modifier bits, for
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

/* common USB HID Consumer Page usage codes, for ajazz_key_slot_set_consumer() */
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

/* Query IDs. Names match the firmware's own debug strings (found in the
 * Windows driver's bundled OTA image): "cmd get all key", "cmd get
 * reportrate", "cmd get dpi value", "cmd get dpi color", "cmd get led
 * mode", "cmd get Sensor advanced par", in that order. */
enum {
    AJAZZ_QUERY_DEVICE_INFO  = 0x10,
    AJAZZ_QUERY_KEY_TABLE    = 0x11,

    /* Report rate: set 125, 500 and 1000 Hz in turn and payload[0]
     * matches the interval-ms value each time (8/2/1). No dedicated
     * "active DPI stage" query exists; read that from
     * AJAZZ_QUERY_DPI_TABLE's high nibble instead
     * (ajazz_dpi_table_active_stage()). */
    AJAZZ_QUERY_REPORT_RATE  = 0x12,

    AJAZZ_QUERY_DPI_TABLE    = 0x13,

    /* Per-DPI-stage LED colour table, not a custom palette -- see "The
     * DPI table" in docs/reverse-engineering.md. Reply's opcode =
     * colour_count*3, then that many raw R,G,B bytes, no header. */
    AJAZZ_QUERY_DPI_COLOR_TABLE = 0x14,

    /* RGB effect state ("cmd get led mode"). Cycling off/rainbow/constant
     * and reading back after each change gives a stable opcode per
     * effect: 0x01 off, 0x05 constant, 0x18 rainbow. See
     * ajazz_parse_led_mode(). */
    AJAZZ_QUERY_LED_MODE     = 0x15,

    AJAZZ_QUERY_UNKNOWN16    = 0x16, /* not tied to anything specific yet */
    AJAZZ_QUERY_SENSOR       = 0x17,
    AJAZZ_QUERY_BATTERY      = 0x20,
};

void ajazz_finalize(ajazz_report_t *r);

void ajazz_build_query(ajazz_report_t *r, uint8_t query_id);

/* Writes all 6 DPI stages and switches the active one in a single report
 * -- this is how the real driver changes DPI stage, there's no separate
 * "just switch stage" command. To change only the active stage, query
 * AJAZZ_QUERY_DPI_TABLE, parse it with ajazz_parse_dpi_table(), and call
 * this back with the same dpi[] values and a new active_stage_0based. */
void ajazz_build_set_dpi_table(ajazz_report_t *r, const uint16_t dpi[AJAZZ_DPI_STAGES], uint8_t active_stage_0based);
void ajazz_build_set_report_rate(ajazz_report_t *r, uint8_t rate_index);
void ajazz_build_set_rgb_rainbow(ajazz_report_t *r, uint8_t brightness, uint8_t speed);

/* Constant/solid colour, no animation. payload = [0x03, (brightness<<4)|0x02,
 * 0xff], same 0-4 brightness scale as rainbow. */
void ajazz_build_set_rgb_constant(ajazz_report_t *r, uint8_t brightness);

void ajazz_build_set_rgb_off(ajazz_report_t *r);

/* AJAZZ_FAM_SENSOR (0x07 opcode 0x04) packs four unrelated settings into
 * one report -- isolated captures each showed exactly one byte changing:
 *
 *   payload[0] = auto-sleep timer, units of 10 seconds. Matches all 8
 *                values in the original driver's sleep dropdown
 *                (10s/30s/1m/2m/5m/10m/20m/30m -> 1/3/6/12/30/60/120/180).
 *   payload[1] = lift-off distance (0/1 -> 1mm/2mm) -- but a separate
 *                capture of the "wake mouse on motion" toggle also
 *                landed on this same byte. Not resolved whether that's
 *                really the same setting or two controls sharing a byte.
 *   payload[2] = "turn light off when idle" (0/1).
 *   payload[3] = 0x08, constant in every capture so far.
 *
 * Since all four share a report, writing one without reading the others
 * first silently resets them -- same hazard as the key table. Use
 * ajazz_build_sensor_from_query() plus the setters below rather than
 * building a report from scratch. */
void ajazz_build_sensor_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply);

/* Rounds `seconds` down to the nearest 10 and clamps to the payload
 * byte's range (0-2550s). The original driver's UI only goes up to 30
 * minutes. */
void ajazz_sensor_set_sleep_seconds(ajazz_report_t *r, uint16_t seconds);

/* May also read as "wake mouse on motion" on the device side -- see the
 * comment on ajazz_build_sensor_from_query() above. */
void ajazz_sensor_set_liftoff(ajazz_report_t *r, uint8_t level);

/* Turn the light off once the mouse has been idle -- 0/1. */
void ajazz_sensor_set_light_idle_off(ajazz_report_t *r, uint8_t enabled);

/* Resets the key table to factory defaults. Verified on real hardware:
 * a button custom-mapped away from its default went back to it after
 * this. Note the original Windows driver's own UI can look stale
 * afterward -- it reads its local SQLite database, not the device, so a
 * reset issued from outside that app won't show up there even though
 * the hardware really did change.
 *
 * Remapping to an arbitrary function isn't implemented -- only a
 * handful of example codes have been seen in captures, not a full
 * function-id-to-byte codebook. */
void ajazz_build_reset_keys(ajazz_report_t *r);

/* Turns a query 0x11 reply into a ready-to-edit SET report. Follow with
 * ajazz_key_slot_set_consumer() or friends to change one slot. */
void ajazz_build_key_table_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply);

/* Remaps button `slot` (0-based) to a USB HID Consumer Page usage code.
 * `r` must already hold a full key table (from
 * ajazz_build_key_table_from_query()) -- only this slot changes. */
void ajazz_key_slot_set_consumer(ajazz_report_t *r, int slot, uint16_t consumer_usage);

/* Remaps button `slot` to a keyboard key: `modifiers` is the usual
 * keyboard-report bitmask (bit0 LCtrl, bit1 LShift, bit2 LAlt, bit3
 * LGui, bit4-7 the right-hand versions), `keycode` a USB HID Keyboard
 * Page usage (e.g. 0x04 = 'A'). */
void ajazz_key_slot_set_keyboard(ajazz_report_t *r, int slot, uint8_t modifiers, uint8_t keycode);

/* Restores button `slot` to its native/default function. */
void ajazz_key_slot_reset_default(ajazz_report_t *r, int slot);

/* Rapid-fire/auto-click: repeats a click every `interval` (5-255) for
 * `number` repeats (0-255) while held. Structurally confirmed against
 * one capture (interval=5, number=1); exact behaviour at edge values
 * like number=0 isn't verified. */
void ajazz_key_slot_set_fire(ajazz_report_t *r, int slot, uint8_t interval, uint8_t number);

/*
 * Macros. One AJAZZ_FAM_MACRO SET report: mode 0x01, opcode = number of
 * payload bytes used (not a fixed id). payload[0] is always 0x03,
 * followed by up to AJAZZ_MACRO_MAX_STEPS four-byte steps:
 * [kind, delay_ms, 0x00, value].
 *   kind = AJAZZ_MACRO_MOUSE_DOWN/UP or AJAZZ_MACRO_KEY_DOWN/UP
 *   delay_ms = pause after this step, in ms
 *   value = mouse button bit flag (1/2/4/...) for mouse steps, or a USB
 *           HID Keyboard Page usage code for key steps
 *
 * Assigning that macro to a button is a second write to the key table:
 * the slot's marker becomes 0x90 with a constant trailing [0x23, 0x01].
 * That pair never varied across captures with different macros, so the
 * device plays back whichever macro content was written most recently
 * for that button rather than looking one up by id. Write the macro
 * content before assigning it.
 */
typedef enum {
    AJAZZ_MACRO_MOUSE_DOWN = 0x10,
    AJAZZ_MACRO_MOUSE_UP   = 0x90,
    AJAZZ_MACRO_KEY_DOWN   = 0x30,
    AJAZZ_MACRO_KEY_UP     = 0xb0,
} ajazz_macro_step_kind_t;

typedef struct {
    uint8_t kind;      /* an ajazz_macro_step_kind_t value */
    uint8_t delay_ms;  /* pause after this step, in milliseconds */
    uint8_t value;     /* mouse button bit, or USB HID keyboard usage code */
} ajazz_macro_step_t;

/* Builds the macro-content SET report for up to AJAZZ_MACRO_MAX_STEPS
 * steps. Extra steps are silently dropped -- check the count yourself
 * if you want to report that clearly. */
void ajazz_build_macro_content(ajazz_report_t *r, const ajazz_macro_step_t *steps, int n_steps);

/* Same idea for up to AJAZZ_MACRO_MAX_CHUNKED_STEPS steps: one report if
 * it fits, chunked writes otherwise (experimental, see the comment on
 * AJAZZ_MACRO_MAX_CHUNKED_STEPS). `out` needs room for at least
 * (AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + 1 + AJAZZ_PAYLOAD_LEN - 1) /
 * AJAZZ_PAYLOAD_LEN reports. Send them in order. Returns the report
 * count, or -1 if n_steps is over the limit. */
int ajazz_build_macro_content_chunked(ajazz_report_t *out, int max_out, const ajazz_macro_step_t *steps, int n_steps);

/* Marks button `slot` (0-based) as macro-triggering. `r` must already
 * hold a full key table. Send the macro content first -- see above. */
void ajazz_key_slot_set_macro(ajazz_report_t *r, int slot);

/* Parses a DPI table payload (query 0x13 reply, or a family 0x03 report). */
void ajazz_parse_dpi_table(const ajazz_report_t *r, uint16_t dpi_out[AJAZZ_DPI_STAGES]);

/* 0-based active DPI stage, from a DPI table payload's high nibble.
 * There's no separate query for this. */
int ajazz_dpi_table_active_stage(const ajazz_report_t *r);

/* Inverse of ajazz_build_set_report_rate()'s interval table -- turns a
 * report-rate payload[0] value back into Hz. Returns 0 if it doesn't
 * match one of the four known intervals. */
int ajazz_report_rate_hz(uint8_t interval_ms);

/* AJAZZ_QUERY_LED_MODE reply decoding. `effect` is the reply's opcode
 * byte -- compare against AJAZZ_LED_OFF/CONSTANT/RAINBOW rather than
 * assuming those are the only values possible. All three confirmed live
 * by cycling through them and reading back; constant's brightness
 * matched at all 5 levels. Odd detail: AJAZZ_LED_OFF (0x01) doesn't
 * match the "off" SET command's own opcode (0x02) the way constant and
 * rainbow's do -- no explanation, that's just what the device sends.
 * `speed` only means anything for AJAZZ_LED_RAINBOW. Returns 0 for a
 * recognized effect, -1 otherwise (still fills in effect=r->opcode so
 * callers can show something without special-casing the error). */
typedef enum {
    AJAZZ_LED_OFF      = 0x01,
    AJAZZ_LED_CONSTANT = 0x05,
    AJAZZ_LED_RAINBOW  = 0x18,
} ajazz_led_effect_t;

typedef struct {
    uint8_t effect;
    uint8_t brightness; /* 0-4 */
    uint8_t speed;      /* 0-4, only valid when effect == AJAZZ_LED_RAINBOW */
} ajazz_led_state_t;

int ajazz_parse_led_mode(const ajazz_report_t *r, ajazz_led_state_t *out);

/* Query 0x20's payload[0] isn't a calibrated percentage -- the Windows
 * driver shows a 4-bar icon, not a number. Only one data point is
 * confirmed (raw 0x21/33 = all 4 bars), so the top tier is real and the
 * other three boundaries below it are an evenly-spaced guess. */
const char *ajazz_battery_label(uint8_t raw);

#endif
