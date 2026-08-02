/*
 * Ajazzy GUI, a GTK4/libadwaita front-end for ajazzyctl.
 *
 * Built from native libadwaita widgets (AdwNavigationSplitView,
 * AdwPreferencesGroup/Row) rather than a hand-painted theme, so it
 * follows the user's system light/dark preference and accent colour
 * instead of forcing a fixed palette -- the only custom CSS left is for
 * things libadwaita has no row type for (DPI stage swatches, the button
 * marker overlay on the mouse diagram).
 *
 * Every user-facing string goes through _() (gettext). English is the
 * source language; po/pt_BR.po has the Portuguese translation.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <math.h>

#include "../protocol/hid.h"
#include "../protocol/transport.h"
#include "../devices/device.h"
#include "tray.h"

#ifndef LOCALEDIR
#define LOCALEDIR "locale"
#endif
#ifndef ICONDIR
#define ICONDIR "gui/icons"
#endif

#define AJAZZ_APP_ICON "io.github.ajazzy.Gui"

/*
 * Ajazzy has its own brand identity (matching the app icon's red/orange
 * mouse mark on near-black, see gui/icons/io.github.ajazzy.Gui.svg)
 * rather than just inheriting whatever accent colour the user's system
 * theme happens to be set to. Redefining accent_color/accent_bg_color/
 * accent_fg_color at APPLICATION priority re-themes every stock
 * libadwaita "suggested-action" button, switch, and selection highlight
 * to match -- this is the single highest-leverage thing for making the
 * app feel like a distinct product instead of a generic GNOME settings
 * panel, since those widgets are used throughout every page.
 */
static const char *CSS =
    "@define-color accent_color #f0564a;"
    "@define-color accent_bg_color #f0564a;"
    "@define-color accent_fg_color #ffffff;"
    "@define-color ajazz_bg_deep #0c0e10;"
    "@define-color ajazz_panel #15181b;"

    "window.background, .background { background-color: @ajazz_bg_deep; }"

    ".ajazz-dpi-btn { border-radius: 10px; padding: 12px; font-weight: 800; min-width: 76px;"
    "  box-shadow: 0 1px 3px rgba(0,0,0,0.4); transition: box-shadow 150ms ease; }"
    ".ajazz-dpi-btn.selected { outline: 2px solid #fff; outline-offset: 2px;"
    "  box-shadow: 0 0 0 3px alpha(@accent_color, 0.55), 0 0 18px 2px alpha(@accent_color, 0.55); }"
    /* matching the mouse's own confirmed per-DPI-stage indicator LED
     * colours (query 0x14, see docs/reverse-engineering.md): red, green,
     * blue, cyan, yellow, magenta -- not arbitrary decoration, this is
     * literally what the hardware lights up for each stage. */
    ".ajazz-dpi-1 { background-color: #ff3b3b; color: white; }"
    ".ajazz-dpi-2 { background-color: #2ecc40; color: black; }"
    ".ajazz-dpi-3 { background-color: #3b6bff; color: white; }"
    ".ajazz-dpi-4 { background-color: #00d1d1; color: black; }"
    ".ajazz-dpi-5 { background-color: #ffd400; color: black; }"
    ".ajazz-dpi-6 { background-color: #e838e8; color: white; }"

    ".ajazz-marker { min-width: 27px; min-height: 27px; border-radius: 14px;"
    "  font-size: 11px; font-weight: 800; padding: 0;"
    "  background-color: alpha(#000, 0.55); color: #fff; border: 1.5px solid alpha(#fff, 0.4); }"
    ".ajazz-marker.selected { background-color: @accent_bg_color; color: @accent_fg_color;"
    "  border-color: #fff; box-shadow: 0 0 0 3px alpha(@accent_bg_color, 0.4), 0 0 14px alpha(@accent_bg_color, 0.6); }"

    ".ajazz-sidebar-panel { background-color: @ajazz_panel;"
    "  background-image: linear-gradient(180deg, alpha(@accent_color, 0.10), transparent 220px);"
    "  border-right: 1px solid alpha(#fff, 0.06); }"
    ".ajazz-sidebar-header { padding: 18px 16px 18px 16px;"
    "  border-bottom: 1px solid alpha(@accent_color, 0.35); }"
    ".ajazz-sidebar-title { font-size: 18px; font-weight: 900; letter-spacing: 3px; color: #fff; }"
    ".ajazz-sidebar-subtitle { font-size: 10px; font-weight: 700; letter-spacing: 1px;"
    "  text-transform: uppercase; opacity: 0.5; }"
    ".navigation-sidebar row { border-left: 3px solid transparent; margin: 1px 6px; border-radius: 8px; }"
    ".navigation-sidebar row:selected { background-color: alpha(@accent_color, 0.16);"
    "  border-left: 3px solid @accent_color; }"
    ".navigation-sidebar row:selected label { font-weight: 700; }"

    ".ajazz-status-connected { color: #3fe07a; font-weight: 700; }"
    ".ajazz-status-disconnected { color: #ff5c5c; font-weight: 700; }"
    ".ajazz-status-dot { min-width: 8px; min-height: 8px; border-radius: 5px; margin: 0 2px; }"
    ".ajazz-status-dot.connected { background-color: #3fe07a; box-shadow: 0 0 6px #3fe07a; }"
    ".ajazz-status-dot.disconnected { background-color: #ff5c5c; }"

    "preferencesgroup > box > label.title { letter-spacing: 0.5px; font-weight: 800;"
    "  text-transform: uppercase; font-size: 12px; opacity: 0.7; }"
    ".ajazz-cta { font-weight: 800; padding-left: 24px; padding-right: 24px; }"
    "button.suggested-action.pill { box-shadow: 0 2px 14px alpha(@accent_color, 0.4); }"

    /* Home page hero */
    ".ajazz-hero { border-radius: 20px; padding: 22px 26px;"
    "  background-image: linear-gradient(135deg, alpha(@accent_color, 0.22), alpha(@accent_color, 0.03) 70%);"
    "  border: 1px solid alpha(@accent_color, 0.3); }"
    ".ajazz-hero-name { font-size: 24px; font-weight: 900; color: #fff; }"
    ".ajazz-hero-sub { font-size: 12px; font-weight: 600; letter-spacing: 1px; text-transform: uppercase; opacity: 0.55; }"
    ".ajazz-stat-label { font-size: 10px; font-weight: 700; letter-spacing: 0.8px; text-transform: uppercase; opacity: 0.5; }"
    ".ajazz-stat-value { font-size: 15px; font-weight: 800; }"

    /* battery bars */
    ".ajazz-batt-seg { min-width: 9px; min-height: 20px; border-radius: 2px;"
    "  background-color: alpha(#fff, 0.12); }"
    ".ajazz-batt-seg.lit { background-color: @accent_color; box-shadow: 0 0 8px alpha(@accent_color, 0.65); }"

    /* lighting preview swatch */
    ".ajazz-rgb-preview { border-radius: 16px; min-height: 72px;"
    "  border: 1px solid alpha(#fff, 0.14); transition: all 200ms ease; }"
    ".ajazz-rgb-preview.ajazz-rgb-off { background-color: alpha(#fff, 0.05); }"
    ".ajazz-rgb-preview.ajazz-rgb-constant { background-color: @accent_color;"
    "  box-shadow: inset 0 0 30px alpha(#000, 0.25); }"
    ".ajazz-rgb-preview.ajazz-rgb-rainbow {"
    "  background-image: linear-gradient(90deg, #ff0000, #ffae00, #2ecc40, #00d1d1, #3b6bff, #e838e8, #ff0000); }"

    /* tested/untested badges */
    ".ajazz-badge { border-radius: 999px; padding: 3px 11px; font-size: 11px; font-weight: 800; letter-spacing: 0.3px; }"
    ".ajazz-badge-green { background-color: alpha(#3fe07a, 0.16); color: #3fe07a; border: 1px solid alpha(#3fe07a, 0.45); }"
    ".ajazz-badge-yellow { background-color: alpha(#ffc93f, 0.16); color: #ffc93f; border: 1px solid alpha(#ffc93f, 0.45); }";

static GtkWidget *make_logo_widget(int size)
{
    GtkWidget *img = gtk_image_new_from_icon_name(AJAZZ_APP_ICON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), size);
    return img;
}

typedef struct {
    AdwApplicationWindow *win;
    GtkApplication *gtk_app;
    AjazzTray *tray;
    AdwToastOverlay *toast_overlay;
    AdwViewStack *stack;
    GtkListBox *sidebar_list;
    GtkLabel *status_lbl;
    GtkWidget *status_dot;

    ajazz_dev_t dev;
    gboolean connected;

    /* home */
    GtkLabel *lbl_name;
    GtkLabel *lbl_vidpid;
    GtkLabel *lbl_devid;
    GtkLabel *lbl_battery;
    GtkLabel *lbl_confirmed;
    GtkWidget *hero_status_dot;
    GtkLabel *hero_status_lbl;
    GtkWidget *batt_seg[4];
    GtkWidget *cap_badge[7];

    /* dpi / config */
    GtkToggleButton *dpi_btn[AJAZZ_DPI_STAGES];
    AdwSpinRow *dpi_value_spin;
    AdwActionRow *dpi_value_row;
    int dpi_selected;
    uint16_t dpi_cache[AJAZZ_DPI_STAGES];
    GtkToggleButton *rate_btn[4];
    GtkCheckButton *liftoff1;
    GtkCheckButton *liftoff2;
    AdwComboRow *sleep_combo;
    AdwSwitchRow *idle_light_switch;

    /* lighting */
    AdwComboRow *rgb_effect;
    AdwSpinRow *rgb_brightness;
    AdwActionRow *rgb_speed_row;
    AdwSpinRow *rgb_speed;
    GtkWidget *rgb_preview;

    /* buttons */
    AdwComboRow *key_dropdown[AJAZZ_KEY_SLOTS];
    GtkWidget *key_row[AJAZZ_KEY_SLOTS];
    GtkWidget *key_marker[AJAZZ_KEY_SLOTS];
    GtkWidget *key_fire_box[AJAZZ_KEY_SLOTS];
    AdwSpinRow *key_fire_interval[AJAZZ_KEY_SLOTS];
    AdwSpinRow *key_fire_number[AJAZZ_KEY_SLOTS];

    /* macro */
    GtkToggleButton *macro_slot_btn[AJAZZ_KEY_SLOTS];
    int macro_slot_selected;
    AdwPreferencesGroup *macro_actions_group;
    GPtrArray *macro_action_rows; /* owns AdwComboRow* widgets, in order */
    GtkWidget *macro_add_btn;
    GtkWidget *macro_experimental_hint;
    AdwSpinRow *macro_delay;
    GtkLabel *macro_status_lbl[AJAZZ_KEY_SLOTS];
} App;

