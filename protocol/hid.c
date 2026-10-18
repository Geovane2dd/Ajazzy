#include <string.h>
#include "hid.h"

const uint16_t AJAZZ_DEFAULT_DPI[AJAZZ_DPI_STAGES] = {400, 800, 1200, 1600, 2400, 3200};
const uint8_t AJAZZ_DEFAULT_DPI_COLOR[AJAZZ_DPI_STAGES][3] = {
    {0xff, 0x00, 0x00}, /* red    */
    {0x00, 0xff, 0x00}, /* green  */
    {0x00, 0x00, 0xff}, /* blue   */
    {0x00, 0xff, 0xff}, /* cyan   */
    {0xff, 0xff, 0x00}, /* yellow */
    {0x80, 0x00, 0x80}, /* purple */
};

void ajazz_finalize(ajazz_report_t *r)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < AJAZZ_PAYLOAD_LEN; i++)
        sum += r->payload[i];
    r->checksum = (uint8_t)(sum & 0xFF);
}

static void ajazz_init(ajazz_report_t *r, uint8_t family, uint8_t mode, uint8_t opcode)
{
    memset(r, 0, sizeof(*r));
    r->family = family;
    r->reserved0 = 0x00;
    r->mode = mode;
    r->opcode = opcode;
}

void ajazz_build_query(ajazz_report_t *r, uint8_t query_id)
{
    ajazz_init(r, query_id, 0x00, 0x00);
    ajazz_finalize(r);
}

void ajazz_build_set_dpi_table(ajazz_report_t *r, const uint16_t dpi[AJAZZ_DPI_STAGES], uint8_t active_stage_0based,
    uint8_t count_nibble)
{
    ajazz_init(r, AJAZZ_FAM_DPI_TABLE, 0x01, 0x25);

    /* high nibble = active stage (0-based); low nibble = the driver's
     * "dpi_count" (decompiled: (dpi_index << 4) | dpi_count) */
    r->payload[0] = (uint8_t)((active_stage_0based << 4) | (count_nibble & 0x0F));

    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        uint16_t raw = dpi[i] / 100; /* device stores dpi/100 */
        int off = 1 + i * 4;
        r->payload[off + 0] = (uint8_t)(raw & 0xFF);
        r->payload[off + 1] = (uint8_t)(raw >> 8);
        r->payload[off + 2] = (uint8_t)(raw & 0xFF); /* Y axis mirrors X */
        r->payload[off + 3] = (uint8_t)(raw >> 8);
    }

    ajazz_finalize(r);
}

void ajazz_build_set_dpi_colors(ajazz_report_t *r, const uint8_t colors[AJAZZ_DPI_STAGES][3])
{
    ajazz_init(r, AJAZZ_FAM_DPI_COLOR, 0x01, AJAZZ_DPI_STAGES * 3);

    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        r->payload[i * 3 + 0] = colors[i][0]; /* R */
        r->payload[i * 3 + 1] = colors[i][1]; /* G */
        r->payload[i * 3 + 2] = colors[i][2]; /* B */
    }

    ajazz_finalize(r);
}

void ajazz_parse_dpi_color_table(const ajazz_report_t *r, uint8_t colors[AJAZZ_DPI_STAGES][3])
{
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        colors[i][0] = r->payload[i * 3 + 0];
        colors[i][1] = r->payload[i * 3 + 1];
        colors[i][2] = r->payload[i * 3 + 2];
    }
}

void ajazz_build_set_report_rate(ajazz_report_t *r, uint8_t rate_index)
{
    ajazz_init(r, AJAZZ_FAM_REPORT_RATE, 0x01, 0x01);

    /* polling interval in milliseconds: 125/250/500/1000 Hz -> 8/4/2/1 ms */
    static const uint8_t interval_ms[4] = {8, 4, 2, 1};
    r->payload[0] = (rate_index < 4) ? interval_ms[rate_index] : 1;

    ajazz_finalize(r);
}

const uint8_t AJAZZ_LIGHT_DEFAULT_PALETTE[AJAZZ_LIGHT_PALETTE][3] = {
    {0xff, 0x00, 0x00}, /* red     */
    {0x00, 0xff, 0x00}, /* green   */
    {0x00, 0x00, 0xff}, /* blue    */
    {0x00, 0xff, 0xff}, /* cyan    */
    {0xff, 0xff, 0x00}, /* yellow  */
    {0xff, 0x00, 0xff}, /* magenta */
    {0xff, 0xff, 0xff}, /* white   */
};

