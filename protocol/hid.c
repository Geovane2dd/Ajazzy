#include <string.h>
#include "hid.h"

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

void ajazz_build_set_dpi_table(ajazz_report_t *r, const uint16_t dpi[AJAZZ_DPI_STAGES], uint8_t active_stage_0based)
{
    ajazz_init(r, AJAZZ_FAM_DPI_TABLE, 0x01, 0x25);

    /* high nibble = active stage (0-based); low nibble is a constant
     * AJAZZ_DPI_STAGES - 1 */
    r->payload[0] = (uint8_t)((active_stage_0based << 4) | (AJAZZ_DPI_STAGES - 1));

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

void ajazz_build_set_report_rate(ajazz_report_t *r, uint8_t rate_index)
{
    ajazz_init(r, AJAZZ_FAM_REPORT_RATE, 0x01, 0x01);

    /* polling interval in milliseconds: 125/250/500/1000 Hz -> 8/4/2/1 ms */
    static const uint8_t interval_ms[4] = {8, 4, 2, 1};
    r->payload[0] = (rate_index < 4) ? interval_ms[rate_index] : 1;

    ajazz_finalize(r);
}

void ajazz_build_set_rgb_rainbow(ajazz_report_t *r, uint8_t brightness, uint8_t speed)
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x18);
    r->payload[0] = 0x02; /* rainbow/colorful cycle mode */
    r->payload[1] = (uint8_t)((brightness << 4) | (speed & 0x0F));
    r->payload[2] = 0x07; /* 7 colours in the palette below */

    static const uint8_t palette[7][3] = {
        {0xff, 0x00, 0x00}, /* red     */
        {0x00, 0xff, 0x00}, /* green   */
        {0x00, 0x00, 0xff}, /* blue    */
        {0x00, 0xff, 0xff}, /* cyan    */
        {0xff, 0xff, 0x00}, /* yellow  */
        {0xff, 0x00, 0xff}, /* magenta */
        {0xff, 0xff, 0xff}, /* white   */
    };
    for (int i = 0; i < 7; i++) {
        r->payload[3 + i * 3 + 0] = palette[i][0];
        r->payload[3 + i * 3 + 1] = palette[i][1];
        r->payload[3 + i * 3 + 2] = palette[i][2];
    }

    ajazz_finalize(r);
}

void ajazz_build_set_rgb_constant(ajazz_report_t *r, uint8_t brightness)
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x05);
    r->payload[0] = 0x03;
    r->payload[1] = (uint8_t)((brightness << 4) | 0x02);
    r->payload[2] = 0xff;
    ajazz_finalize(r);
}

void ajazz_build_set_rgb_off(ajazz_report_t *r)
{
    ajazz_init(r, AJAZZ_FAM_MISC, 0x01, 0x02);
    ajazz_finalize(r);
}

void ajazz_build_sensor_from_query(ajazz_report_t *out, const ajazz_report_t *query_reply)
{
    *out = *query_reply;
    out->family = AJAZZ_FAM_SENSOR;
    out->reserved0 = 0x00;
    out->mode = 0x01;
    out->opcode = 0x04;
    ajazz_finalize(out);
}

void ajazz_sensor_set_sleep_seconds(ajazz_report_t *r, uint16_t seconds)
{
    uint16_t tenths = seconds / 10;
    r->payload[0] = (uint8_t)(tenths > 0xFF ? 0xFF : tenths);
    ajazz_finalize(r);
}

void ajazz_sensor_set_liftoff(ajazz_report_t *r, uint8_t level)
{
    r->payload[1] = level;
    ajazz_finalize(r);
}