/* Auto-sleep timer presets, exactly matching the 8 values confirmed
 * against the original driver's own sleep-time dropdown (see
 * docs/reverse-engineering.md) -- offering the same fixed list here
 * rather than a freeform seconds field keeps the UI honest about what's
 * actually been tested, and matches the source app's own UX. */
static const struct { const char *label; int seconds; } sleep_presets[] = {
    {N_("10 seconds"), 10},
    {N_("30 seconds"), 30},
    {N_("1 minute"),   60},
    {N_("2 minutes"),  120},
    {N_("5 minutes"),  300},
    {N_("10 minutes"), 600},
    {N_("20 minutes"), 1200},
    {N_("30 minutes"), 1800},
};

enum { KF_SKIP, KF_RESET, KF_CONSUMER, KF_KEYBOARD, KF_FIRE };

typedef struct {
    char label[48];
    int kind;
    uint16_t code;
} key_function_t;

#define MAX_KEY_FUNCTIONS 64
static key_function_t key_functions[MAX_KEY_FUNCTIONS];
static size_t n_key_functions;

/* macro action list: keyboard keys + mouse buttons, flat, for the macro
 * editor's per-step dropdowns. Index 0 is always "(none)". */
enum { MA_NONE, MA_KEY, MA_BUTTON };
typedef struct {
    char label[48];
    int kind;
    uint16_t code; /* HID keycode, or mouse button bit (1,2,4,8,16) */
} macro_action_t;

#define MAX_MACRO_ACTIONS 48
static macro_action_t macro_actions[MAX_MACRO_ACTIONS];
static size_t n_macro_actions;

static void init_key_functions(void)
{
    size_t i = 0;

    g_strlcpy(key_functions[i].label, _("Don't change"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_SKIP;
    i++;

    g_strlcpy(key_functions[i].label, _("Default (built-in click)"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_RESET;
    i++;

    g_strlcpy(key_functions[i].label, _("Rapid-fire (auto-click)"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_FIRE;
    i++;

    struct { const char *label; uint16_t usage; } consumer[] = {
        {N_("Volume up"),     AJAZZ_USAGE_VOLUME_UP},
        {N_("Volume down"),   AJAZZ_USAGE_VOLUME_DOWN},
        {N_("Mute"),          AJAZZ_USAGE_MUTE},
        {N_("Play / Pause"),  AJAZZ_USAGE_PLAY_PAUSE},
        {N_("Next track"),    AJAZZ_USAGE_NEXT_TRACK},
        {N_("Previous track"),AJAZZ_USAGE_PREV_TRACK},
        {N_("Stop"),          AJAZZ_USAGE_STOP},
        {N_("Browser home"),  AJAZZ_USAGE_AC_HOME},
        {N_("Browser back"),  AJAZZ_USAGE_AC_BACK},
        {N_("Browser forward"),AJAZZ_USAGE_AC_FORWARD},
        {N_("Refresh page"),  AJAZZ_USAGE_AC_REFRESH},
        {N_("Bookmarks"),     AJAZZ_USAGE_AC_BOOKMARKS},
    };
    for (size_t c = 0; c < G_N_ELEMENTS(consumer); c++, i++) {
        g_strlcpy(key_functions[i].label, gettext(consumer[c].label), sizeof(key_functions[i].label));
        key_functions[i].kind = KF_CONSUMER;
        key_functions[i].code = consumer[c].usage;
    }

    for (char c = 'A'; c <= 'Z'; c++, i++) {
        snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), c);
        key_functions[i].kind = KF_KEYBOARD;
        key_functions[i].code = (uint8_t)(0x04 + (c - 'A'));
    }
    for (char c = '1'; c <= '9'; c++, i++) {
        snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), c);
        key_functions[i].kind = KF_KEYBOARD;
        key_functions[i].code = (uint8_t)(0x1E + (c - '1'));
    }
    snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), '0');
    key_functions[i].kind = KF_KEYBOARD;
    key_functions[i].code = 0x27;
    i++;

    n_key_functions = i;
}
#define N_KEY_FUNCTIONS n_key_functions

static void init_macro_actions(void)
{
    size_t i = 0;

    g_strlcpy(macro_actions[i].label, _("(none)"), sizeof(macro_actions[i].label));
    macro_actions[i].kind = MA_NONE;
    i++;

    struct { const char *label; uint16_t bit; } buttons[] = {
        {N_("Left click"),   1},
        {N_("Right click"),  2},
        {N_("Middle click"), 4},
        {N_("Button 4"),     8},
        {N_("Button 5"),     16},
    };
    for (size_t b = 0; b < G_N_ELEMENTS(buttons); b++, i++) {
        g_strlcpy(macro_actions[i].label, gettext(buttons[b].label), sizeof(macro_actions[i].label));
        macro_actions[i].kind = MA_BUTTON;
        macro_actions[i].code = buttons[b].bit;
    }

    for (char c = 'A'; c <= 'Z'; c++, i++) {
        snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), c);
        macro_actions[i].kind = MA_KEY;
        macro_actions[i].code = (uint8_t)(0x04 + (c - 'A'));
    }
    for (char c = '1'; c <= '9'; c++, i++) {
        snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), c);
        macro_actions[i].kind = MA_KEY;
        macro_actions[i].code = (uint8_t)(0x1E + (c - '1'));
    }
    snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), '0');
    macro_actions[i].kind = MA_KEY;
    macro_actions[i].code = 0x27;
    i++;

    n_macro_actions = i;
}
#define N_MACRO_ACTIONS n_macro_actions

static void free_closure_data(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static void refresh_buttons(App *app);
static void refresh_macro_status(App *app);
static void set_confirm_badge(GtkWidget *badge, gboolean confirmed);

/* Reading a table back immediately after writing to it sometimes catches
 * the mouse mid-update and returns the old values (seen with the button
 * table, report rate, and battery alike) -- give it a moment before
 * refreshing the UI from a fresh read. */
static gboolean refresh_buttons_delayed_cb(gpointer user_data)
{
    refresh_buttons((App *)user_data);
    return G_SOURCE_REMOVE;
}

static void refresh_buttons_delayed(App *app)
{
    g_timeout_add(250, refresh_buttons_delayed_cb, app);
}

static gboolean refresh_macro_status_delayed_cb(gpointer user_data)
{
    refresh_macro_status((App *)user_data);
    return G_SOURCE_REMOVE;
}

static void refresh_macro_status_delayed(App *app)
{
    g_timeout_add(250, refresh_macro_status_delayed_cb, app);
}

static void toast(App *app, const char *msg)
{
    AdwToast *t = adw_toast_new(msg);
    adw_toast_set_timeout(t, 3);
    adw_toast_overlay_add_toast(app->toast_overlay, t);
}

/* Sets both a status-dot widget's colour and (if given) an accompanying
 * label's text/colour in one place, since the sidebar footer and the Home
 * hero both show the same connection state and should never disagree. */
static void set_status_widgets(GtkWidget *dot, GtkLabel *lbl, gboolean connected)
{
    if (dot) {
        gtk_widget_remove_css_class(dot, connected ? "disconnected" : "connected");
        gtk_widget_add_css_class(dot, connected ? "connected" : "disconnected");
    }
    if (lbl) {
        gtk_label_set_text(lbl, connected ? _("Connected") : _("Disconnected"));
        gtk_widget_remove_css_class(GTK_WIDGET(lbl), connected ? "ajazz-status-disconnected" : "ajazz-status-connected");
        gtk_widget_add_css_class(GTK_WIDGET(lbl), connected ? "ajazz-status-connected" : "ajazz-status-disconnected");
    }
}

static gboolean ensure_connected(App *app)
{
    if (app->connected)
        return TRUE;
    if (ajazz_open(&app->dev) == 0) {
        app->connected = TRUE;
        set_status_widgets(app->status_dot, app->status_lbl, TRUE);
        return TRUE;
    }
    toast(app, _("No AJAZZ mouse found. Check the connection and permissions."));
    return FALSE;
}

/* Tier boundaries must match ajazz_battery_label() in protocol/hid.c --
 * kept as a separate small function here (rather than parsing the label
 * string back apart) since the GUI needs a 0-4 count to light up bars,
 * not just a sentence. Only the top tier (raw >= 25 => all 4 lit) is
 * anchored to a confirmed data point, see the comment in protocol/hid.h. */
static int battery_tier(uint8_t raw)
{
    if (raw >= 25) return 4;
    if (raw >= 17) return 3;
    if (raw >= 8)  return 2;
    if (raw >= 1)  return 1;
    return 0;
}

static const char *battery_tier_icon(int tier)
{
    switch (tier) {
        case 4:  return "battery-full-symbolic";
        case 3:  return "battery-good-symbolic";
        case 2:  return "battery-caution-symbolic";
        case 1:  return "battery-low-symbolic";
        case 0:  return "battery-empty-symbolic";
        default: return "battery-missing-symbolic";
    }
}

static void set_battery_bars(App *app, int tier /* -1 = unknown, all dim */)
{
    for (int i = 0; i < 4; i++) {
        gboolean lit = (tier >= 0 && i < tier);
        if (lit) gtk_widget_add_css_class(app->batt_seg[i], "lit");
        else gtk_widget_remove_css_class(app->batt_seg[i], "lit");
    }
}

/* The Home page's Compatibility section is the one place this app tells
 * you what's actually been tried on real hardware -- green means someone
 * confirmed it works on this exact model, yellow means it should work
 * (same protocol as the reference model) but nobody's checked yet. */
static const struct { uint32_t cap; const char *label; } CAP_ROWS[] = {
    {AJAZZ_CAP_DPI,     N_("DPI")},
    {AJAZZ_CAP_RATE,    N_("Report rate")},
    {AJAZZ_CAP_RGB,     N_("RGB lighting")},
    {AJAZZ_CAP_SENSOR,  N_("Sensor and power")},
    {AJAZZ_CAP_KEYS,    N_("Button remapping")},
    {AJAZZ_CAP_BATTERY, N_("Battery")},
    {AJAZZ_CAP_MACRO,   N_("Macros")},
};
#define N_CAP_ROWS (sizeof(CAP_ROWS) / sizeof(CAP_ROWS[0]))

static void refresh_home(App *app)
{
    if (!ensure_connected(app)) {
        gtk_label_set_text(app->lbl_name, _("Not connected"));
        gtk_label_set_text(app->lbl_vidpid, "--");
        gtk_label_set_text(app->lbl_devid, "--");
        gtk_label_set_text(app->lbl_battery, "--");
        gtk_label_set_text(app->lbl_confirmed, "");
        set_status_widgets(app->status_dot, app->status_lbl, FALSE);
        set_status_widgets(app->hero_status_dot, app->hero_status_lbl, FALSE);
        set_battery_bars(app, -1);
        ajazz_tray_set_status(app->tray, _("No AJAZZ mouse detected"), "");
        ajazz_tray_set_icon(app->tray, battery_tier_icon(-1));
        return;
    }

    set_status_widgets(app->status_dot, app->status_lbl, TRUE);
    set_status_widgets(app->hero_status_dot, app->hero_status_lbl, TRUE);
    for (size_t i = 0; i < N_CAP_ROWS; i++)
        set_confirm_badge(app->cap_badge[i], app->dev.confirmed & CAP_ROWS[i].cap);

    gtk_label_set_text(app->lbl_name, app->dev.name);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04X:%04X", app->dev.vid, app->dev.pid);
    gtk_label_set_text(app->lbl_vidpid, buf);
    gtk_label_set_text(app->lbl_devid, app->dev.dev_id[0] ? app->dev.dev_id : "?");

    if (app->dev.confirmed == 0) {
        gtk_label_set_text(app->lbl_confirmed,
            _("This exact model hasn't been individually confirmed to work --\n"
              "it should, since every AJAZZ mouse we've looked at shares the\n"
              "same protocol, but please report back either way."));
    } else {
        gtk_label_set_text(app->lbl_confirmed, _("Protocol confirmed against real hardware for this model."));
    }

    char device_line[256];
    snprintf(device_line, sizeof(device_line), "%s", app->dev.name);

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_BATTERY, &reply, 1000) == 0) {
        int tier = battery_tier(reply.payload[0]);
        const char *label = ajazz_battery_label(reply.payload[0]);
        gtk_label_set_text(app->lbl_battery, label);
        set_battery_bars(app, tier);
        char batt_line[128];
        snprintf(batt_line, sizeof(batt_line), _("Battery: %s"), label);
        ajazz_tray_set_status(app->tray, device_line, batt_line);
        ajazz_tray_set_icon(app->tray, battery_tier_icon(tier));
    } else {
        gtk_label_set_text(app->lbl_battery, "?");
        set_battery_bars(app, -1);
        ajazz_tray_set_status(app->tray, device_line, _("Battery: unknown"));
        ajazz_tray_set_icon(app->tray, battery_tier_icon(-1));
    }
}

