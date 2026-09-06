/*
 * Shared state and cross-page glue for the Ajazzy GUI.
 *
 * The app is one process split across one file per page (gui/main.c is
 * the app shell -- window, CSS, sidebar, tray -- plus gui/page_*.c for
 * each AdwViewStack page). Every file needs the full App struct, and a
 * handful of helpers/pages are called from outside the file that
 * defines them (e.g. refresh_page() in main.c calls each page's
 * refresh_*() function) -- this header is the single place both sides
 * agree on those signatures. Anything used only within one .c file
 * stays `static` there rather than appearing here.
 */
#ifndef AJAZZY_GUI_APP_H
#define AJAZZY_GUI_APP_H

#include <adwaita.h>

#include "../protocol/hid.h"
#include "../protocol/transport.h"
#include "tray.h"

typedef struct {
    AdwApplicationWindow *win;
    GtkApplication *gtk_app;
    AjazzTray *tray;
    AdwToastOverlay *toast_overlay;
    AdwViewStack *stack;
    GtkListBox *sidebar_list;
    GtkLabel *status_lbl;
    GtkWidget *status_dot;
    GtkLabel *topbar_device_lbl;
    GtkWidget *topbar_status_dot;
    GtkLabel *topbar_status_lbl;

    ajazz_dev_t dev;
    gboolean connected;

    /* Every ajazz_query()/ajazz_send()/ajazz_send_and_wait() call in the
     * GUI goes through the *_mt() wrappers below, which hold this while
     * touching app->dev's fd. Without it, a page's background refresh
     * fetch (see the *_fetch_pending flags) and a synchronous Apply-
     * button click on the main thread could both read/write the same
     * fd at once. It does NOT protect app->dev's fields themselves
     * (vid/pid/mode/etc) from concurrent read -- those are only ever
     * written on the main thread (ensure_connected()), so plain reads
     * from a fetch thread are safe. */
    GMutex hid_lock;

    /* One flag per page's background refresh -- set right before
     * spawning that page's fetch GThread, cleared by its idle-apply
     * callback on the main thread. Guards against a burst of clicks (or
     * the periodic Home tick landing mid-fetch) piling up overlapping
     * threads for the same page; a fetch already in flight just means
     * the click's refresh request is dropped, since the in-flight one
     * will land momentarily anyway. */
    gboolean home_fetch_pending;
    gboolean config_fetch_pending;
    gboolean lighting_fetch_pending;
    gboolean buttons_fetch_pending;
    gboolean macro_fetch_pending;

    /* home */
    GtkLabel *lbl_name;
    GtkLabel *lbl_vidpid;
    GtkLabel *lbl_devid;
    GtkLabel *lbl_battery;
    GtkWidget *lbl_charging; /* "Charging" hint, visible only while mode=="USB" */
    GtkLabel *lbl_confirmed;
    GtkWidget *hero_status_dot;
    GtkLabel *hero_status_lbl;
    GtkWidget *batt_seg[4];
    GtkWidget *cap_badge[7];
    int battery_percent; /* -1 = unknown, else 0-100; drives the Home battery ring */

    /* Home page's compact "Button Mapping" readout -- kept in sync by
     * page_buttons.c's buttons_fetch_apply() so Home shows the real
     * current mapping without duplicating the fetch. */
    GtkLabel *home_key_lbl[AJAZZ_KEY_SLOTS];

    /* Home dashboard cards -- top-row stat widgets, refreshed straight
     * from the same shared state the Mouse/Lighting pages already keep
     * (dpi_cache/dpi_selected, rate_hz, rgb_effect's selection), not a
     * separate fetch. See refresh_home_cards() in page_home.c. */
    GtkLabel *home_dpi_val;
    GtkLabel *home_rate_val;
    GtkLabel *home_light_val;
    GtkWidget *home_batt_ring; /* GtkDrawingArea, draws battery_percent as a ring */
    GtkLabel *home_batt_pct_lbl;

    /* dpi / config */
    GtkToggleButton *dpi_btn[AJAZZ_DPI_STAGES];
    AdwSpinRow *dpi_value_spin;
    AdwActionRow *dpi_value_row;
    int dpi_selected;
    uint16_t dpi_cache[AJAZZ_DPI_STAGES];
    GtkColorDialogButton *dpi_color_btn; /* one button, follows dpi_selected */
    uint8_t dpi_color_cache[AJAZZ_DPI_STAGES][3];
    GtkDrawingArea *dpi_chart; /* "DPI Stages Preview" line chart, redrawn on any dpi_cache change */
    GtkToggleButton *rate_btn[4];
    int rate_hz; /* -1 = unknown; last applied/fetched report rate, mirrored on the Home card */
    GtkCheckButton *liftoff1;
    GtkCheckButton *liftoff2;
    AdwComboRow *sleep_combo;
    AdwSwitchRow *idle_light_switch;

    /* lighting */
    AdwComboRow *rgb_effect;
    AdwSpinRow *rgb_brightness;
    AdwActionRow *rgb_speed_row;
    AdwSpinRow *rgb_speed;
    AdwActionRow *rgb_soft_color_row; /* visible for Breathing and for picking the next Custom swatch */
    GtkColorDialogButton *rgb_soft_color_btn;
    uint8_t rgb_soft_color[3]; /* Breathing's base colour / next colour to add to a Custom sequence */
    guint rgb_soft_timer_id;  /* 0 when no software effect loop is running */
    int rgb_soft_effect_kind; /* which RGB_SOFT_* is running, valid while rgb_soft_timer_id != 0 */
    double rgb_soft_phase;
    int rgb_soft_fail_count;

    /* Custom (software) effect: a user-built sequence of colours the
     * tick loop plays through, either cross-fading between them or
     * hard-cutting -- this is the "make your own effect" control the
     * fixed Off/Constant/hardware-cycle/Breathing/Rainbow list can't
     * cover on its own. */
    uint8_t rgb_custom_colors[8][3];
    int rgb_custom_count;
    int rgb_custom_transition; /* 0 = fade between colours, 1 = hard cut */
    GtkWidget *rgb_custom_swatches_box;
    AdwActionRow *rgb_custom_row; /* wraps rgb_custom_swatches_box, visible only for Custom */
    AdwComboRow *rgb_custom_transition_row;

    /* app settings (gui/page_settings.c) */
    int settings_theme;             /* 0 = system, 1 = light, 2 = dark */
    gboolean settings_autostart;
    gboolean settings_low_battery_warning;
    gboolean settings_low_battery_was_ok; /* tracks the >=20% -> <20% crossing */

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

/* gui/main.c -- app-wide helpers used by every page */
GtkWidget *make_logo_widget(int size);
void toast(App *app, const char *msg);
void set_status_widgets(GtkWidget *dot, GtkLabel *lbl, gboolean connected);
gboolean ensure_connected(App *app);
void free_closure_data(gpointer data, GClosure *closure);
/* Selects the sidebar row for `page_name` (see build_sidebar()'s nav_item_t
 * list in main.c), which switches the visible AdwViewStack child and
 * re-reads that page from the mouse -- the same path a manual sidebar
 * click takes. Used by Home's per-card "go to this page" buttons. */
void switch_to_page(App *app, const char *page_name);

/* Thread-safe wrappers around protocol/transport.h's ajazz_query() /
 * ajazz_send() / ajazz_send_and_wait() -- every call site in the gui/
 * sources
 * must go through these, not the raw functions, since a page's
 * background refresh fetch (see the *_fetch_pending flags above) runs
 * on a GThread while Apply-button handlers call these synchronously on
 * the main thread; both touch the same app->dev.fd. See the comment on
 * App::hid_lock. Safe to call from either thread -- never touch a GTK
 * widget while holding the lock or from inside a fetch thread, only
 * from a main-thread idle callback after the fetch returns. */
static inline int ajazz_query_mt(App *app, uint8_t query_id, ajazz_report_t *reply, int timeout_ms)
{
    g_mutex_lock(&app->hid_lock);
    int rc = ajazz_query(&app->dev, query_id, reply, timeout_ms);
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

static inline int ajazz_send_mt(App *app, const ajazz_report_t *r)
{
    g_mutex_lock(&app->hid_lock);
    int rc = ajazz_send(&app->dev, r);
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

static inline int ajazz_send_and_wait_mt(App *app, const ajazz_report_t *r, int timeout_ms)
{
    g_mutex_lock(&app->hid_lock);
    int rc = ajazz_send_and_wait(&app->dev, r, timeout_ms);
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

/* Non-blocking variant for a periodic effect ticking on the MAIN thread
 * (a plain g_timeout_add callback, not a GThread) -- a page's
 * background fetch can hold hid_lock for up to its query's timeout
 * (up to 1000ms), and a normal ajazz_send_mt() tick landing during that
 * window would stall the whole UI waiting for the lock, which is
 * exactly the freeze this exists to prevent. Returns 1 (skipped, lock
 * was busy -- just try again next tick) rather than blocking; 0/-1 have
 * ajazz_send()'s normal meaning. */
static inline int ajazz_send_trylock_mt(App *app, const ajazz_report_t *r)
{
    if (!g_mutex_trylock(&app->hid_lock))
        return 1;
    int rc = ajazz_send(&app->dev, r);
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

/* gui/page_buttons.c and gui/page_macro.c each debounce their own
 * "did this remap/macro survive a device round-trip" re-read, but both
 * need to be kicked off from the other page too (a macro write flips a
 * key table slot into "plays a macro" mode, which the Buttons page
 * should reflect, and vice versa) -- see the callers in page_buttons.c/
 * page_macro.c for why. */
void refresh_buttons_delayed(App *app);
void refresh_macro_status_delayed(App *app);

/* gui/page_home.c */
GtkWidget *build_home_page(App *app);
void refresh_home(App *app);

/* gui/page_mouse.c */
GtkWidget *build_mouse_config_page(App *app);
void refresh_config(App *app);
/* Selects DPI stage `stage` (0-based) in the already-built Mouse settings
 * page -- used by Home's DPI card chips to jump-select a stage before
 * switching to that page. Safe to call even before the page has ever been
 * shown, since build_mouse_config_page() runs once at startup. */
void mouse_page_select_stage(App *app, int stage);
/* Applies a full DPI table + colour table from a saved profile: updates
 * app->dpi_cache/dpi_color_cache, sends both SET commands, and refreshes
 * the stage grid/chart/swatch. Returns TRUE if both sends succeeded. */
gboolean mouse_page_apply_profile_dpi(App *app, const uint16_t dpi[AJAZZ_DPI_STAGES],
    const uint8_t colors[AJAZZ_DPI_STAGES][3], int selected_stage);
/* Applies a saved report rate (125/250/500/1000 Hz) -- maps to the
 * nearest toggle, sends it, and updates app->rate_hz. Returns TRUE on a
 * successful send. */
gboolean mouse_page_apply_profile_rate(App *app, int hz);

/* gui/page_lighting.c */
GtkWidget *build_lighting_page(App *app);
void refresh_lighting(App *app);
/* Applies a saved lighting configuration: sets the effect/brightness/
 * speed/breathing-colour widgets to the given values and immediately
 * applies them, same as picking them by hand and clicking "Apply to
 * mouse". `color` is only used for RGB_SOFT_BREATHING (see the
 * RGB_SOFT_* enum in page_lighting.c); pass NULL if not applicable. */
void lighting_apply_profile(App *app, int effect, int brightness, int speed, const uint8_t color[3]);

/* gui/page_buttons.c */
GtkWidget *build_buttons_page(App *app);
void refresh_buttons(App *app);
void init_key_functions(void);
/* Applies a saved button table verbatim -- `raw` is AJAZZ_KEY_SLOTS
 * entries of the same 3-byte-per-slot wire format AJAZZ_QUERY_KEY_TABLE
 * returns (marker, param1, param2), exactly what a profile save reads
 * back with a fresh query. Re-reads the mouse afterward (refresh_buttons())
 * so the dropdowns/Home readout reflect what was actually written, rather
 * than assuming the raw bytes round-trip losslessly through the UI. */
gboolean buttons_apply_profile_table(App *app, const uint8_t raw[AJAZZ_KEY_SLOTS][3]);

/* gui/page_macro.c */
GtkWidget *build_macro_page(App *app);
void refresh_macro_status(App *app);
void init_macro_actions(void);

/* gui/page_profiles.c -- local (PC-side) configuration snapshots, saved
 * as JSON under ~/.config/ajazzy/profiles/. Not a mouse feature: this
 * protocol has no on-device profile storage, see the page's own
 * subtitle and build_profiles_page()'s comment. */
GtkWidget *build_profiles_page(App *app);
void refresh_profiles(App *app);

/* gui/page_settings.c -- app-level preferences, all local to this PC
 * (~/.config/ajazzy/settings.json): theme, launch-on-login, low-battery
 * warning, and wiping this app's own local data. None of this is a
 * mouse/firmware feature. */
GtkWidget *build_settings_page(App *app);
/* Loads settings.json (if present) and applies theme/autostart state at
 * startup, before the window is shown -- called once from main(). */
void settings_load_and_apply(App *app);
/* Called from periodic_refresh_cb() every tick with the latest battery
 * reading; fires a one-shot toast on the crossing into <20% while
 * unplugged, if the user has that warning enabled. */
void settings_check_low_battery(App *app, int percent, gboolean charging);

#endif
