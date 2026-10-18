/*
 * Keyboard mapping helpers shared by the Buttons and Macros pages: GDK/evdev
 * keycodes to USB HID usages, and readable names for them.
 */
#ifndef AJAZZY_GUI_KEYMAP_H
#define AJAZZY_GUI_KEYMAP_H

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

/* GDK keycode (XKB, evdev + 8) -> HID keyboard usage, 0 if unmapped or a modifier */
uint8_t keymap_hid_from_gdk(guint gdk_keycode);
/* GDK keycode -> HID modifier bit (1 LCtrl .. 0x80 RGui), 0 if not a modifier */
uint8_t keymap_modifier_bit(guint gdk_keycode);
/* "Ctrl + Shift + A", from a HID modifier mask and usage (either may be 0) */
void keymap_name(uint8_t mods, uint8_t key, char *out, size_t n);
/* Name of a single HID usage ("A", "F5", "Enter") */
void keymap_key_name(uint8_t key, char *out, size_t n);
/* Name of a single modifier bit ("Ctrl", "Shift"...) */
const char *keymap_modifier_name(uint8_t bit);

#endif