static GtkWidget *info_row(const char *title, GtkLabel **out_value)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    GtkWidget *v = gtk_label_new("--");
    gtk_widget_add_css_class(v, "dim-label");
    adw_action_row_add_suffix(row, v);
    *out_value = GTK_LABEL(v);
    return GTK_WIDGET(row);
}

/* Green "Tested" / yellow "Untested" pill for a group's header, driven by
 * dev.confirmed -- only the AJ179 V2 MAX has any bits set right now,
 * every other model in devices/ is untested until someone with one
 * reports back. Sits at the top of each page so it's the first thing
 * you see, not something you have to go dig for. */
static GtkWidget *make_confirm_badge(void)
{
    GtkWidget *lbl = gtk_label_new(_("Untested"));
    gtk_widget_add_css_class(lbl, "ajazz-badge");
    gtk_widget_add_css_class(lbl, "ajazz-badge-yellow");
    return lbl;
}

static void set_confirm_badge(GtkWidget *badge, gboolean confirmed)
{
    if (!badge) return;
    gtk_label_set_text(GTK_LABEL(badge), confirmed ? _("Tested \xE2\x9C\x93") : _("Untested"));
    gtk_widget_remove_css_class(badge, confirmed ? "ajazz-badge-yellow" : "ajazz-badge-green");
    gtk_widget_add_css_class(badge, confirmed ? "ajazz-badge-green" : "ajazz-badge-yellow");
}

static void on_refresh_home(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    refresh_home((App *)user_data);
}

static GtkWidget *build_hero(App *app)
{
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_add_css_class(hero, "ajazz-hero");
    gtk_widget_set_margin_top(hero, 4);
    gtk_widget_set_margin_bottom(hero, 4);

    gtk_box_append(GTK_BOX(hero), make_logo_widget(56));

    GtkWidget *id_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_valign(id_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(id_box, TRUE);

    app->lbl_name = GTK_LABEL(gtk_label_new(_("Not connected")));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_name), "ajazz-hero-name");
    gtk_widget_set_halign(GTK_WIDGET(app->lbl_name), GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(id_box), GTK_WIDGET(app->lbl_name));

    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app->hero_status_dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(app->hero_status_dot, "ajazz-status-dot");
    gtk_widget_add_css_class(app->hero_status_dot, "disconnected");
    gtk_widget_set_valign(app->hero_status_dot, GTK_ALIGN_CENTER);
    app->hero_status_lbl = GTK_LABEL(gtk_label_new(_("Disconnected")));
    gtk_widget_add_css_class(GTK_WIDGET(app->hero_status_lbl), "ajazz-hero-sub");
    gtk_box_append(GTK_BOX(status_row), app->hero_status_dot);
    gtk_box_append(GTK_BOX(status_row), GTK_WIDGET(app->hero_status_lbl));
    gtk_widget_set_halign(status_row, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(id_box), status_row);

    gtk_box_append(GTK_BOX(hero), id_box);

    /* battery bars */
    GtkWidget *batt_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(batt_box, GTK_ALIGN_CENTER);
    GtkWidget *batt_label = gtk_label_new(_("BATTERY"));
    gtk_widget_add_css_class(batt_label, "ajazz-stat-label");
    gtk_widget_set_halign(batt_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(batt_box), batt_label);
    GtkWidget *batt_bars = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_set_halign(batt_bars, GTK_ALIGN_END);
    for (int i = 0; i < 4; i++) {
        GtkWidget *seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(seg, "ajazz-batt-seg");
        app->batt_seg[i] = seg;
        gtk_box_append(GTK_BOX(batt_bars), seg);
    }
    gtk_box_append(GTK_BOX(batt_box), batt_bars);
    app->lbl_battery = GTK_LABEL(gtk_label_new("--"));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_battery), "ajazz-stat-label");
    gtk_widget_set_halign(GTK_WIDGET(app->lbl_battery), GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(batt_box), GTK_WIDGET(app->lbl_battery));
    gtk_box_append(GTK_BOX(hero), batt_box);

    GtkWidget *refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_valign(refresh_btn, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(refresh_btn, "flat");
    gtk_widget_add_css_class(refresh_btn, "circular");
    gtk_widget_set_tooltip_text(refresh_btn, _("Refresh"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_home), app);
    gtk_box_append(GTK_BOX(hero), refresh_btn);

    return hero;
}

static GtkWidget *build_home_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *hero_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_add(hero_group, build_hero(app));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), hero_group);

    AdwPreferencesGroup *dev_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(dev_group, _("Device details"));
    adw_preferences_group_add(dev_group, GTK_WIDGET(info_row(_("VID:PID"), &app->lbl_vidpid)));
    adw_preferences_group_add(dev_group, GTK_WIDGET(info_row(_("Firmware id"), &app->lbl_devid)));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dev_group);

    AdwPreferencesGroup *compat_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(compat_group, _("Compatibility"));
    app->lbl_confirmed = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_confirmed), "dim-label");
    gtk_label_set_wrap(app->lbl_confirmed, TRUE);
    gtk_widget_set_halign(GTK_WIDGET(app->lbl_confirmed), GTK_ALIGN_START);
    gtk_widget_set_margin_start(GTK_WIDGET(app->lbl_confirmed), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(app->lbl_confirmed), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(app->lbl_confirmed), 4);
    adw_preferences_group_add(compat_group, GTK_WIDGET(app->lbl_confirmed));

    for (size_t i = 0; i < N_CAP_ROWS; i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), gettext(CAP_ROWS[i].label));
        app->cap_badge[i] = make_confirm_badge();
        adw_action_row_add_suffix(row, app->cap_badge[i]);
        adw_preferences_group_add(compat_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), compat_group);

    AdwPreferencesGroup *issues_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(issues_group, _("Known limitations"));
    GtkWidget *issues = gtk_label_new(
        _("- Macros support up to 3 key/button press+release actions per\n"
          "  button (a device-side limit on a single write); longer macros\n"
          "  need a multi-packet write that's experimental past that point.\n"
          "- Lighting effect names beyond \"off\", \"rainbow\" and \"constant\"\n"
          "  are not reliably distinguished -- some share the same wire command.\n"
          "- Lift-off distance may also control \"wake mouse on motion\" --\n"
          "  a capture of that toggle landed on the exact same byte.\n"
          "- The per-DPI-stage indicator light colour can be read back, but\n"
          "  the command to change it hasn't been captured yet."));
    gtk_widget_add_css_class(issues, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(issues), TRUE);
    gtk_widget_set_halign(issues, GTK_ALIGN_START);
    gtk_widget_set_margin_start(issues, 6);
    gtk_widget_set_margin_end(issues, 6);
    adw_preferences_group_add(issues_group, issues);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), issues_group);

    return page;
}

static void on_keys_reset_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    App *app = user_data;
    if (strcmp(response, "reset") != 0)
        return;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t req;
    ajazz_build_reset_keys(&req);
    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0) {
        toast(app, _("Button mapping reset to default."));
        refresh_buttons_delayed(app);
    } else {
        toast(app, _("Failed to reset buttons."));
    }
}

static void on_keys_reset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Reset all buttons?"),
        _("This clears every button remap and restores the factory defaults.")));
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "reset", _("Reset"));
    adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_keys_reset_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

