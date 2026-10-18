/*
 * Wire-format tests for protocol/hid.c. No hardware needed: the "replies"
 * below were captured from a real AJ179 V2 MAX (dev_id N625) and the
 * expected request bytes come from the official driver's decompiled
 * builders, so these lock the encoding against regressions.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../protocol/hid.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static ajazz_report_t from_hex(const char *hex)
{
    ajazz_report_t r;
    memset(&r, 0, sizeof(r));
    uint8_t *p = (uint8_t *)&r;
    for (int i = 0; i < AJAZZ_REPORT_LEN && *hex; i++) {
        p[i] = (uint8_t)strtoul((char[3]){hex[0], hex[1], 0}, NULL, 16);
        hex += 2;
        while (*hex == ' ') hex++;
    }
    return r;
}

static void test_checksum(void)
{
    ajazz_report_t r;
    ajazz_build_set_report_rate(&r, 3);
    CHECK(r.family == 0x02 && r.mode == 0x01 && r.opcode == 0x01);
    CHECK(r.payload[0] == 1);
    CHECK(r.checksum == 1);
}

static void test_dpi(void)
{
    /* real 0x13 reply: active stage 2, count nibble 5 */
    ajazz_report_t rep = from_hex("13 00 01 19 25 04 00 04 00 08 00 08 00 0c 00 0c 00 10 00 10 00 18 00 18 00 20 00 20 00 00 00 e5");
    uint16_t dpi[AJAZZ_DPI_STAGES];
    ajazz_parse_dpi_table(&rep, dpi);
    CHECK(dpi[0] == 400 && dpi[1] == 800 && dpi[2] == 1200 && dpi[5] == 3200);
    CHECK(ajazz_dpi_table_active_stage(&rep) == 2);
    CHECK(ajazz_dpi_table_count(&rep) == 5);

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, dpi, 2, (uint8_t)ajazz_dpi_table_count(&rep));
    CHECK(req.family == 0x03 && req.opcode == 0x25 && req.payload[0] == 0x25);
    CHECK(memcmp(req.payload, rep.payload, 25) == 0);
}

static void test_light(void)
{
    ajazz_light_state_t st;

    /* steady red, brightness 4, speed 2 -- read back from the mouse */
    ajazz_report_t rep = from_hex("15 00 01 05 03 42 ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 44");
    CHECK(ajazz_parse_light(&rep, &st) == 0);
    CHECK(st.mode == AJAZZ_LIGHT_STEADY && st.brightness == 4 && st.speed == 2);
    CHECK(st.rgb[0] == 0xff && st.rgb[1] == 0 && st.rgb[2] == 0);

    const uint8_t red[3] = {0xff, 0, 0};
    ajazz_report_t req;
    ajazz_build_set_light_steady(&req, 4, 2, red);
    CHECK(req.family == 0x05 && req.opcode == 0x05);
    CHECK(memcmp(req.payload, rep.payload, 5) == 0);

    /* breathing: 7 identical purples, as read back after setting them */
    ajazz_report_t br = from_hex("15 00 01 18 02 42 07 a0 20 f0 a0 20 f0 a0 20 f0 a0 20 f0 a0 20 f0 a0 20 f0 a0 20 f0 00 00 00");
    CHECK(ajazz_parse_light(&br, &st) == 0);
    CHECK(st.mode == AJAZZ_LIGHT_BREATHING && st.palette[6][0] == 0xa0 && st.palette[3][2] == 0xf0);

    uint8_t pal[AJAZZ_LIGHT_PALETTE][3];
    for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++) { pal[i][0] = 0xa0; pal[i][1] = 0x20; pal[i][2] = 0xf0; }
    ajazz_build_set_light_breathing(&req, 4, 2, pal);
    CHECK(req.opcode == 0x18 && req.payload[0] == 2 && req.payload[2] == 7);
    CHECK(memcmp(req.payload, br.payload, 3 + 21) == 0);

    /* simple modes: opcode 3 for "flow", 2 for off / neon / wave */
    ajazz_build_set_light_simple(&req, AJAZZ_LIGHT_FLOW, 3, 1);
    CHECK(req.opcode == 0x03 && req.payload[0] == 1 && req.payload[1] == 0x31);
    ajazz_build_set_light_simple(&req, AJAZZ_LIGHT_NEON, 4, 2);
    CHECK(req.opcode == 0x02 && req.payload[0] == 4 && req.payload[1] == 0x42);
    ajazz_build_set_light_simple(&req, AJAZZ_LIGHT_WAVE, 4, 2);
    CHECK(req.opcode == 0x02 && req.payload[0] == 5);

    ajazz_report_t off = from_hex("15 00 01 01 00 00");
    CHECK(ajazz_parse_light(&off, &st) == 0 && st.mode == AJAZZ_LIGHT_OFF);
    ajazz_report_t flow = from_hex("15 00 01 03 01 42 01");
    CHECK(ajazz_parse_light(&flow, &st) == 0 && st.mode == AJAZZ_LIGHT_FLOW);
}