static uint8_t light_bs(uint8_t brightness, uint8_t speed)
{
    if (brightness > 4) brightness = 4;
    if (speed > 4) speed = 4;
    return (uint8_t)((brightness << 4) | speed);
}

void ajazz_build_set_light_simple(ajazz_report_t *r, uint8_t mode, uint8_t brightness, uint8_t speed)
{
    /* mode 1 uses opcode 3, modes 0/4/5 opcode 2 (see hid.h) */
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, mode == AJAZZ_LIGHT_FLOW ? 0x03 : 0x02);
    r->payload[0] = mode;
    r->payload[1] = light_bs(brightness, speed);
    ajazz_finalize(r);
}

void ajazz_build_set_light_steady(ajazz_report_t *r, uint8_t brightness, uint8_t speed, const uint8_t rgb[3])
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x05);
    r->payload[0] = AJAZZ_LIGHT_STEADY;
    r->payload[1] = light_bs(brightness, speed);
    r->payload[2] = rgb[0];
    r->payload[3] = rgb[1];
    r->payload[4] = rgb[2];
    ajazz_finalize(r);
}

void ajazz_build_set_light_breathing(ajazz_report_t *r, uint8_t brightness, uint8_t speed,
    const uint8_t palette[AJAZZ_LIGHT_PALETTE][3])
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x18);
    r->payload[0] = AJAZZ_LIGHT_BREATHING;
    r->payload[1] = light_bs(brightness, speed);
    r->payload[2] = AJAZZ_LIGHT_PALETTE;
    for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++) {
        r->payload[3 + i * 3 + 0] = palette[i][0];
        r->payload[3 + i * 3 + 1] = palette[i][1];
        r->payload[3 + i * 3 + 2] = palette[i][2];
    }
    ajazz_finalize(r);
}

void ajazz_build_set_rgb_off(ajazz_report_t *r)
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x02);
    ajazz_finalize(r);
}

int ajazz_parse_light(const ajazz_report_t *r, ajazz_light_state_t *out)
{
    memset(out, 0, sizeof(*out));
    /* "off" reads back with opcode 0x01 and an empty payload */
    if (r->opcode == 0x01 || (r->opcode == 0x02 && r->payload[0] == AJAZZ_LIGHT_OFF)) {
        out->mode = AJAZZ_LIGHT_OFF;
        return 0;
    }
    uint8_t mode = r->payload[0];
    if (mode < AJAZZ_LIGHT_FLOW || mode > AJAZZ_LIGHT_WAVE)
        return -1;
    out->mode = mode;
    out->brightness = r->payload[1] >> 4;
    out->speed = r->payload[1] & 0x0F;
    if (mode == AJAZZ_LIGHT_STEADY) {
        memcpy(out->rgb, &r->payload[2], 3);
    } else if (mode == AJAZZ_LIGHT_BREATHING) {
        int n = r->payload[2];
        if (n > AJAZZ_LIGHT_PALETTE) n = AJAZZ_LIGHT_PALETTE;
        for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++) {
            const uint8_t *src = &r->payload[3 + (i < n ? i : (n ? n - 1 : 0)) * 3];
            memcpy(out->palette[i], src, 3);
        }
    }
    return 0;
}

void ajazz_build_power_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply)
{
    *out = *query_reply;
    out->family = AJAZZ_FAM_SENSOR;
    out->reserved0 = 0x00;
    out->mode = 0x01;
    out->opcode = 0x04;
    ajazz_finalize(out);
}

void ajazz_power_set_sleep_seconds(ajazz_report_t *r, uint16_t seconds)
{
    uint16_t tenths = seconds / 10;
    r->payload[0] = (uint8_t)(tenths > 0xFF ? 0xFF : tenths);
    ajazz_finalize(r);
}

void ajazz_power_set_wake_on_motion(ajazz_report_t *r, uint8_t enabled)
{
    r->payload[1] = enabled ? 1 : 0;
    ajazz_finalize(r);
}

void ajazz_power_set_light_idle_off(ajazz_report_t *r, uint8_t enabled)
{
    r->payload[2] = enabled ? 1 : 0;
    ajazz_finalize(r);
}