static void on_keys_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        toast(app, _("Could not read the current button table."));
        return;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);

    gboolean changed = FALSE;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        if (!app->key_dropdown[i]) continue; /* left/right click, not remappable */
        guint sel = adw_combo_row_get_selected(app->key_dropdown[i]);
        if (sel >= N_KEY_FUNCTIONS) continue;
        const key_function_t *kf = &key_functions[sel];
        if (kf->kind == KF_SKIP) continue;
        changed = TRUE;
        if (kf->kind == KF_RESET) {
            ajazz_key_slot_reset_default(&req, i);
        } else if (kf->kind == KF_CONSUMER) {
            ajazz_key_slot_set_consumer(&req, i, kf->code);
        } else if (kf->kind == KF_KEYBOARD) {
            ajazz_key_slot_set_keyboard(&req, i, 0, (uint8_t)kf->code);
        } else if (kf->kind == KF_FIRE) {
            uint8_t interval = app->key_fire_interval[i]
                ? (uint8_t)adw_spin_row_get_value(app->key_fire_interval[i]) : 5;
            uint8_t number = app->key_fire_number[i]
                ? (uint8_t)adw_spin_row_get_value(app->key_fire_number[i]) : 1;
            ajazz_key_slot_set_fire(&req, i, interval, number);
        }
    }

    if (!changed) { toast(app, _("Nothing selected to change.")); return; }

    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0) {
        toast(app, _("Buttons remapped."));
        refresh_buttons_delayed(app);
    } else {
        toast(app, _("Failed to apply the remap."));
    }
}

/* Finds the dropdown entry that matches what's actually on the mouse
 * right now, if there is one -- KF_RESET for the native-default marker,
 * a KF_CONSUMER/KF_KEYBOARD entry for a recognised code, or -1 for
 * anything we don't have a dropdown entry for (a macro trigger, or a raw
 * code we don't recognise). */
static int find_key_function_for_slot(const uint8_t *slot)
{
    uint8_t marker = slot[0];

    if (marker == 0x10) {
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_RESET)
                return (int)i;
        return -1;
    }
    if (marker == 0x80) {
        uint16_t code = (uint16_t)(slot[1] | (slot[2] << 8));
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_CONSUMER && key_functions[i].code == code)
                return (int)i;
        return -1;
    }
    if (marker == 0x70) {
        uint8_t keycode = slot[2];
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_KEYBOARD && key_functions[i].code == keycode)
                return (int)i;
        return -1;
    }
    if (marker == 0x30) {
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_FIRE)
                return (int)i;
        return -1;
    }
    return -1;
}

/* Text shown under each button's dropdown for what's actually on the
 * mouse right now -- the dropdown's own selected-item text alone isn't
 * a strong enough signal at a glance. */
static void describe_key_slot(const uint8_t *slot, char *out, size_t outlen)
{
    int idx = find_key_function_for_slot(slot);
    if (idx >= 0) {
        snprintf(out, outlen, _("Currently: %s"), key_functions[idx].label);
        return;
    }
    if (slot[0] == 0x90) {
        snprintf(out, outlen, _("Currently: plays a macro"));
        return;
    }
    snprintf(out, outlen, _("Currently: unrecognized code (0x%02x)"), slot[0]);
}

/* What each slot does out of the box. Slot 5 (Forward) is the only one
 * independently confirmed -- resetting it brought back "Forward" in the
 * original driver. The rest follow the standard Windows mouse button
 * order (Left, Right, Middle, Back=XButton1, Forward=XButton2), which
 * this lines up with exactly, but 1-4 haven't been checked one by one. */
static const char *default_button_names[AJAZZ_KEY_SLOTS] = {
    N_("Left click"), N_("Right click"), N_("Middle click"), N_("Back"), N_("Forward"),
};

/* Left/right click can't be remapped in the original driver, so we
 * don't offer it here either -- better to match a limit we know is
 * real than to let someone remap a button and wonder why nothing
 * changes. */
static gboolean key_slot_is_lockable(int slot)
{
    return slot == 0 || slot == 1;
}

/* Shows the interval/number fields only for the row currently set to
 * rapid-fire -- everyone else doesn't need to see them. */
static void update_fire_box_visibility(App *app, int slot)
{
    if (!app->key_dropdown[slot] || !app->key_fire_box[slot])
        return;
    guint sel = adw_combo_row_get_selected(app->key_dropdown[slot]);
    gboolean show = (sel < N_KEY_FUNCTIONS && key_functions[sel].kind == KF_FIRE);
    gtk_widget_set_visible(app->key_fire_box[slot], show);
}

static void on_key_dropdown_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd; (void)pspec;
    gpointer *data = user_data;
    update_fire_box_visibility(data[0], GPOINTER_TO_INT(data[1]));
}

static void refresh_buttons(App *app)
{
    if (!app->connected)
        return;

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0)
        return;

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        const uint8_t *slot = &reply.payload[i * 3];
        if (!app->key_dropdown[i])
            continue; /* left/right click, not remappable */

        int idx = find_key_function_for_slot(slot);
        adw_combo_row_set_selected(app->key_dropdown[i], idx >= 0 ? (guint)idx : 0);

        char desc[64];
        describe_key_slot(slot, desc, sizeof(desc));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(app->key_dropdown[i]), desc);

        if (slot[0] == 0x30 && app->key_fire_interval[i] && app->key_fire_number[i]) {
            adw_spin_row_set_value(app->key_fire_interval[i], slot[1]);
            adw_spin_row_set_value(app->key_fire_number[i], slot[2]);
        }
        update_fire_box_visibility(app, i);
    }
}

/* Traces the mouse-body silhouette into `cr`'s current path, as a
 * rounded-shoulders / tapered-bottom top-down shape -- the same curve
 * family as the app icon's own mouse mark (gui/icons/io.github.ajazzy.Gui.svg),
 * just re-parameterized from that SVG's 128x128 space to an arbitrary
 * (x, y, w, h) box, for visual consistency between the icon and this
 * diagram. Not a drawing of any specific AJAZZ model. */
static void trace_mouse_body(cairo_t *cr, double x, double y, double w, double h)
{
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.5 * w, y);
    cairo_curve_to(cr, x + 0.8125 * w, y, x + w, y + 0.15 * h, x + w, y + 0.40 * h);
    cairo_line_to(cr, x + w, y + 0.65 * h);
    cairo_curve_to(cr, x + w, y + 0.875 * h, x + 0.78125 * w, y + h, x + 0.5 * w, y + h);
    cairo_curve_to(cr, x + 0.21875 * w, y + h, x, y + 0.875 * h, x, y + 0.65 * h);
    cairo_line_to(cr, x, y + 0.40 * h);
    cairo_curve_to(cr, x, y + 0.15 * h, x + 0.1875 * w, y, x + 0.5 * w, y);
    cairo_close_path(cr);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

/* Brand accent (#f0564a) as 0-1 floats, matching the CSS override at the
 * top of this file -- cairo draws in raw RGBA, it can't read GTK theme
 * colours, so this is kept in sync by hand rather than looked up. */
#define AJAZZ_ACCENT_R 0.941
#define AJAZZ_ACCENT_G 0.337
#define AJAZZ_ACCENT_B 0.290

static void draw_mouse_outline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void)area; (void)data;
    double x = width * 0.14, y = 10, w = width * 0.72, h = height - 22;

    /* soft shadow: same silhouette, offset down and blurred by stacking
     * a few progressively larger/fainter copies (cairo has no blur op) */
    for (int i = 6; i >= 1; i--) {
        trace_mouse_body(cr, x - i * 0.6, y - i * 0.3 + 5, w + i * 1.2, h + i * 1.2);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.035);
        cairo_fill(cr);
    }

    trace_mouse_body(cr, x, y, w, h);
    cairo_pattern_t *body = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba(body, 0.0, 1, 1, 1, 0.09);
    cairo_pattern_add_color_stop_rgba(body, 0.45, 1, 1, 1, 0.04);
    cairo_pattern_add_color_stop_rgba(body, 1.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.10);
    cairo_set_source(cr, body);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(body);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    /* a soft highlight along the top edge, like light catching the shell */
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.14 * w, y + 0.10 * h);
    cairo_curve_to(cr, x + 0.3 * w, y + 0.01 * h, x + 0.7 * w, y + 0.01 * h, x + 0.86 * w, y + 0.10 * h);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* left/right click seam, following the same curve as the icon's stem */
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.5 * w, y + 0.03 * h);
    cairo_line_to(cr, x + 0.5 * w, y + 0.30 * h);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    /* scroll wheel: body + a couple of grooves for texture */
    double ww = w * 0.14, wh = h * 0.19, wx = x + w / 2 - ww / 2, wy = y + h * 0.03;
    rounded_rect(cr, wx, wy, ww, wh, ww * 0.42);
    cairo_pattern_t *wheel = cairo_pattern_create_linear(wx, wy, wx + ww, wy);
    cairo_pattern_add_color_stop_rgba(wheel, 0.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.55);
    cairo_pattern_add_color_stop_rgba(wheel, 0.5, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.85);
    cairo_pattern_add_color_stop_rgba(wheel, 1.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.55);
    cairo_set_source(cr, wheel);
    cairo_fill(cr);
    cairo_pattern_destroy(wheel);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 2; i++) {
        double gy = wy + wh * (0.32 * i);
        cairo_move_to(cr, wx + ww * 0.22, gy);
        cairo_line_to(cr, wx + ww * 0.78, gy);
    }
    cairo_stroke(cr);
}

static void select_key_marker(App *app, int slot)
{
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        gboolean sel = (i == slot);
        if (app->key_marker[i]) {
            if (sel) gtk_widget_add_css_class(app->key_marker[i], "selected");
            else gtk_widget_remove_css_class(app->key_marker[i], "selected");
        }
        if (app->key_row[i]) {
            if (sel) gtk_widget_add_css_class(app->key_row[i], "accent");
            else gtk_widget_remove_css_class(app->key_row[i], "accent");
        }
    }
    if (app->key_dropdown[slot])
        gtk_widget_grab_focus(GTK_WIDGET(app->key_dropdown[slot]));
}

static void on_marker_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    int slot = GPOINTER_TO_INT(data[1]);
    select_key_marker(app, slot);
}

/* Roughly where each button sits on a typical AJAZZ mouse: left/right
 * click up front, wheel click between them, two side buttons on the
 * thumb rest. Fractions of the drawing area's width/height. */
static const double marker_pos[AJAZZ_KEY_SLOTS][2] = {
    {0.34, 0.13}, /* 1: left click   */
    {0.66, 0.13}, /* 2: right click  */
    {0.50, 0.08}, /* 3: wheel click  */
    {0.06, 0.58}, /* 4: side button, Back (rear)     */
    {0.06, 0.42}, /* 5: side button, Forward (front) */
};

