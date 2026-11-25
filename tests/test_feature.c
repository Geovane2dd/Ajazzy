/* Frame checks for the feature-report 0x0C protocol (protocol/feature.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../protocol/feature.h"

int main(void)
{
    uint8_t f[8][AJAZZ_FEATURE_LEN], seq = 5;
    ajazz_report_t r;

    /* DPI: stage 1 active, 6 stages, stage 1 = 800 */
    uint16_t dpi[6] = { 800, 800, 1200, 1600, 2400, 3200 };
    ajazz_build_set_dpi_table(&r, dpi, 0, 6);
    assert(ajazz_feature_translate(&r, NULL, f, 8, &seq) == 1);
    const uint8_t want_dpi[] = { 0x0C, 0x01, 0x05, 0x00, 0x05, 0x01, 0x12, 0x16, 0x3F,
        0x0F, 0x00, 0x0F, 0x00, 0x17, 0x00, 0x1F, 0x00, 0x2F, 0x00, 0x3F, 0x00 };
    assert(memcmp(f[0], want_dpi, sizeof(want_dpi)) == 0);
    assert(seq == 6);

    /* report rate index 3 = 1000 Hz = interval 1 ms */
    ajazz_build_set_report_rate(&r, 3);
    assert(ajazz_feature_translate(&r, NULL, f, 8, &seq) == 1);
    assert(f[0][2] == 0x07 && f[0][6] == 1 && f[0][7] == 1);

    /* key table: only the changed slot is sent */
    ajazz_report_t cached, keys;
    ajazz_build_reset_keys(&cached);
    keys = cached;
    ajazz_key_slot_set_function(&keys, 3, AJAZZ_BTN_DISABLED);
    assert(ajazz_feature_translate(&keys, &cached, f, 8, &seq) == 1);
    assert(f[0][2] == 0x04 && f[0][7] == 3 && f[0][8] == 0x60);

    /* sensor perf would collide with the report-rate command: refused */
    ajazz_sensor_t s = { .lift_off = 2 };
    ajazz_build_set_sensor_perf(&r, &s);
    assert(ajazz_feature_translate(&r, NULL, f, 8, &seq) == -1);

    /* descriptor of the AJ159 dongle's MI_01 vendor collection */
    const uint8_t desc[] = { 0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x0c, 0x09, 0x01,
        0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x20, 0xb1, 0x02, 0xc0 };
    assert(ajazz_feature_is_interface(desc, sizeof(desc)));

    puts("feature protocol: all checks passed");
    return 0;
}