void ajazz_power_set_debounce_ms(ajazz_report_t *r, uint8_t ms)
{
    r->payload[3] = ms;
    ajazz_finalize(r);
}

void ajazz_parse_sensor_perf(const ajazz_report_t *r, ajazz_sensor_t *out)
{
    out->lift_off = r->payload[0] >= 2 ? 2 : 1;
    out->angle_snap = r->payload[1] ? 1 : 0;
    out->ripple_control = r->payload[2] ? 1 : 0;
    out->motion_sync = r->payload[3] ? 1 : 0;
}

void ajazz_build_set_sensor_perf(ajazz_report_t *r, const ajazz_sensor_t *s)
{
    ajazz_init(r, AJAZZ_FAM_SENSOR_PERF, 0x01, 0x05);
    r->payload[0] = s->lift_off >= 2 ? 2 : 1;
    r->payload[1] = s->angle_snap ? 1 : 0;
    r->payload[2] = s->ripple_control ? 1 : 0;
    r->payload[3] = s->motion_sync ? 1 : 0;
    ajazz_finalize(r);
}

void ajazz_build_factory_reset(ajazz_report_t *r)
{
    ajazz_init(r, AJAZZ_FAM_FACTORY_RESET, 0x01, 0x01);
    r->payload[0] = 0xff;
    ajazz_finalize(r);
}

void ajazz_build_reset_keys(ajazz_report_t *r)
{
    ajazz_init(r, AJAZZ_FAM_KEY_TABLE, 0x01, 0x0f);

    static const uint8_t defaults[] = {
        0x10, 0x01, 0x00,
        0x10, 0x02, 0x00,
        0x10, 0x04, 0x00,
        0x10, 0x08, 0x00,
        0x10, 0x10, 0x00,
        0x40, 0x01, 0x00,
    };
    memcpy(r->payload, defaults, sizeof(defaults));

    ajazz_finalize(r);
}

void ajazz_build_key_table_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply)
{
    *out = *query_reply;
    out->family = AJAZZ_FAM_KEY_TABLE;
    out->reserved0 = 0x00;
    out->mode = 0x01;
    out->opcode = 0x0f;
    ajazz_finalize(out);
}

/* Every ajazz_key_slot_set_*()/reset_default() below writes 3 bytes at
 * payload[slot*3] -- with AJAZZ_KEY_SLOTS callers this stays well
 * inside the 27-byte payload, but nothing enforced that if a bad slot
 * ever reached here (a typo'd loop bound, a future caller), so bounds-
 * check it explicitly rather than trust every call site forever. */
static int key_slot_offset(int slot)
{
    if (slot < 0 || slot >= AJAZZ_KEY_SLOTS)
        return -1;
    return slot * 3;
}

void ajazz_key_slot_set_consumer(ajazz_report_t *r, int slot, uint16_t consumer_usage)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    r->payload[off + 0] = 0x80;
    r->payload[off + 1] = (uint8_t)(consumer_usage & 0xFF);
    r->payload[off + 2] = (uint8_t)(consumer_usage >> 8);
    ajazz_finalize(r);
}

void ajazz_key_slot_set_keyboard(ajazz_report_t *r, int slot, uint8_t modifiers, uint8_t keycode)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    r->payload[off + 0] = 0x70;
    r->payload[off + 1] = modifiers;
    r->payload[off + 2] = keycode;
    ajazz_finalize(r);
}

void ajazz_key_slot_set_fire(ajazz_report_t *r, int slot, uint8_t interval, uint8_t number)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    r->payload[off + 0] = 0x30;
    r->payload[off + 1] = interval;
    r->payload[off + 2] = number;
    ajazz_finalize(r);
}

void ajazz_key_slot_reset_default(ajazz_report_t *r, int slot)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    if (slot == 5) { /* the DPI button */
        r->payload[off + 0] = 0x40;
        r->payload[off + 1] = 0x01;
        r->payload[off + 2] = 0x00;
    } else {
        r->payload[off + 0] = 0x10;
        r->payload[off + 1] = (uint8_t)(1u << slot);
        r->payload[off + 2] = 0x00;
    }
    ajazz_finalize(r);
}