static GtkWidget *build_mouse_diagram(App *app)
{
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 200, 280);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_mouse_outline, NULL, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        GtkWidget *marker = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(marker, "ajazz-marker");
        gtk_widget_add_css_class(marker, "circular");
        gtk_widget_set_halign(marker, GTK_ALIGN_START);
        gtk_widget_set_valign(marker, GTK_ALIGN_START);
        gtk_widget_set_margin_start(marker, (int)(200 * marker_pos[i][0]));
        gtk_widget_set_margin_top(marker, (int)(280 * marker_pos[i][1]));
        app->key_marker[i] = marker;

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(marker, "clicked", G_CALLBACK(on_marker_clicked), data, free_closure_data, 0);

        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), marker);
    }

    return overlay;
}

static GtkWidget *build_buttons_page(App *app)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    gtk_widget_set_margin_start(page, 12);
    gtk_widget_set_margin_end(page, 12);

    GtkWidget *prefs = adw_preferences_page_new();
    gtk_widget_set_hexpand(prefs, TRUE);

    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Button mapping"));
    adw_preferences_group_set_description(group,
        _("Pick a new function for each button, or click a number on the mouse to jump there."));

    const char *names[MAX_KEY_FUNCTIONS + 1];
    for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
        names[i] = key_functions[i].label;
    names[N_KEY_FUNCTIONS] = NULL;

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char title[48];
        snprintf(title, sizeof(title), _("Button %d"), i + 1);

        if (key_slot_is_lockable(i)) {
            AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
            adw_action_row_set_subtitle(row, gettext(default_button_names[i]));
            GtkWidget *fixed = gtk_label_new(_("Fixed by the manufacturer"));
            gtk_widget_add_css_class(fixed, "dim-label");
            adw_action_row_add_suffix(row, fixed);
            adw_preferences_group_add(group, GTK_WIDGET(row));
            app->key_row[i] = GTK_WIDGET(row);
            app->key_dropdown[i] = NULL;
            app->key_fire_box[i] = NULL;
            continue;
        }

        AdwComboRow *dd = ADW_COMBO_ROW(adw_combo_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(dd), title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(dd), gettext(default_button_names[i]));
        adw_combo_row_set_model(dd, G_LIST_MODEL(gtk_string_list_new(names)));
        adw_combo_row_set_selected(dd, 0);
        app->key_dropdown[i] = dd;
        adw_preferences_group_add(group, GTK_WIDGET(dd));
        app->key_row[i] = GTK_WIDGET(dd);

        AdwActionRow *interval_row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(interval_row), _("Rapid-fire interval"));
        AdwSpinRow *interval_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(5, 255, 1));
        adw_spin_row_set_value(interval_spin, 5);
        gtk_widget_set_valign(GTK_WIDGET(interval_spin), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(interval_row, GTK_WIDGET(interval_spin));
        app->key_fire_interval[i] = interval_spin;

        AdwActionRow *number_row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(number_row), _("Rapid-fire repeats"));
        AdwSpinRow *number_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 255, 1));
        adw_spin_row_set_value(number_spin, 1);
        gtk_widget_set_valign(GTK_WIDGET(number_spin), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(number_row, GTK_WIDGET(number_spin));
        app->key_fire_number[i] = number_spin;

        GtkWidget *fire_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(fire_box), GTK_WIDGET(interval_row));
        gtk_box_append(GTK_BOX(fire_box), GTK_WIDGET(number_row));
        gtk_widget_set_visible(fire_box, FALSE);
        adw_preferences_group_add(group, fire_box);
        app->key_fire_box[i] = fire_box;

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(dd, "notify::selected", G_CALLBACK(on_key_dropdown_changed), data, free_closure_data, 0);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), group);

    AdwPreferencesGroup *actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Apply"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_keys_apply), app);
    GtkWidget *btn_reset = gtk_button_new_with_label(_("Reset all"));
    gtk_widget_add_css_class(btn_reset, "destructive-action");
    gtk_widget_add_css_class(btn_reset, "pill");
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(on_keys_reset_clicked), app);
    gtk_box_append(GTK_BOX(actions), btn_apply);
    gtk_box_append(GTK_BOX(actions), btn_reset);
    adw_preferences_group_add(actions_group, actions);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), actions_group);

    GtkWidget *diagram_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(diagram_frame, "card");
    gtk_widget_set_margin_top(diagram_frame, 20);
    gtk_widget_set_valign(diagram_frame, GTK_ALIGN_START);
    GtkWidget *diagram_pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(diagram_pad, 16);
    gtk_widget_set_margin_bottom(diagram_pad, 16);
    gtk_widget_set_margin_start(diagram_pad, 16);
    gtk_widget_set_margin_end(diagram_pad, 16);
    gtk_box_append(GTK_BOX(diagram_pad), build_mouse_diagram(app));
    gtk_frame_set_child(GTK_FRAME(diagram_frame), diagram_pad);

    gtk_box_append(GTK_BOX(page), prefs);
    gtk_box_append(GTK_BOX(page), diagram_frame);
    return page;
}

static void dpi_select(App *app, int stage)
{
    app->dpi_selected = stage;
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        if (i == stage)
            gtk_widget_add_css_class(GTK_WIDGET(app->dpi_btn[i]), "selected");
        else
            gtk_widget_remove_css_class(GTK_WIDGET(app->dpi_btn[i]), "selected");
    }
    adw_spin_row_set_value(app->dpi_value_spin, app->dpi_cache[stage]);
    char buf[32];
    snprintf(buf, sizeof(buf), _("DPI value (stage %d)"), stage + 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->dpi_value_row), buf);
}

static void on_dpi_stage_clicked(GtkButton *btn, gpointer user_data)
{
    App *app = ((gpointer *)user_data)[0];
    int stage = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
    (void)btn;
    dpi_select(app, stage);
}

/* Called every time the Mouse Config page becomes visible, so what's on
 * screen is always what the mouse actually has right now -- there's no
 * "load" button to remember to click. */
static gboolean refresh_config(App *app)
{
    if (!app->connected)
        return FALSE;

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_DPI_TABLE, &reply, 1000) != 0)
        return FALSE;
    ajazz_parse_dpi_table(&reply, app->dpi_cache);

    /* Active stage lives in this same reply's payload[0] high nibble --
     * there's no separate "get active DPI stage" query. */
    int active = ajazz_dpi_table_active_stage(&reply);
    if (active < 0 || active >= AJAZZ_DPI_STAGES) active = 0;
    dpi_select(app, active);

    ajazz_report_t rate_reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_REPORT_RATE, &rate_reply, 1000) == 0) {
        int hz = ajazz_report_rate_hz(rate_reply.payload[0]);
        int idx = (hz == 125) ? 0 : (hz == 250) ? 1 : (hz == 500) ? 2 : (hz == 1000) ? 3 : -1;
        if (idx >= 0)
            gtk_toggle_button_set_active(app->rate_btn[idx], TRUE);
    }

    ajazz_report_t sensor_reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_SENSOR, &sensor_reply, 1000) == 0) {
        gtk_check_button_set_active(sensor_reply.payload[1] ? app->liftoff2 : app->liftoff1, TRUE);
        adw_switch_row_set_active(app->idle_light_switch, sensor_reply.payload[2] != 0);

        int seconds = sensor_reply.payload[0] * 10;
        guint best = 0;
        int best_diff = G_MAXINT;
        for (guint i = 0; i < G_N_ELEMENTS(sleep_presets); i++) {
            int diff = ABS(sleep_presets[i].seconds - seconds);
            if (diff < best_diff) { best_diff = diff; best = i; }
        }
        adw_combo_row_set_selected(app->sleep_combo, best);
    }

    return TRUE;
}

/* Active DPI stage goes through a full table rewrite (family 0x03), not
 * a separate "switch stage" command -- see ajazz_build_set_dpi_table(). */
static void on_dpi_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    app->dpi_cache[app->dpi_selected] = (uint16_t)adw_spin_row_get_value(app->dpi_value_spin);

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, app->dpi_cache, (uint8_t)app->dpi_selected);
    gboolean ok = (ajazz_send_and_wait(&app->dev, &req, 1000) == 0);

    toast(app, ok ? _("DPI applied.") : _("Failed to apply DPI."));
}

static void on_rate_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    int rate_idx = -1;
    for (int i = 0; i < 4; i++)
        if (gtk_toggle_button_get_active(app->rate_btn[i])) rate_idx = i;
    if (rate_idx < 0) { toast(app, _("Pick a report rate first.")); return; }

    ajazz_report_t req;
    ajazz_build_set_report_rate(&req, (uint8_t)rate_idx);
    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0)
        toast(app, _("Report rate applied."));
    else
        toast(app, _("Failed to apply report rate."));
}

/* Sleep timer, lift-off and idle light-off share one report (see
 * ajazz_build_sensor_from_query() in protocol/hid.h), so this reads the
 * current values first and sends all three back together. */
static void on_sensor_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
        toast(app, _("Could not read the current sensor settings."));
        return;
    }

    uint8_t level = gtk_check_button_get_active(app->liftoff2) ? 1 : 0;
    guint sleep_idx = adw_combo_row_get_selected(app->sleep_combo);
    int seconds = (sleep_idx < G_N_ELEMENTS(sleep_presets)) ? sleep_presets[sleep_idx].seconds : 60;
    gboolean idle_off = adw_switch_row_get_active(app->idle_light_switch);

    ajazz_report_t req;
    ajazz_build_sensor_from_query(&req, &reply);
    ajazz_sensor_set_liftoff(&req, level);
    ajazz_sensor_set_sleep_seconds(&req, (uint16_t)seconds);
    ajazz_sensor_set_light_idle_off(&req, (uint8_t)(idle_off ? 1 : 0));

    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0)
        toast(app, _("Sensor and power settings applied."));
    else
        toast(app, _("Failed to apply sensor settings."));
}