static void test_sensor_power(void)
{
    /* real 0x16 reply: lift-off 2 mm, no angle snap/ripple, motion sync on */
    ajazz_report_t rep = from_hex("16 00 01 05 02 00 00 01");
    ajazz_sensor_t s;
    ajazz_parse_sensor_perf(&rep, &s);
    CHECK(s.lift_off == 2 && !s.angle_snap && !s.ripple_control && s.motion_sync);

    s.angle_snap = 1;
    ajazz_report_t req;
    ajazz_build_set_sensor_perf(&req, &s);
    CHECK(req.family == 0x06 && req.opcode == 0x05);
    CHECK(req.payload[0] == 2 && req.payload[1] == 1 && req.payload[2] == 0 && req.payload[3] == 1);

    /* real 0x17 reply: 300 s sleep, debounce 8 ms */
    ajazz_report_t pw = from_hex("17 00 01 05 1e 02 00 08");
    ajazz_build_power_from_query(&req, &pw);
    CHECK(req.family == 0x07 && req.opcode == 0x04);
    ajazz_power_set_debounce_ms(&req, 12);
    ajazz_power_set_wake_on_motion(&req, 1);
    CHECK(req.payload[0] == 0x1e && req.payload[1] == 1 && req.payload[3] == 12);
    ajazz_power_set_sleep_seconds(&req, 600);
    CHECK(req.payload[0] == 60);
}

static void test_keys(void)
{
    ajazz_report_t t;
    ajazz_build_reset_keys(&t);
    ajazz_key_slot_set_function(&t, 3, AJAZZ_BTN_DISABLED);
    CHECK(t.payload[9] == 0x60 && t.payload[10] == 0 && t.payload[11] == 0);
    ajazz_key_slot_reset_default(&t, 3);
    CHECK(t.payload[9] == 0x10 && t.payload[10] == 0x08);
    ajazz_key_slot_reset_default(&t, 5);
    CHECK(t.payload[15] == 0x40 && t.payload[16] == 0x01);
    ajazz_key_slot_set_system(&t, 4, AJAZZ_SYS_COPY);
    CHECK(t.payload[12] == 0x70 && t.payload[13] == 0x01 && t.payload[14] == 0x06);
    ajazz_key_slot_set_system(&t, 4, AJAZZ_SYS_CALCULATOR);
    CHECK(t.payload[12] == 0x80 && t.payload[13] == 0x92 && t.payload[14] == 0x01);
}

static void test_macro(void)
{
    ajazz_macro_step_t steps[2] = {
        {AJAZZ_MACRO_KEY_DOWN, 10, 0x04},
        {AJAZZ_MACRO_KEY_UP, 20, 0x04},
    };
    ajazz_report_t out[4];
    int n = ajazz_build_macro_content_chunked(out, 4, 2, steps, 2);
    CHECK(n == 1);
    CHECK(out[0].family == 0x08 && out[0].payload[0] == 2); /* button index, not a constant */
    CHECK(out[0].opcode == 9);
    CHECK(out[0].payload[1] == 0x30 && out[0].payload[2] == 10 && out[0].payload[4] == 0x04);

    /* longer than one report: chunk header is (index << 4) | total */
    ajazz_macro_step_t many[20];
    for (int i = 0; i < 20; i++) many[i] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_DOWN, 5, 0x05};
    ajazz_report_t big[8];
    n = ajazz_build_macro_content_chunked(big, 8, 1, many, 20);
    CHECK(n == 3); /* 1 + 80 bytes = 81 -> 27 + 27 + 27 */
    CHECK(big[0].mode == 0x03 && big[1].mode == 0x13 && big[2].mode == 0x23);
    CHECK(big[0].payload[0] == 1);

    CHECK(ajazz_macro_kind_valid(0x30) && ajazz_macro_kind_valid(0xa0) && !ajazz_macro_kind_valid(0x00) && !ajazz_macro_kind_valid(0xff));

    ajazz_report_t t;
    ajazz_build_reset_keys(&t);
    ajazz_key_slot_set_macro(&t, 3, AJAZZ_MACRO_N_TIMES, 1);
    CHECK(t.payload[9] == 0x90 && t.payload[10] == 0x23 && t.payload[11] == 1); /* the captured 0x23,0x01 */
    ajazz_key_slot_set_macro(&t, 2, AJAZZ_MACRO_WHILE_HELD, 0);
    CHECK(t.payload[6] == 0x90 && t.payload[7] == 0x12);
    ajazz_key_slot_set_macro(&t, 2, AJAZZ_MACRO_UNTIL_CLICK, 0);
    CHECK(t.payload[7] == 0x42);
}

int main(void)
{
    test_checksum();
    test_dpi();
    test_light();
    test_sensor_power();
    test_keys();
    test_macro();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("protocol: all checks passed\n");
    return 0;
}
