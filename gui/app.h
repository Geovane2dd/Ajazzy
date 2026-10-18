/*
 * Shared state and cross-page glue for the Ajazzy GUI.
 *
 * One process, one file per page. gui/main.c is the shell (window, sidebar,
 * connection manager, navigation); gui/page_*.c each build one page and keep
 * their own widgets file-static. What lives in App is only what more than
 * one page needs: the device handle, the cached device state that Home and
 * Profiles read, and the app settings.
 *
 * Threading rule: every HID transaction goes through the *_mt() wrappers
 * and never runs on the GTK main thread. Pages use app_async() -- the work
 * function runs on a worker thread (no GTK calls allowed in it), the done
 * function runs back on the main thread.
 */
#ifndef AJAZZY_GUI_APP_H
#define AJAZZY_GUI_APP_H

#include <adwaita.h>
#include <glib/gi18n.h>

#include "../protocol/hid.h"
#include "../protocol/transport.h"
#include "tray.h"
#include "ui.h"

#define AJAZZY_VERSION "0.2.0"
#define AJAZZY_GITHUB_URL "https://github.com/Geovane2dd/Ajazzy"
#define AJAZZ_APP_ID "io.github.ajazzy.Gui"

/* ---- cached device state shared across pages ---- */

#define LIGHT_PALETTE_MAX 7

typedef struct {
    int effect;       /* LightEffect id, see gui/page_lighting.c */
    int brightness;   /* 0-4 */
    int speed;        /* 0-4 */
    uint8_t color[3]; /* single colour (constant / breathing / software modes) */
    uint8_t palette[LIGHT_PALETTE_MAX][3];
    int palette_n;
    int soft_transition; /* software custom effect: 0 fade, 1 cut */
} AppLight;

typedef struct {
    int liftoff_mm;          /* 1 or 2 */
    gboolean angle_snap;
    gboolean ripple;
    gboolean motion_sync;
    int sleep_seconds;
    gboolean wake_on_move;
    gboolean idle_light_off;
    int debounce_ms;
    gboolean valid;
} AppSensor;

/* What Battery saver changed, so turning it off puts everything back. */
typedef struct {
    gboolean valid;
    int rate_hz;
    int sleep_seconds;
    gboolean idle_light_off;
    AppLight light;
} AppSaverSnapshot;

typedef struct {
    char name[48];
    ajazz_macro_step_t steps[AJAZZ_MACRO_MAX_CHUNKED_STEPS];
    int n_steps;
    int loop_mode;  /* see AjazzMacroLoop in protocol/hid.h */
    int loop_count;
} AppMacro;

typedef struct App {
    AdwApplicationWindow *win;
    GtkApplication *gtk_app;
    AjazzTray *tray;
    AdwToastOverlay *toast_overlay;
    AdwViewStack *stack;
    GtkStack *content_stack; /* "pages" or "offline" */
    GtkListBox *nav;
    GtkWidget *header_title;
    GtkWidget *side_dot;
    GtkLabel *side_name;
    GtkLabel *side_state;
    GtkWidget *side_battery;
    GtkWidget *offline_status;
    AdwBanner *asleep_banner;
    gboolean asleep;   /* connected to the dongle but the mouse isn't answering */
    int access_state;  /* ajazz_access_t from the last failed connect: plugged in but not permitted? */
    const char *current_page;

    ajazz_dev_t dev;
    gboolean connected;
    gboolean connect_busy;
    int tick;

    /* Held across every HID transaction and across open/close. */
    GMutex hid_lock;

    /* ---- shared device state (written by the owning page on the main
     * thread; read by Home, the tray and Profiles) ---- */
    int battery_percent; /* -1 unknown */
    gboolean charging;
    uint16_t dpi_cache[AJAZZ_DPI_STAGES];
    uint8_t dpi_color_cache[AJAZZ_DPI_STAGES][3];
    int dpi_selected;
    int rate_hz; /* -1 unknown */
    AppLight light;
    AppSensor sensor;
    int dpi_count;      /* low nibble of the DPI table: stages the DPI button cycles through */
    gboolean saver_active;
    AppSaverSnapshot saver_prev;
    uint8_t key_table[AJAZZ_KEY_SLOTS][3];
    gboolean key_table_valid;
    GPtrArray *macro_lib;                  /* AppMacro*, the saved macro library */
    char macro_slot_name[AJAZZ_KEY_SLOTS][48]; /* library macro last written to each button */

    /* ---- settings (gui/page_settings.c) ---- */
    int settings_theme; /* 0 system, 1 light, 2 dark */
    gboolean settings_autostart;
    gboolean settings_start_minimized;
    gboolean settings_close_to_tray;
    gboolean settings_low_battery_warning;
    int settings_low_battery_threshold;
    gboolean settings_low_battery_was_ok;
    gboolean settings_profile_on_start; /* re-apply last profile when mouse connects */
    gboolean settings_auto_saver;       /* switch Battery saver on automatically when low */
    char settings_last_profile[64];
} App;