static GtkWidget *build_mouse_config_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    /* dpi */
    AdwPreferencesGroup *dpi_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(dpi_group, _("DPI sensitivity"));

    GtkWidget *dpi_row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(dpi_row_box, 6);
    gtk_widget_set_margin_bottom(dpi_row_box, 6);
    gtk_widget_set_halign(dpi_row_box, GTK_ALIGN_CENTER);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        char label[16];
        snprintf(label, sizeof(label), "DPI %d", i + 1);
        GtkWidget *b = gtk_button_new_with_label(label);
        char klass[16];
        snprintf(klass, sizeof(klass), "ajazz-dpi-%d", i + 1);
        gtk_widget_add_css_class(b, "ajazz-dpi-btn");
        gtk_widget_add_css_class(b, klass);
        app->dpi_btn[i] = GTK_TOGGLE_BUTTON(b);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_dpi_stage_clicked), data, free_closure_data, 0);

        gtk_box_append(GTK_BOX(dpi_row_box), b);
    }
    adw_preferences_group_add(dpi_group, dpi_row_box);

    app->dpi_value_row = ADW_ACTION_ROW(adw_action_row_new());
    app->dpi_value_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(50, 26000, 50));
    adw_spin_row_set_value(app->dpi_value_spin, 800);
    gtk_widget_set_valign(GTK_WIDGET(app->dpi_value_spin), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(app->dpi_value_row, GTK_WIDGET(app->dpi_value_spin));
    adw_preferences_group_add(dpi_group, GTK_WIDGET(app->dpi_value_row));

    GtkWidget *dpi_apply = gtk_button_new_with_label(_("Apply DPI"));
    gtk_widget_add_css_class(dpi_apply, "suggested-action");
    gtk_widget_add_css_class(dpi_apply, "pill");
    gtk_widget_set_halign(dpi_apply, GTK_ALIGN_START);
    gtk_widget_set_margin_top(dpi_apply, 4);
    g_signal_connect(dpi_apply, "clicked", G_CALLBACK(on_dpi_apply), app);
    adw_preferences_group_add(dpi_group, dpi_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dpi_group);

    /* report rate */
    AdwPreferencesGroup *rate_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(rate_group, _("Report rate"));
    GtkWidget *rate_row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(rate_row_box, 6);
    gtk_widget_set_margin_bottom(rate_row_box, 6);
    gtk_widget_set_halign(rate_row_box, GTK_ALIGN_CENTER);
    const char *rate_labels[] = {"125 Hz", "250 Hz", "500 Hz", "1000 Hz"};
    GtkToggleButton *rate_toggle_group = NULL;
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_toggle_button_new_with_label(rate_labels[i]);
        gtk_widget_add_css_class(b, "pill");
        if (rate_toggle_group)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), rate_toggle_group);
        else
            rate_toggle_group = GTK_TOGGLE_BUTTON(b);
        app->rate_btn[i] = GTK_TOGGLE_BUTTON(b);
        gtk_box_append(GTK_BOX(rate_row_box), b);
    }
    gtk_toggle_button_set_active(app->rate_btn[3], TRUE);
    adw_preferences_group_add(rate_group, rate_row_box);

    GtkWidget *rate_apply = gtk_button_new_with_label(_("Apply report rate"));
    gtk_widget_add_css_class(rate_apply, "suggested-action");
    gtk_widget_add_css_class(rate_apply, "pill");
    gtk_widget_set_halign(rate_apply, GTK_ALIGN_START);
    gtk_widget_set_margin_top(rate_apply, 4);
    g_signal_connect(rate_apply, "clicked", G_CALLBACK(on_rate_apply), app);
    adw_preferences_group_add(rate_group, rate_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), rate_group);

    /* sensor & power */
    AdwPreferencesGroup *sensor_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(sensor_group, _("Sensor and power"));

    AdwActionRow *lift_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lift_row), _("Lift-off distance"));
    adw_action_row_set_subtitle(lift_row, _("May also control \"wake mouse on motion\" -- see Home"));
    GtkWidget *lift_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(lift_box, GTK_ALIGN_CENTER);
    GtkWidget *lift1 = gtk_check_button_new_with_label(_("1 mm"));
    GtkWidget *lift2 = gtk_check_button_new_with_label(_("2 mm"));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(lift2), GTK_CHECK_BUTTON(lift1));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(lift1), TRUE);
    app->liftoff1 = GTK_CHECK_BUTTON(lift1);
    app->liftoff2 = GTK_CHECK_BUTTON(lift2);
    gtk_box_append(GTK_BOX(lift_box), lift1);
    gtk_box_append(GTK_BOX(lift_box), lift2);
    adw_action_row_add_suffix(lift_row, lift_box);
    adw_preferences_group_add(sensor_group, GTK_WIDGET(lift_row));

    app->sleep_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->sleep_combo), _("Auto-sleep timer"));
    const char *sleep_names[G_N_ELEMENTS(sleep_presets) + 1];
    for (guint i = 0; i < G_N_ELEMENTS(sleep_presets); i++)
        sleep_names[i] = gettext(sleep_presets[i].label);
    sleep_names[G_N_ELEMENTS(sleep_presets)] = NULL;
    adw_combo_row_set_model(app->sleep_combo, G_LIST_MODEL(gtk_string_list_new(sleep_names)));
    adw_combo_row_set_selected(app->sleep_combo, 5); /* 10 minutes, a reasonable default */
    adw_preferences_group_add(sensor_group, GTK_WIDGET(app->sleep_combo));

    app->idle_light_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->idle_light_switch), _("Turn light off when idle"));
    adw_switch_row_set_active(app->idle_light_switch, TRUE);
    adw_preferences_group_add(sensor_group, GTK_WIDGET(app->idle_light_switch));

    GtkWidget *sensor_apply = gtk_button_new_with_label(_("Apply sensor and power"));
    gtk_widget_add_css_class(sensor_apply, "suggested-action");
    gtk_widget_add_css_class(sensor_apply, "pill");
    gtk_widget_add_css_class(sensor_apply, "ajazz-cta");
    gtk_widget_set_halign(sensor_apply, GTK_ALIGN_START);
    gtk_widget_set_margin_top(sensor_apply, 4);
    g_signal_connect(sensor_apply, "clicked", G_CALLBACK(on_sensor_apply), app);
    adw_preferences_group_add(sensor_group, sensor_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), sensor_group);

    dpi_select(app, 0);
    return page;
}

enum { RGB_OFF, RGB_RAINBOW, RGB_CONSTANT };

/* There's no arbitrary colour picker -- rainbow uses a fixed palette and
 * off/constant don't carry a colour choice at all -- so this shows the
 * effect itself: the fixed rainbow gradient, the brand accent for
 * constant (scaled by brightness), a dark swatch for off. */
static void update_rgb_preview(App *app)
{
    guint sel = adw_combo_row_get_selected(app->rgb_effect);
    const char *want = (sel == RGB_OFF) ? "ajazz-rgb-off"
                      : (sel == RGB_CONSTANT) ? "ajazz-rgb-constant"
                      : "ajazz-rgb-rainbow";
    static const char *all_classes[] = {"ajazz-rgb-off", "ajazz-rgb-constant", "ajazz-rgb-rainbow"};
    for (size_t i = 0; i < G_N_ELEMENTS(all_classes); i++) {
        if (strcmp(all_classes[i], want) == 0)
            gtk_widget_add_css_class(app->rgb_preview, all_classes[i]);
        else
            gtk_widget_remove_css_class(app->rgb_preview, all_classes[i]);
    }

    double brightness = (sel == RGB_OFF) ? 0 : adw_spin_row_get_value(app->rgb_brightness);
    gtk_widget_set_opacity(app->rgb_preview, (sel == RGB_OFF) ? 1.0 : 0.35 + 0.65 * (brightness / 4.0));
}

static void update_rgb_row_visibility(App *app)
{
    guint sel = adw_combo_row_get_selected(app->rgb_effect);
    gtk_widget_set_visible(GTK_WIDGET(app->rgb_brightness), sel != RGB_OFF);
    gtk_widget_set_visible(GTK_WIDGET(app->rgb_speed_row), sel == RGB_RAINBOW);
    update_rgb_preview(app);
}

static void on_rgb_effect_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd; (void)pspec;
    update_rgb_row_visibility((App *)user_data);
}

static void on_rgb_brightness_changed(GtkAdjustment *adj, gpointer user_data)
{
    (void)adj;
    update_rgb_preview((App *)user_data);
}

static void on_rgb_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t req;
    guint effect = adw_combo_row_get_selected(app->rgb_effect);
    uint8_t brightness = (uint8_t)adw_spin_row_get_value(app->rgb_brightness);

    switch (effect) {
        case RGB_RAINBOW: {
            uint8_t speed = (uint8_t)adw_spin_row_get_value(app->rgb_speed);
            ajazz_build_set_rgb_rainbow(&req, brightness, speed);
            break;
        }
        case RGB_CONSTANT:
            ajazz_build_set_rgb_constant(&req, brightness);
            break;
        default:
            ajazz_build_set_rgb_off(&req);
            break;
    }

    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0)
        toast(app, _("Lighting applied."));
    else
        toast(app, _("Failed to apply lighting."));
}

/* Called every time the Lighting page becomes visible, same as the other
 * pages. All three effects (off/constant/rainbow) and their
 * brightness/speed are confirmed against real hardware, see the comment
 * on AJAZZ_QUERY_LED_MODE in protocol/hid.h. An unrecognized opcode just
 * leaves the controls alone rather than guessing. */
static gboolean refresh_lighting(App *app)
{
    if (!app->connected)
        return FALSE;

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_LED_MODE, &reply, 1000) != 0)
        return FALSE;

    ajazz_led_state_t st;
    if (ajazz_parse_led_mode(&reply, &st) != 0)
        return TRUE; /* unrecognized state -- leave the controls as they are */

    guint effect_sel;
    switch (st.effect) {
        case AJAZZ_LED_OFF:      effect_sel = RGB_OFF;      break;
        case AJAZZ_LED_CONSTANT: effect_sel = RGB_CONSTANT; break;
        default:                 effect_sel = RGB_RAINBOW;  break;
    }
    adw_combo_row_set_selected(app->rgb_effect, effect_sel);
    if (effect_sel != RGB_OFF)
        adw_spin_row_set_value(app->rgb_brightness, st.brightness);
    if (effect_sel == RGB_RAINBOW)
        adw_spin_row_set_value(app->rgb_speed, st.speed);
    update_rgb_row_visibility(app);

    return TRUE;
}