static void slot_put(ajazz_report_t *r, int slot, uint8_t a, uint8_t b, uint8_t c)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    r->payload[off + 0] = a;
    r->payload[off + 1] = b;
    r->payload[off + 2] = c;
    ajazz_finalize(r);
}

void ajazz_key_slot_set_function(ajazz_report_t *r, int slot, ajazz_button_fn_t fn)
{
    switch (fn) {
        case AJAZZ_BTN_LEFT:         slot_put(r, slot, 0x10, 0x01, 0x00); break;
        case AJAZZ_BTN_MIDDLE:       slot_put(r, slot, 0x10, 0x04, 0x00); break;
        case AJAZZ_BTN_RIGHT:        slot_put(r, slot, 0x10, 0x02, 0x00); break;
        case AJAZZ_BTN_BACK:         slot_put(r, slot, 0x10, 0x08, 0x00); break;
        case AJAZZ_BTN_FORWARD:      slot_put(r, slot, 0x10, 0x10, 0x00); break;
        case AJAZZ_BTN_DOUBLE_CLICK: slot_put(r, slot, 0x30, 0x32, 0x03); break;
        case AJAZZ_BTN_SCROLL_UP:    slot_put(r, slot, 0x20, 0x01, 0x00); break;
        case AJAZZ_BTN_SCROLL_DOWN:  slot_put(r, slot, 0x20, 0xff, 0x00); break;
        case AJAZZ_BTN_SCROLL_LEFT:  slot_put(r, slot, 0x10, 0x00, 0xf5); break;
        case AJAZZ_BTN_SCROLL_RIGHT: slot_put(r, slot, 0x10, 0x00, 0xf6); break;
        case AJAZZ_BTN_DPI_LOOP:     slot_put(r, slot, 0x40, 0x01, 0x00); break;
        case AJAZZ_BTN_DPI_UP:       slot_put(r, slot, 0x40, 0x02, 0x00); break;
        case AJAZZ_BTN_DPI_DOWN:     slot_put(r, slot, 0x40, 0x03, 0x00); break;
        case AJAZZ_BTN_DISABLED:     slot_put(r, slot, 0x60, 0x00, 0x00); break;
        default: break;
    }
}

void ajazz_key_slot_set_system(ajazz_report_t *r, int slot, ajazz_system_fn_t fn)
{
    switch (fn) {
        case AJAZZ_SYS_BRIGHTNESS_UP:   slot_put(r, slot, 0x80, 0x6f, 0x00); break;
        case AJAZZ_SYS_BRIGHTNESS_DOWN: slot_put(r, slot, 0x80, 0x70, 0x00); break;
        case AJAZZ_SYS_CALCULATOR:      slot_put(r, slot, 0x80, 0x92, 0x01); break;
        case AJAZZ_SYS_MY_COMPUTER:     slot_put(r, slot, 0x80, 0x94, 0x01); break;
        case AJAZZ_SYS_WEBSITE:         slot_put(r, slot, 0x80, 0x23, 0x02); break;
        case AJAZZ_SYS_MAIL:            slot_put(r, slot, 0x80, 0x8a, 0x01); break;
        case AJAZZ_SYS_SHOW_DESKTOP:    slot_put(r, slot, 0x70, 0x08, 0x07); break;
        case AJAZZ_SYS_SWITCH_APP:      slot_put(r, slot, 0x70, 0x08, 0x2b); break;
        case AJAZZ_SYS_COPY:            slot_put(r, slot, 0x70, 0x01, 0x06); break;
        case AJAZZ_SYS_PASTE:           slot_put(r, slot, 0x70, 0x01, 0x19); break;
        case AJAZZ_SYS_CUT:             slot_put(r, slot, 0x70, 0x01, 0x1b); break;
        case AJAZZ_SYS_MEDIA_PLAYER:    slot_put(r, slot, 0x80, 0x83, 0x01); break;
        default: break;
    }
}

void ajazz_build_macro_content(ajazz_report_t *r, uint8_t key_index, const ajazz_macro_step_t *steps, int n_steps)
{
    ajazz_init(r, AJAZZ_FAM_MACRO, 0x01, 0x00);

    r->payload[0] = key_index;
    int len = 1;
    for (int i = 0; i < n_steps && i < AJAZZ_MACRO_MAX_STEPS; i++) {
        r->payload[len + 0] = steps[i].kind;
        r->payload[len + 1] = (uint8_t)(steps[i].delay_ms & 0xFF);
        r->payload[len + 2] = (uint8_t)(steps[i].delay_ms >> 8);
        r->payload[len + 3] = steps[i].value;
        len += 4;
    }
    r->opcode = (uint8_t)len;

    ajazz_finalize(r);
}