/* ---- gui/main.c ---- */
void toast(App *app, const char *msg);
void toastf(App *app, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void free_closure_data(gpointer data, GClosure *closure);
/* Switches to a page by id: home, buttons, performance, lighting, macros,
 * profiles, settings. */
void switch_to_page(App *app, const char *page_name);
/* Re-reads everything from the mouse and refreshes every page. */
void app_refresh_all(App *app);

typedef void (*AppWorkFn)(App *app, gpointer data);
typedef void (*AppDoneFn)(App *app, gpointer data);
/* Runs work(app, data) on a worker thread, then done(app, data) on the main
 * thread (done may be NULL). free_data is called after done. */
void app_async(App *app, AppWorkFn work, AppDoneFn done, gpointer data, GDestroyNotify free_data);

/* The only way pages learn that the connection state changed. */
void app_connection_changed(App *app);

/* Thread-safe wrappers over protocol/transport.h. Safe from any thread; the
 * work happens under hid_lock. */
static inline int ajazz_query_mt(App *app, uint8_t query_id, ajazz_report_t *reply, int timeout_ms)
{
    g_mutex_lock(&app->hid_lock);
    int rc = app->dev.fd >= 0 ? ajazz_query(&app->dev, query_id, reply, timeout_ms) : -1;
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

static inline int ajazz_send_mt(App *app, const ajazz_report_t *r)
{
    g_mutex_lock(&app->hid_lock);
    int rc = app->dev.fd >= 0 ? ajazz_send(&app->dev, r) : -1;
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

static inline int ajazz_send_and_wait_mt(App *app, const ajazz_report_t *r, int timeout_ms)
{
    g_mutex_lock(&app->hid_lock);
    int rc = app->dev.fd >= 0 ? ajazz_send_and_wait(&app->dev, r, timeout_ms) : -1;
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

/* Non-blocking send for a periodic effect ticking on the main thread.
 * Returns 1 when the lock was busy (skip this frame). */
static inline int ajazz_send_trylock_mt(App *app, const ajazz_report_t *r)
{
    if (!g_mutex_trylock(&app->hid_lock))
        return 1;
    int rc = app->dev.fd >= 0 ? ajazz_send(&app->dev, r) : -1;
    g_mutex_unlock(&app->hid_lock);
    return rc;
}

/* ---- pages ---- */
GtkWidget *build_home_page(App *app);
void refresh_home(App *app);
/* Called by the connection manager with the latest battery reading. */
void home_set_battery(App *app, int percent, gboolean charging);

GtkWidget *build_buttons_page(App *app);
void refresh_buttons(App *app);
gboolean buttons_apply_table(App *app, const uint8_t raw[AJAZZ_KEY_SLOTS][3]);
/* Human-readable function of button `slot` from the cached table. */
void buttons_describe(App *app, int slot, char *out, size_t n);

GtkWidget *build_performance_page(App *app);
void refresh_performance(App *app);
gboolean performance_apply_profile(App *app, const uint16_t dpi[AJAZZ_DPI_STAGES],
    const uint8_t colors[AJAZZ_DPI_STAGES][3], int selected_stage, int rate_hz, const AppSensor *sensor);
/* Battery saver (gui/page_performance.c). */
void powersaver_set(App *app, gboolean on, gboolean automatic);
void powersaver_battery_update(App *app, int percent, gboolean charging);
void performance_select_stage(App *app, int stage);

GtkWidget *build_lighting_page(App *app);
void refresh_lighting(App *app);
gboolean lighting_apply(App *app, const AppLight *l);
void lighting_shutdown(App *app); /* stops any software effect loop */
const char *lighting_effect_name(int effect);
/* Lighting effect ids (index into the gallery in gui/page_lighting.c). */
enum { LFX_OFF, LFX_STEADY, LFX_BREATHING, LFX_FLOW, LFX_NEON, LFX_WAVE, LFX_SOFT_CYCLE, LFX_SOFT_FADE, LFX_COUNT };

GtkWidget *build_macros_page(App *app);
void refresh_macros(App *app);
/* Writes macro `m` to the mouse and binds it to button `slot` (0-based).
 * Asynchronous; failure toasts. Returns FALSE if the mouse isn't connected. */
gboolean macros_assign(App *app, int slot, const AppMacro *m);
/* The library is loaded once at startup and saved on exit and after edits. */
void macros_load_library(App *app);
void macros_save_library(App *app);
AppMacro *macros_find(App *app, const char *name);

GtkWidget *build_profiles_page(App *app);
void refresh_profiles(App *app);
/* Applies a saved profile by file name (without directory). */
gboolean profiles_apply_by_name(App *app, const char *name);
/* Names of every saved profile, newline separated. Caller frees. */
GPtrArray *profiles_list_names(void);

GtkWidget *build_settings_page(App *app);
void settings_load_and_apply(App *app);
void settings_save(App *app);
void settings_check_low_battery(App *app, int percent, gboolean charging);

#endif