static GtkWidget *build_lighting_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *preview_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    app->rgb_preview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(app->rgb_preview, "ajazz-rgb-preview");
    gtk_widget_set_margin_top(app->rgb_preview, 4);
    gtk_widget_set_margin_bottom(app->rgb_preview, 4);
    adw_preferences_group_add(preview_group, app->rgb_preview);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), preview_group);

    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("RGB lighting"));
    adw_preferences_group_set_description(group,
        _("\"Off\", rainbow cycle and constant colour apply correctly, and reading back the current effect and its brightness/speed now works too -- all three confirmed against real hardware."));

    app->rgb_effect = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_effect), _("Effect"));
    const char *effect_names[] = {N_("Off"), N_("Rainbow cycle"), N_("Constant colour"), NULL};
    const char *effect_names_tr[4];
    for (int i = 0; i < 3; i++) effect_names_tr[i] = gettext(effect_names[i]);
    effect_names_tr[3] = NULL;
    adw_combo_row_set_model(app->rgb_effect, G_LIST_MODEL(gtk_string_list_new(effect_names_tr)));
    adw_combo_row_set_selected(app->rgb_effect, RGB_RAINBOW);
    g_signal_connect(app->rgb_effect, "notify::selected", G_CALLBACK(on_rgb_effect_changed), app);
    adw_preferences_group_add(group, GTK_WIDGET(app->rgb_effect));

    app->rgb_brightness = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_brightness), _("Brightness"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(app->rgb_brightness), _("0 to 4 (25% steps)"));
    adw_spin_row_set_value(app->rgb_brightness, 4);
    g_signal_connect(adw_spin_row_get_adjustment(app->rgb_brightness), "value-changed",
        G_CALLBACK(on_rgb_brightness_changed), app);
    adw_preferences_group_add(group, GTK_WIDGET(app->rgb_brightness));

    app->rgb_speed = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_speed), _("Speed"));
    adw_spin_row_set_value(app->rgb_speed, 2);
    app->rgb_speed_row = ADW_ACTION_ROW(app->rgb_speed);
    adw_preferences_group_add(group, GTK_WIDGET(app->rgb_speed));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), group);
    update_rgb_row_visibility(app);

    AdwPreferencesGroup *apply_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Apply"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    gtk_widget_set_halign(btn_apply, GTK_ALIGN_START);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_rgb_apply), app);
    adw_preferences_group_add(apply_group, btn_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), apply_group);

    return page;
}

/* ---------------- Macro page ---------------- */

static void macro_select_slot(App *app, int slot)
{
    app->macro_slot_selected = slot;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        gtk_toggle_button_set_active(app->macro_slot_btn[i], i == slot);
}

static void on_macro_slot_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    macro_select_slot((App *)data[0], GPOINTER_TO_INT(data[1]));
}

static void refresh_macro_status(App *app)
{
    if (!app->connected)
        return;

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0)
        return;

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        const uint8_t *slot = &reply.payload[i * 3];
        gboolean is_macro = (slot[0] == 0x90);
        if (app->macro_status_lbl[i])
            gtk_label_set_text(app->macro_status_lbl[i],
                is_macro ? _("plays a macro") : _("default"));
    }
}

static void update_macro_add_button(App *app)
{
    gboolean can_add = app->macro_action_rows->len < AJAZZ_MACRO_MAX_CHUNKED_ACTIONS;
    gtk_widget_set_sensitive(app->macro_add_btn, can_add);
    gtk_widget_set_visible(app->macro_experimental_hint,
        app->macro_action_rows->len > AJAZZ_MACRO_MAX_STEPS / 2);
}

static void renumber_macro_action_rows(App *app)
{
    for (guint i = 0; i < app->macro_action_rows->len; i++) {
        AdwComboRow *row = g_ptr_array_index(app->macro_action_rows, i);
        char title[32];
        snprintf(title, sizeof(title), _("Action %u"), i + 1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    }
}

static void on_macro_action_remove_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    AdwComboRow *row = data[1];

    /* keep at least one row so there's always something to pick from */
    if (app->macro_action_rows->len <= 1)
        return;

    adw_preferences_group_remove(app->macro_actions_group, GTK_WIDGET(row));
    g_ptr_array_remove(app->macro_action_rows, row);
    renumber_macro_action_rows(app);
    update_macro_add_button(app);
}

static AdwComboRow *add_macro_action_row(App *app)
{
    const char *action_names[MAX_MACRO_ACTIONS + 1];
    for (size_t i = 0; i < N_MACRO_ACTIONS; i++)
        action_names[i] = macro_actions[i].label;
    action_names[N_MACRO_ACTIONS] = NULL;

    AdwComboRow *dd = ADW_COMBO_ROW(adw_combo_row_new());
    adw_combo_row_set_model(dd, G_LIST_MODEL(gtk_string_list_new(action_names)));
    adw_combo_row_set_selected(dd, 0);

    GtkWidget *remove_btn = gtk_button_new_from_icon_name("list-remove-symbolic");
    gtk_widget_add_css_class(remove_btn, "flat");
    gtk_widget_add_css_class(remove_btn, "circular");
    gtk_widget_set_valign(remove_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(remove_btn, _("Remove this action"));
    gpointer *data = g_new(gpointer, 2);
    data[0] = app;
    data[1] = dd;
    g_signal_connect_data(remove_btn, "clicked", G_CALLBACK(on_macro_action_remove_clicked), data, free_closure_data, 0);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dd), remove_btn);

    g_ptr_array_add(app->macro_action_rows, dd);
    adw_preferences_group_add(app->macro_actions_group, GTK_WIDGET(dd));
    renumber_macro_action_rows(app);
    update_macro_add_button(app);
    return dd;
}

static void on_macro_add_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    add_macro_action_row((App *)user_data);
}

static void on_macro_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_macro_step_t steps[AJAZZ_MACRO_MAX_CHUNKED_STEPS];
    int n_steps = 0;
    uint8_t delay_ms = (uint8_t)adw_spin_row_get_value(app->macro_delay);

    for (guint i = 0; i < app->macro_action_rows->len; i++) {
        AdwComboRow *row = g_ptr_array_index(app->macro_action_rows, i);
        guint sel = adw_combo_row_get_selected(row);
        if (sel >= N_MACRO_ACTIONS || macro_actions[sel].kind == MA_NONE)
            continue;
        if (macro_actions[sel].kind == MA_BUTTON) {
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_DOWN, delay_ms, (uint8_t)macro_actions[sel].code};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_UP,   delay_ms, (uint8_t)macro_actions[sel].code};
        } else {
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_DOWN, delay_ms, (uint8_t)macro_actions[sel].code};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_UP,   delay_ms, (uint8_t)macro_actions[sel].code};
        }
    }

    if (n_steps == 0) { toast(app, _("Pick at least one action.")); return; }

    ajazz_report_t chunks[(AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + AJAZZ_PAYLOAD_LEN) / AJAZZ_PAYLOAD_LEN + 1];
    int n_chunks = ajazz_build_macro_content_chunked(chunks, (int)(sizeof(chunks) / sizeof(chunks[0])), steps, n_steps);
    if (n_chunks < 0) { toast(app, _("Macro is too long to encode.")); return; }
    for (int i = 0; i < n_chunks; i++) {
        if (ajazz_send_and_wait(&app->dev, &chunks[i], 1000) != 0) {
            toast(app, _("Failed to write macro content."));
            return;
        }
    }

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        toast(app, _("Could not read the current button table."));
        return;
    }
    ajazz_report_t assign;
    ajazz_build_key_table_from_query(&assign, &reply);
    ajazz_key_slot_set_macro(&assign, app->macro_slot_selected);

    if (ajazz_send_and_wait(&app->dev, &assign, 1000) == 0) {
        toast(app, _("Macro saved to button."));
        refresh_macro_status_delayed(app);
    } else {
        toast(app, _("Failed to assign the macro to the button."));
    }
}

static void on_macro_clear(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t reply;
    if (ajazz_query(&app->dev, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        toast(app, _("Could not read the current button table."));
        return;
    }
    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    ajazz_key_slot_reset_default(&req, app->macro_slot_selected);

    if (ajazz_send_and_wait(&app->dev, &req, 1000) == 0) {
        toast(app, _("Button reset to default."));
        refresh_macro_status_delayed(app);
    } else {
        toast(app, _("Failed to reset the button."));
    }
}

static GtkWidget *build_macro_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *slot_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(slot_group, _("Button"));
    adw_preferences_group_set_description(slot_group, _("Choose which button plays the macro below."));

    GtkWidget *slot_row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(slot_row_box, 6);
    gtk_widget_set_margin_bottom(slot_row_box, 6);
    gtk_widget_set_halign(slot_row_box, GTK_ALIGN_CENTER);
    GtkToggleButton *slot_group_btn = NULL;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char label[16];
        snprintf(label, sizeof(label), _("Button %d"), i + 1);
        GtkWidget *b = gtk_toggle_button_new_with_label(label);
        gtk_widget_add_css_class(b, "pill");
        if (slot_group_btn)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), slot_group_btn);
        else
            slot_group_btn = GTK_TOGGLE_BUTTON(b);
        app->macro_slot_btn[i] = GTK_TOGGLE_BUTTON(b);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_macro_slot_clicked), data, free_closure_data, 0);

        GtkWidget *status = gtk_label_new(_("default"));
        gtk_widget_add_css_class(status, "dim-label");
        gtk_widget_add_css_class(status, "caption");
        app->macro_status_lbl[i] = GTK_LABEL(status);

        gtk_box_append(GTK_BOX(box), b);
        gtk_box_append(GTK_BOX(box), status);
        gtk_box_append(GTK_BOX(slot_row_box), box);
    }
    gtk_toggle_button_set_active(app->macro_slot_btn[0], TRUE);
    app->macro_slot_selected = 0;
    adw_preferences_group_add(slot_group, slot_row_box);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), slot_group);

    app->macro_actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(app->macro_actions_group, _("Macro"));
    adw_preferences_group_set_description(app->macro_actions_group,
        _("Actions are played in order, each as press-then-release, with the same delay between every step."));

    app->macro_add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(app->macro_add_btn, "flat");
    gtk_widget_set_valign(app->macro_add_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(app->macro_add_btn, _("Add action"));
    g_signal_connect(app->macro_add_btn, "clicked", G_CALLBACK(on_macro_add_clicked), app);
    adw_preferences_group_set_header_suffix(app->macro_actions_group, app->macro_add_btn);

    /* created before the first add_macro_action_row() call below, since
     * that call runs update_macro_add_button() which references this */
    app->macro_experimental_hint = gtk_label_new(
        _("More than 3 actions needs a multi-packet write that isn't independently\n"
          "confirmed the way the rest of this app's protocol is -- test the button\n"
          "afterward to make sure it plays back correctly."));
    gtk_widget_add_css_class(app->macro_experimental_hint, "warning");
    gtk_widget_add_css_class(app->macro_experimental_hint, "caption");
    gtk_label_set_wrap(GTK_LABEL(app->macro_experimental_hint), TRUE);
    gtk_widget_set_halign(app->macro_experimental_hint, GTK_ALIGN_START);
    gtk_widget_set_visible(app->macro_experimental_hint, FALSE);

    app->macro_action_rows = g_ptr_array_new();
    add_macro_action_row(app);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), app->macro_actions_group);

    AdwPreferencesGroup *delay_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_add(delay_group, app->macro_experimental_hint);

    app->macro_delay = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 255, 5));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->macro_delay), _("Delay between steps"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(app->macro_delay), _("Milliseconds"));
    adw_spin_row_set_value(app->macro_delay, 10);
    adw_preferences_group_add(delay_group, GTK_WIDGET(app->macro_delay));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), delay_group);

    AdwPreferencesGroup *actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Save to button"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_macro_apply), app);
    GtkWidget *btn_clear = gtk_button_new_with_label(_("Reset button to default"));
    gtk_widget_add_css_class(btn_clear, "destructive-action");
    gtk_widget_add_css_class(btn_clear, "pill");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_macro_clear), app);
    gtk_box_append(GTK_BOX(actions), btn_apply);
    gtk_box_append(GTK_BOX(actions), btn_clear);
    adw_preferences_group_add(actions_group, actions);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), actions_group);

    return page;
}