int ajazz_build_macro_content_chunked(ajazz_report_t *out, int max_out, uint8_t key_index, const ajazz_macro_step_t *steps, int n_steps)
{
    if (n_steps > AJAZZ_MACRO_MAX_CHUNKED_STEPS)
        return -1;

    if (n_steps <= AJAZZ_MACRO_MAX_STEPS) {
        if (max_out < 1) return -1;
        ajazz_build_macro_content(&out[0], key_index, steps, n_steps);
        return 1;
    }

    uint8_t stream[1 + AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4];
    int len = 0;
    stream[len++] = key_index;
    for (int i = 0; i < n_steps; i++) {
        stream[len++] = steps[i].kind;
        stream[len++] = (uint8_t)(steps[i].delay_ms & 0xFF);
        stream[len++] = (uint8_t)(steps[i].delay_ms >> 8);
        stream[len++] = steps[i].value;
    }

    /* total_chunks must be known up front -- it goes in every chunk's
     * mode byte, not just the last one. */
    int total_chunks = (len + AJAZZ_PAYLOAD_LEN - 1) / AJAZZ_PAYLOAD_LEN;
    if (total_chunks > AJAZZ_MACRO_MAX_CHUNKS || total_chunks > max_out)
        return -1;

    int n_chunks = 0;
    int off = 0;
    while (off < len) {
        int chunk_len = len - off;
        if (chunk_len > AJAZZ_PAYLOAD_LEN)
            chunk_len = AJAZZ_PAYLOAD_LEN;

        ajazz_report_t *r = &out[n_chunks];
        memset(r, 0, sizeof(*r));
        r->family = AJAZZ_FAM_MACRO;
        r->reserved0 = 0x00;
        r->mode = (uint8_t)((n_chunks << 4) | total_chunks);
        r->opcode = (uint8_t)chunk_len;
        memcpy(r->payload, stream + off, (size_t)chunk_len);
        ajazz_finalize(r);

        off += chunk_len;
        n_chunks++;
    }

    return n_chunks;
}

void ajazz_key_slot_set_macro(ajazz_report_t *r, int slot, ajazz_macro_loop_t loop, uint8_t loop_count)
{
    int off = key_slot_offset(slot);
    if (off < 0) return;
    uint8_t b1 = (uint8_t)slot;
    uint8_t b2 = 0;
    switch (loop) {
        case AJAZZ_MACRO_WHILE_HELD:  b1 |= 0x10; break;
        case AJAZZ_MACRO_UNTIL_CLICK: b1 |= 0x40; break;
        case AJAZZ_MACRO_N_TIMES:
            if (loop_count > 0) { b1 |= 0x20; b2 = loop_count; }
            break;
        default: break;
    }
    r->payload[off + 0] = 0x90;
    r->payload[off + 1] = b1;
    r->payload[off + 2] = b2;
    ajazz_finalize(r);
}

void ajazz_parse_dpi_table(const ajazz_report_t *r, uint16_t dpi_out[AJAZZ_DPI_STAGES])
{
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        int off = 1 + i * 4;
        uint16_t raw = (uint16_t)(r->payload[off] | (r->payload[off + 1] << 8));
        dpi_out[i] = (uint16_t)(raw * 100);
    }
}

int ajazz_dpi_table_active_stage(const ajazz_report_t *r)
{
    return r->payload[0] >> 4;
}

int ajazz_dpi_table_count(const ajazz_report_t *r)
{
    return r->payload[0] & 0x0F;
}

int ajazz_report_rate_hz(uint8_t interval_ms)
{
    switch (interval_ms) {
        case 8: return 125;
        case 4: return 250;
        case 2: return 500;
        case 1: return 1000;
        default: return 0;
    }
}

const char *ajazz_battery_label(uint8_t raw)
{
    if (raw >= 75) return "Full (4/4 bars)";
    if (raw >= 50) return "Good (3/4 bars)";
    if (raw >= 25) return "Medium (2/4 bars)";
    return "Low (1/4 bars)";
}
