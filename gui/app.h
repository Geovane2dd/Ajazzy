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

    /* dpi / config */
    GtkToggleButton *dpi_btn[AJAZZ_DPI_STAGES];
    AdwSpinRow *dpi_value_spin;
    AdwActionRow *dpi_value_row;
    int dpi_selected;
    uint16_t dpi_cache[AJAZZ_DPI_STAGES];
    GtkColorDialogButton *dpi_color_btn[AJAZZ_DPI_STAGES];
    uint8_t dpi_color_cache[AJAZZ_DPI_STAGES][3];
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
    guint rgb_soft_timer_id;  /* 0 when the software colour cycle isn't running */
    double rgb_soft_phase;
    int rgb_soft_fail_count;

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

/* gui/page_lighting.c */
GtkWidget *build_lighting_page(App *app);
void refresh_lighting(App *app);

/* gui/page_buttons.c */
GtkWidget *build_buttons_page(App *app);
void refresh_buttons(App *app);
void init_key_functions(void);

/* gui/page_macro.c */
GtkWidget *build_macro_page(App *app);
void refresh_macro_status(App *app);
void init_macro_actions(void);

#endif