/* ---------------- navigation / window chrome ---------------- */

typedef struct {
    const char *label;
    const char *icon;
    const char *page;
} nav_item_t;

/* Re-reads whichever page is named, from the mouse, right now -- shared by
 * both the "switched to a different page" and "clicked the page that was
 * already open" paths below, so every page is always showing live state
 * rather than whatever was on screen when you last looked at it. */
static void refresh_page(App *app, const char *page_name)
{
    if (strcmp(page_name, "home") == 0)
        refresh_home(app);
    else if (strcmp(page_name, "config") == 0)
        refresh_config(app);
    else if (strcmp(page_name, "buttons") == 0)
        refresh_buttons(app);
    else if (strcmp(page_name, "macro") == 0)
        refresh_macro_status(app);
    else if (strcmp(page_name, "lighting") == 0)
        refresh_lighting(app);
}

static void on_sidebar_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row)
        return;

    const char *page_name = g_object_get_data(G_OBJECT(row), "page");
    adw_view_stack_set_visible_child_name(app->stack, page_name);
    refresh_page(app, page_name);
}

static GtkWidget *build_sidebar(App *app, const nav_item_t *items, size_t n_items)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "ajazz-sidebar-panel");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(header, "ajazz-sidebar-header");
    gtk_box_append(GTK_BOX(header), make_logo_widget(34));

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new(_("Ajazzy"));
    gtk_widget_add_css_class(title, "ajazz-sidebar-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *subtitle = gtk_label_new(_("AJAZZ mouse configurator"));
    gtk_widget_add_css_class(subtitle, "ajazz-sidebar-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_box_append(GTK_BOX(header), title_box);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    app->sidebar_list = GTK_LIST_BOX(list);

    for (size_t i = 0; i < n_items; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_top(rowbox, 8);
        gtk_widget_set_margin_bottom(rowbox, 8);
        gtk_widget_set_margin_start(rowbox, 12);
        gtk_widget_set_margin_end(rowbox, 12);
        GtkWidget *icon = gtk_image_new_from_icon_name(items[i].icon);
        GtkWidget *lbl = gtk_label_new(gettext(items[i].label));
        gtk_box_append(GTK_BOX(rowbox), icon);
        gtk_box_append(GTK_BOX(rowbox), lbl);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowbox);
        g_object_set_data(G_OBJECT(row), "page", (gpointer)items[i].page);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    g_signal_connect(list, "row-selected", G_CALLBACK(on_sidebar_row_selected), app);
    /* row-selected only fires when the selection actually changes -- also
     * handle row-activated (fires on every click) so clicking the page
     * that's already open still re-reads the mouse instead of doing nothing. */
    g_signal_connect(list, "row-activated", G_CALLBACK(on_sidebar_row_selected), app);
    gtk_widget_set_vexpand(list, TRUE);
    gtk_box_append(GTK_BOX(box), list);

    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(status_box, 8);
    gtk_widget_set_margin_bottom(status_box, 12);
    gtk_widget_set_margin_start(status_box, 16);
    gtk_widget_set_halign(status_box, GTK_ALIGN_START);
    app->status_dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(app->status_dot, "ajazz-status-dot");
    gtk_widget_add_css_class(app->status_dot, "disconnected");
    gtk_widget_set_valign(app->status_dot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(status_box), app->status_dot);
    app->status_lbl = GTK_LABEL(gtk_label_new(_("Disconnected")));
    gtk_widget_add_css_class(GTK_WIDGET(app->status_lbl), "ajazz-status-disconnected");
    gtk_widget_add_css_class(GTK_WIDGET(app->status_lbl), "caption");
    gtk_box_append(GTK_BOX(status_box), GTK_WIDGET(app->status_lbl));
    gtk_box_append(GTK_BOX(box), status_box);

    gtk_list_box_select_row(GTK_LIST_BOX(list),
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0));

    return box;
}

/* Tray callbacks -- see gui/tray.h for why this exists instead of a
 * library-based status icon. "Quit" is the only way to actually end the
 * app once a tray icon is present; closing the window just hides it
 * (see on_window_close_request()), Steam-style, so the mouse stays
 * configurable/watchable from the tray without a window open. */
static void on_tray_activate(gpointer user_data)
{
    App *app = user_data;
    gtk_widget_set_visible(GTK_WIDGET(app->win), TRUE);
    gtk_window_present(GTK_WINDOW(app->win));
}

static void on_tray_refresh(gpointer user_data)
{
    refresh_home((App *)user_data);
}

static void on_tray_quit(gpointer user_data)
{
    App *app = user_data;
    g_application_quit(G_APPLICATION(app->gtk_app));
}

static gboolean on_window_close_request(GtkWindow *win, gpointer user_data)
{
    (void)win; (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
    return TRUE; /* handled -- don't destroy the window or quit the app */
}

static void on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    App *app = user_data;
    app->gtk_app = gtk_app;
    /* keeps the GApplication alive with no visible windows, since closing
     * to the tray (see on_window_close_request()) would otherwise quit it */
    g_application_hold(G_APPLICATION(gtk_app));

    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_PREFER_DARK);

    /* Lets gtk_image_new_from_icon_name(AJAZZ_APP_ICON) and the window icon
     * resolve even when running straight from the build directory, not just
     * when installed under $(PREFIX)/share/icons/hicolor/... */
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), ICONDIR);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    app->win = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(gtk_app)));
    gtk_window_set_title(GTK_WINDOW(app->win), _("Ajazzy"));
    gtk_window_set_icon_name(GTK_WINDOW(app->win), AJAZZ_APP_ICON);
    gtk_window_set_default_size(GTK_WINDOW(app->win), 960, 720);
    g_signal_connect(app->win, "close-request", G_CALLBACK(on_window_close_request), app);

    /* created before build_sidebar() below, since selecting its initial
     * row synchronously fires a refresh that can call toast() */
    app->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());

    app->stack = ADW_VIEW_STACK(adw_view_stack_new());
    adw_view_stack_add_named(app->stack, build_home_page(app), "home");
    adw_view_stack_add_named(app->stack, build_buttons_page(app), "buttons");
    adw_view_stack_add_named(app->stack, build_mouse_config_page(app), "config");
    adw_view_stack_add_named(app->stack, build_lighting_page(app), "lighting");
    adw_view_stack_add_named(app->stack, build_macro_page(app), "macro");

    static const nav_item_t items[] = {
        {N_("Home"), "go-home-symbolic", "home"},
        {N_("Buttons"), "input-mouse-symbolic", "buttons"},
        {N_("Mouse settings"), "preferences-system-symbolic", "config"},
        {N_("Lighting"), "weather-clear-symbolic", "lighting"},
        {N_("Macro"), "input-keyboard-symbolic", "macro"},
    };
    GtkWidget *sidebar_content = build_sidebar(app, items, G_N_ELEMENTS(items));

    AdwToolbarView *sidebar_toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_set_content(sidebar_toolbar, sidebar_content);
    AdwNavigationPage *sidebar_page = ADW_NAVIGATION_PAGE(
        adw_navigation_page_new(GTK_WIDGET(sidebar_toolbar), _("Ajazzy")));

    AdwHeaderBar *content_header = ADW_HEADER_BAR(adw_header_bar_new());
    adw_header_bar_set_show_title(content_header, TRUE);

    AdwToolbarView *content_toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(content_toolbar, GTK_WIDGET(content_header));
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), GTK_WIDGET(app->stack));
    adw_toolbar_view_set_content(content_toolbar, scroller);
    AdwNavigationPage *content_page = ADW_NAVIGATION_PAGE(
        adw_navigation_page_new(GTK_WIDGET(content_toolbar), _("Ajazzy")));

    AdwNavigationSplitView *split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    adw_navigation_split_view_set_sidebar(split, sidebar_page);
    adw_navigation_split_view_set_content(split, content_page);
    adw_navigation_split_view_set_min_sidebar_width(split, 220);
    adw_navigation_split_view_set_max_sidebar_width(split, 260);

    adw_toast_overlay_set_child(app->toast_overlay, GTK_WIDGET(split));

    adw_application_window_set_content(app->win, GTK_WIDGET(app->toast_overlay));
    gtk_window_present(GTK_WINDOW(app->win));

    AjazzTrayCallbacks tray_cb = {
        .on_activate = on_tray_activate,
        .on_refresh = on_tray_refresh,
        .on_quit = on_tray_quit,
        .user_data = app,
    };
    /* Starts on the "missing" battery icon since we haven't talked to the
     * mouse yet; refresh_home() swaps it for the real charge level as
     * soon as it reads one. Uses a standard battery-*-symbolic name, not
     * our own app icon -- the shell/AppIndicator extension renders this
     * in its own process and won't see our ICONDIR search path unless
     * we're actually installed under a standard icon directory. */
    app->tray = ajazz_tray_new("battery-missing-symbolic", _("Ajazzy"), &tray_cb);

    refresh_home(app);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("ajazzy", LOCALEDIR);
    bind_textdomain_codeset("ajazzy", "UTF-8");
    textdomain("ajazzy");

    App app = {0};
    app.dev.fd = -1;
    app.dpi_selected = 0;
    init_key_functions();
    init_macro_actions();

    AdwApplication *gtk_app = adw_application_new("io.github.ajazzy.Gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &app);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    ajazz_tray_free(app.tray);
    if (app.connected)
        ajazz_close(&app.dev);

    g_object_unref(gtk_app);
    return status;
}
