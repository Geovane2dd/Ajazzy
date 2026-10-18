#include <stdio.h>
#include <string.h>

#include "keymap.h"

/* evdev keycode -> USB HID keyboard usage */
static const uint8_t EVDEV_TO_HID[128] = {
    [1] = 0x29, [2] = 0x1e, [3] = 0x1f, [4] = 0x20, [5] = 0x21, [6] = 0x22, [7] = 0x23, [8] = 0x24,
    [9] = 0x25, [10] = 0x26, [11] = 0x27, [12] = 0x2d, [13] = 0x2e, [14] = 0x2a, [15] = 0x2b,
    [16] = 0x14, [17] = 0x1a, [18] = 0x08, [19] = 0x15, [20] = 0x17, [21] = 0x1c, [22] = 0x18,
    [23] = 0x0c, [24] = 0x12, [25] = 0x13, [26] = 0x2f, [27] = 0x30, [28] = 0x28,
    [30] = 0x04, [31] = 0x16, [32] = 0x07, [33] = 0x09, [34] = 0x0a, [35] = 0x0b, [36] = 0x0d,
    [37] = 0x0e, [38] = 0x0f, [39] = 0x33, [40] = 0x34, [41] = 0x35, [43] = 0x31,
    [44] = 0x1d, [45] = 0x1b, [46] = 0x06, [47] = 0x19, [48] = 0x05, [49] = 0x11, [50] = 0x10,
    [51] = 0x36, [52] = 0x37, [53] = 0x38, [55] = 0x55, [57] = 0x2c, [58] = 0x39,
    [59] = 0x3a, [60] = 0x3b, [61] = 0x3c, [62] = 0x3d, [63] = 0x3e, [64] = 0x3f, [65] = 0x40,
    [66] = 0x41, [67] = 0x42, [68] = 0x43, [69] = 0x53, [70] = 0x47,
    [71] = 0x5f, [72] = 0x60, [73] = 0x61, [74] = 0x56, [75] = 0x5c, [76] = 0x5d, [77] = 0x5e,
    [78] = 0x57, [79] = 0x59, [80] = 0x5a, [81] = 0x5b, [82] = 0x62, [83] = 0x63,
    [86] = 0x64, [87] = 0x44, [88] = 0x45, [96] = 0x58, [98] = 0x54, [99] = 0x46,
    [102] = 0x4a, [103] = 0x52, [104] = 0x4b, [105] = 0x50, [106] = 0x4f, [107] = 0x4d,
    [108] = 0x51, [109] = 0x4e, [110] = 0x49, [111] = 0x4c, [119] = 0x48, [127] = 0x65,
};

uint8_t keymap_hid_from_gdk(guint gdk_keycode)
{
    guint ev = gdk_keycode >= 8 ? gdk_keycode - 8 : 0;
    if (ev >= sizeof(EVDEV_TO_HID))
        return 0;
    return EVDEV_TO_HID[ev];
}

uint8_t keymap_modifier_bit(guint gdk_keycode)
{
    switch (gdk_keycode >= 8 ? gdk_keycode - 8 : 0) {
        case 29: return 0x01;  /* left ctrl   */
        case 42: return 0x02;  /* left shift  */
        case 56: return 0x04;  /* left alt    */
        case 125: return 0x08; /* left meta   */
        case 97: return 0x10;
        case 54: return 0x20;
        case 100: return 0x40;
        case 126: return 0x80;
        default: return 0;
    }
}

const char *keymap_modifier_name(uint8_t bit)
{
    switch (bit) {
        case 0x01: case 0x10: return "Ctrl";
        case 0x02: case 0x20: return "Shift";
        case 0x04: case 0x40: return "Alt";
        case 0x08: case 0x80: return "Super";
        default: return "?";
    }
}

void keymap_key_name(uint8_t key, char *out, size_t n)
{
    static const char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (key >= 0x04 && key <= 0x1d) {
        snprintf(out, n, "%c", letters[key - 0x04]);
    } else if (key >= 0x1e && key <= 0x26) {
        snprintf(out, n, "%d", key - 0x1e + 1);
    } else if (key == 0x27) {
        snprintf(out, n, "0");
    } else if (key >= 0x3a && key <= 0x45) {
        snprintf(out, n, "F%d", key - 0x3a + 1);
    } else {
        static const struct { uint8_t k; const char *n; } names[] = {
            {0x28, "Enter"}, {0x29, "Esc"}, {0x2a, "Backspace"}, {0x2b, "Tab"}, {0x2c, "Space"},
            {0x2d, "-"}, {0x2e, "="}, {0x2f, "["}, {0x30, "]"}, {0x31, "\\"}, {0x33, ";"}, {0x34, "'"},
            {0x35, "`"}, {0x36, ","}, {0x37, "."}, {0x38, "/"}, {0x39, "Caps Lock"},
            {0x46, "Print Screen"}, {0x47, "Scroll Lock"}, {0x48, "Pause"}, {0x49, "Insert"},
            {0x4a, "Home"}, {0x4b, "Page Up"}, {0x4c, "Delete"}, {0x4d, "End"}, {0x4e, "Page Down"},
            {0x4f, "Right"}, {0x50, "Left"}, {0x51, "Down"}, {0x52, "Up"}, {0x53, "Num Lock"},
            {0x54, "Num /"}, {0x55, "Num *"}, {0x56, "Num -"}, {0x57, "Num +"}, {0x58, "Num Enter"},
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (names[i].k == key) {
                snprintf(out, n, "%s", names[i].n);
                return;
            }
        if (key >= 0x59 && key <= 0x62)
            snprintf(out, n, "Num %d", key == 0x62 ? 0 : key - 0x59 + 1);
        else
            snprintf(out, n, "0x%02X", key);
    }
}

void keymap_name(uint8_t mods, uint8_t key, char *out, size_t n)
{
    char buf[96] = "";
    size_t len = 0;
    const struct { uint8_t mask; const char *name; } m[] = {
        {0x11, "Ctrl"}, {0x22, "Shift"}, {0x44, "Alt"}, {0x88, "Super"},
    };
    for (size_t i = 0; i < 4; i++)
        if (mods & m[i].mask)
            len += (size_t)snprintf(buf + len, sizeof(buf) - len, "%s%s", len ? " + " : "", m[i].name);
    if (key) {
        char k[24];
        keymap_key_name(key, k, sizeof(k));
        snprintf(buf + len, sizeof(buf) - len, "%s%s", len ? " + " : "", k);
    }
    snprintf(out, n, "%s", buf);
}