void ajazz_sensor_set_light_idle_off(ajazz_report_t *r, uint8_t enabled)
{
    r->payload[2] = enabled;
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

void ajazz_key_slot_set_consumer(ajazz_report_t *r, int slot, uint16_t consumer_usage)
{
    int off = slot * 3;
    r->payload[off + 0] = 0x80;
    r->payload[off + 1] = (uint8_t)(consumer_usage & 0xFF);
    r->payload[off + 2] = (uint8_t)(consumer_usage >> 8);
    ajazz_finalize(r);
}

void ajazz_key_slot_set_keyboard(ajazz_report_t *r, int slot, uint8_t modifiers, uint8_t keycode)
{
    int off = slot * 3;
    r->payload[off + 0] = 0x70;
    r->payload[off + 1] = modifiers;
    r->payload[off + 2] = keycode;
    ajazz_finalize(r);
}

void ajazz_key_slot_set_fire(ajazz_report_t *r, int slot, uint8_t interval, uint8_t number)
{
    int off = slot * 3;
    r->payload[off + 0] = 0x30;
    r->payload[off + 1] = interval;
    r->payload[off + 2] = number;
    ajazz_finalize(r);
}

void ajazz_key_slot_reset_default(ajazz_report_t *r, int slot)
{
    int off = slot * 3;
    r->payload[off + 0] = 0x10;
    r->payload[off + 1] = (uint8_t)(1u << slot);
    r->payload[off + 2] = 0x00;
    ajazz_finalize(r);
}

void ajazz_build_macro_content(ajazz_report_t *r, const ajazz_macro_step_t *steps, int n_steps)
{
    ajazz_init(r, AJAZZ_FAM_MACRO, 0x01, 0x00);

    r->payload[0] = 0x03;
    int len = 1;
    for (int i = 0; i < n_steps && i < AJAZZ_MACRO_MAX_STEPS; i++) {
        r->payload[len + 0] = steps[i].kind;
        r->payload[len + 1] = steps[i].delay_ms;
        r->payload[len + 2] = 0x00;
        r->payload[len + 3] = steps[i].value;
        len += 4;
    }
    r->opcode = (uint8_t)len;

    ajazz_finalize(r);
}

int ajazz_build_macro_content_chunked(ajazz_report_t *out, int max_out, const ajazz_macro_step_t *steps, int n_steps)
{
    if (n_steps > AJAZZ_MACRO_MAX_CHUNKED_STEPS)
        return -1;

    if (n_steps <= AJAZZ_MACRO_MAX_STEPS) {
        if (max_out < 1) return -1;
        ajazz_build_macro_content(&out[0], steps, n_steps);
        return 1;
    }

    uint8_t stream[1 + AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4];
    int len = 0;
    stream[len++] = 0x03;
    for (int i = 0; i < n_steps; i++) {
        stream[len++] = steps[i].kind;
        stream[len++] = steps[i].delay_ms;
        stream[len++] = 0x00;
        stream[len++] = steps[i].value;
    }

    int n_chunks = 0;
    int off = 0;
    while (off < len) {
        if (n_chunks >= max_out)
            return -1;

        int chunk_len = len - off;
        if (chunk_len > AJAZZ_PAYLOAD_LEN)
            chunk_len = AJAZZ_PAYLOAD_LEN;

        ajazz_report_t *r = &out[n_chunks];
        memset(r, 0, sizeof(*r));
        r->family = AJAZZ_FAM_MACRO;
        r->reserved0 = 0x00;
        r->mode = (uint8_t)((n_chunks << 4) | 0x02);
        r->opcode = (uint8_t)chunk_len;
        memcpy(r->payload, stream + off, (size_t)chunk_len);
        ajazz_finalize(r);

        off += chunk_len;
        n_chunks++;
    }

    return n_chunks;
}

void ajazz_key_slot_set_macro(ajazz_report_t *r, int slot)
{
    int off = slot * 3;
    r->payload[off + 0] = 0x90;
    r->payload[off + 1] = 0x23;
    r->payload[off + 2] = 0x01;
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

int ajazz_parse_led_mode(const ajazz_report_t *r, ajazz_led_state_t *out)
{
    out->effect = r->opcode;
    out->brightness = 0;
    out->speed = 0;

    switch (r->opcode) {
        case AJAZZ_LED_OFF:
            return 0;
        case AJAZZ_LED_CONSTANT:
            out->brightness = r->payload[1] >> 4;
            return 0;
        case AJAZZ_LED_RAINBOW:
            out->brightness = r->payload[1] >> 4;
            out->speed = r->payload[1] & 0x0F;
            return 0;
        default:
            return -1;
    }
}

const char *ajazz_battery_label(uint8_t raw)
{
    if (raw >= 25) return "Cheia (4/4 barras, estimado)";
    if (raw >= 17) return "Boa (3/4 barras, estimado)";
    if (raw >= 8)  return "Media (2/4 barras, estimado)";
    if (raw >= 1)  return "Baixa (1/4 barras, estimado)";
    return "Vazia/critica (estimado)";
}
