/*
 * Performance: DPI stages (+ indicator colours), report rate, sensor tuning,
 * power, and the Battery saver mode.
 *
 * Every control applies the moment you change it (sliders and number fields
 * are debounced so a drag is one write, not fifty). Failures toast; success
 * is silent. The values on screen are always what the mouse reported on the
 * last refresh -- there is no "unsaved" state to get out of sync.
 *
 * Wire formats are in protocol/hid.h; most of them were recovered by
 * decompiling the official driver (see docs/reverse-engineering.md).
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "jsonutil.h"
#include "../devices/device.h"

static const int RATE_HZ[4] = {125, 250, 500, 1000};
static const struct { const char *label; int seconds; } SLEEP_PRESETS[] = {
    {N_("10 seconds"), 10}, {N_("30 seconds"), 30}, {N_("1 minute"), 60}, {N_("2 minutes"), 120},
    {N_("5 minutes"), 300}, {N_("10 minutes"), 600}, {N_("20 minutes"), 1200}, {N_("30 minutes"), 1800},
};
#define N_SLEEP G_N_ELEMENTS(SLEEP_PRESETS)

#define DPI_MIN 100
#define DPI_MAX 26000

typedef struct {
    GtkWidget *stage_btn[AJAZZ_DPI_STAGES];
    GtkWidget *stage_dot[AJAZZ_DPI_STAGES];
    GtkWidget *stage_val[AJAZZ_DPI_STAGES];
    GtkAdjustment *dpi_adj;
    GtkWidget *dpi_title;
    GtkWidget *color_btn;
    AdwSpinRow *count_row;
    GtkWidget *rate_seg;
    GtkWidget *liftoff_seg;
    AdwSwitchRow *angle_sw, *ripple_sw, *msync_sw;
    AdwComboRow *sleep_combo;
    AdwSwitchRow *wake_sw, *idle_sw;
    AdwSpinRow *debounce_row;
    AdwSwitchRow *saver_sw, *saver_auto_sw;
    GtkWidget *saver_chip;
    UiDebounce *dpi_deb, *color_deb, *power_deb;
} PerfUI;

static PerfUI P;
static gboolean loading;
static gboolean dpi_loaded;
static ajazz_report_t g_power_raw;
static gboolean g_power_have;
static gboolean g_saver_auto; /* current saver state was switched on automatically */

/* ---------------- async plumbing ---------------- */

typedef struct {
    ajazz_report_t req[3];
    int n;
    int rc;
    char *fail;
} SendJob;

static void send_work(App *app, gpointer data)
{
    SendJob *j = data;
    j->rc = 0;
    for (int i = 0; i < j->n; i++) {
        if (ajazz_send_and_wait_mt(app, &j->req[i], 1000) != 0) {
            j->rc = -1;
            break;
        }
    }
}

static void send_done(App *app, gpointer data)
{
    SendJob *j = data;
    if (j->rc != 0 && j->fail)
        toast(app, j->fail);
}

static void send_free(gpointer data)
{
    SendJob *j = data;
    g_free(j->fail);
    g_free(j);
}

static void send_async(App *app, const ajazz_report_t *r, const char *fail)
{
    if (!app->connected)
        return;
    SendJob *j = g_new0(SendJob, 1);
    j->req[0] = *r;
    j->n = 1;
    j->fail = fail ? g_strdup(fail) : NULL;
    app_async(app, send_work, send_done, j, send_free);
}

/* ---------------- DPI ---------------- */

static void draw_dot(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
    (void)area;
    App *app = ((gpointer *)data)[0];
    int i = GPOINTER_TO_INT(((gpointer *)data)[1]);
    cairo_arc(cr, w / 2.0, h / 2.0, MIN(w, h) / 2.0 - 0.5, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, app->dpi_color_cache[i][0] / 255.0, app->dpi_color_cache[i][1] / 255.0,
        app->dpi_color_cache[i][2] / 255.0);
    cairo_fill(cr);
}

static void update_dpi_ui(App *app)
{
    loading = TRUE;
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        char v[16];
        snprintf(v, sizeof(v), "%u", app->dpi_cache[i]);
        gtk_label_set_text(GTK_LABEL(P.stage_val[i]), v);
        gtk_widget_queue_draw(P.stage_dot[i]);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.stage_btn[i]), i == app->dpi_selected);
    }
    int s = app->dpi_selected;
    gtk_adjustment_set_value(P.dpi_adj, app->dpi_cache[s] ? app->dpi_cache[s] : DPI_MIN);
    char t[48];
    snprintf(t, sizeof(t), _("Stage %d"), s + 1);
    gtk_label_set_text(GTK_LABEL(P.dpi_title), t);
    GdkRGBA rgba;
    ui_rgba_from_rgb(&rgba, app->dpi_color_cache[s]);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(P.color_btn), &rgba);
    if (app->dpi_count >= 1 && app->dpi_count <= AJAZZ_DPI_STAGES)
        adw_spin_row_set_value(P.count_row, app->dpi_count);
    loading = FALSE;
}

/* The table write also carries the active stage, so selecting a stage here
 * activates it on the mouse, exactly like the official driver. */
static void send_dpi_table(App *app)
{
    if (!dpi_loaded)
        return;
    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, app->dpi_cache, (uint8_t)app->dpi_selected, (uint8_t)app->dpi_count);
    send_async(app, &req, _("Couldn't change the DPI."));
}

static void send_dpi_colors(App *app)
{
    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);
    send_async(app, &req, _("Couldn't change the indicator colour."));
}

static gboolean dpi_deb_fire(gpointer user_data)
{
    send_dpi_table(user_data);
    return G_SOURCE_REMOVE;
}

static gboolean color_deb_fire(gpointer user_data)
{
    send_dpi_colors(user_data);
    return G_SOURCE_REMOVE;
}

void performance_select_stage(App *app, int stage)
{
    if (stage < 0 || stage >= AJAZZ_DPI_STAGES)
        return;
    app->dpi_selected = stage;
    update_dpi_ui(app);
    send_dpi_table(app);
}

static void on_stage_clicked(GtkToggleButton *b, gpointer user_data)
{
    gpointer *d = user_data;
    App *app = d[0];
    int i = GPOINTER_TO_INT(d[1]);
    if (loading)
        return;
    if (!gtk_toggle_button_get_active(b)) {
        /* clicking the selected stage again must not leave nothing selected */
        gtk_toggle_button_set_active(b, TRUE);
        return;
    }
    if (i == app->dpi_selected)
        return;
    performance_select_stage(app, i);
}

static void on_dpi_value_changed(GtkAdjustment *adj, gpointer user_data)
{
    App *app = user_data;
    if (loading)
        return;
    int v = ((int)(gtk_adjustment_get_value(adj) + 50) / 100) * 100;
    if (v < DPI_MIN) v = DPI_MIN;
    if (v > DPI_MAX) v = DPI_MAX;
    app->dpi_cache[app->dpi_selected] = (uint16_t)v;
    char s[16];
    snprintf(s, sizeof(s), "%d", v);
    gtk_label_set_text(GTK_LABEL(P.stage_val[app->dpi_selected]), s);
    ui_debounce_poke(P.dpi_deb);
}

static void on_quick_dpi(GtkButton *b, gpointer user_data)
{
    (void)user_data;
    gtk_adjustment_set_value(P.dpi_adj, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "dpi")));
}

static void on_color_changed(GObject *btn, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    if (loading)
        return;
    ui_rgb_from_rgba(app->dpi_color_cache[app->dpi_selected],
        gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(btn)));
    gtk_widget_queue_draw(P.stage_dot[app->dpi_selected]);
    ui_debounce_poke(P.color_deb);
}

static void on_count_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    if (loading)
        return;
    app->dpi_count = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    ui_debounce_poke(P.dpi_deb);
}

static void on_dpi_defaults(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    memcpy(app->dpi_cache, AJAZZ_DEFAULT_DPI, sizeof(app->dpi_cache));
    memcpy(app->dpi_color_cache, AJAZZ_DEFAULT_DPI_COLOR, sizeof(app->dpi_color_cache));
    app->dpi_selected = 0;
    update_dpi_ui(app);
    send_dpi_table(app);
    send_dpi_colors(app);
}

/* ---------------- report rate ---------------- */

static void on_rate_selected(int index, gpointer user_data)
{
    App *app = user_data;
    if (loading)
        return;
    app->rate_hz = RATE_HZ[index];
    ajazz_report_t req;
    ajazz_build_set_report_rate(&req, (uint8_t)index);
    send_async(app, &req, _("Couldn't change the report rate."));
}

/* ---------------- sensor ---------------- */

static void send_sensor(App *app)
{
    ajazz_sensor_t s = {
        .lift_off = (uint8_t)app->sensor.liftoff_mm,
        .angle_snap = app->sensor.angle_snap,
        .ripple_control = app->sensor.ripple,
        .motion_sync = app->sensor.motion_sync,
    };
    ajazz_report_t req;
    ajazz_build_set_sensor_perf(&req, &s);
    send_async(app, &req, _("Couldn't change the sensor settings."));
}

static void on_liftoff_selected(int index, gpointer user_data)
{
    App *app = user_data;
    if (loading) return;
    app->sensor.liftoff_mm = index + 1;
    send_sensor(app);
}

#define SENSOR_SWITCH(fn, field)                                                 \
    static void fn(GObject *sw, GParamSpec *p, gpointer user_data)                \
    {                                                                             \
        (void)p;                                                                  \
        App *app = user_data;                                                     \
        if (loading) return;                                                      \
        app->sensor.field = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));        \
        send_sensor(app);                                                         \
    }
SENSOR_SWITCH(on_angle_changed, angle_snap)
SENSOR_SWITCH(on_ripple_changed, ripple)
SENSOR_SWITCH(on_msync_changed, motion_sync)

/* ---------------- power ---------------- */

static void send_power(App *app)
{
    if (!g_power_have)
        return;
    ajazz_report_t req;
    ajazz_build_power_from_query(&req, &g_power_raw);
    ajazz_power_set_sleep_seconds(&req, (uint16_t)app->sensor.sleep_seconds);
    ajazz_power_set_wake_on_motion(&req, app->sensor.wake_on_move);
    ajazz_power_set_light_idle_off(&req, app->sensor.idle_light_off);
    ajazz_power_set_debounce_ms(&req, (uint8_t)app->sensor.debounce_ms);
    g_power_raw = req; /* keep the cache in step so later edits stay read-modify-write */
    send_async(app, &req, _("Couldn't change the power settings."));
}

static gboolean power_deb_fire(gpointer user_data)
{
    send_power(user_data);
    return G_SOURCE_REMOVE;
}

static void on_sleep_changed(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    guint i = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    if (i < N_SLEEP)
        app->sensor.sleep_seconds = SLEEP_PRESETS[i].seconds;
    send_power(app);
}

static void on_wake_changed(GObject *sw, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->sensor.wake_on_move = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));
    send_power(app);
}

static void on_idle_changed(GObject *sw, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->sensor.idle_light_off = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));
    send_power(app);
}

static void on_debounce_changed(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->sensor.debounce_ms = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    ui_debounce_poke(P.power_deb);
}

/* ---------------- refresh ---------------- */

typedef struct {
    gboolean dpi_ok, color_ok, rate_ok, sens_ok, power_ok;
    ajazz_report_t dpi, color, rate, sens, power;
} Fetch;

static void fetch_work(App *app, gpointer data)
{
    Fetch *f = data;
    f->dpi_ok = ajazz_query_mt(app, AJAZZ_QUERY_DPI_TABLE, &f->dpi, 1000) == 0;
    f->color_ok = ajazz_query_mt(app, AJAZZ_QUERY_DPI_COLOR_TABLE, &f->color, 1000) == 0;
    f->rate_ok = ajazz_query_mt(app, AJAZZ_QUERY_REPORT_RATE, &f->rate, 1000) == 0;
    f->sens_ok = ajazz_query_mt(app, AJAZZ_QUERY_SENSOR_PERF, &f->sens, 1000) == 0;
    f->power_ok = ajazz_query_mt(app, AJAZZ_QUERY_POWER, &f->power, 1000) == 0;
}

static void saver_sync_ui(App *app);

static void fetch_done(App *app, gpointer data)
{
    Fetch *f = data;
    if (!app->connected)
        return;

    if (f->dpi_ok) {
        ajazz_parse_dpi_table(&f->dpi, app->dpi_cache);
        int a = ajazz_dpi_table_active_stage(&f->dpi);
        app->dpi_selected = (a >= 0 && a < AJAZZ_DPI_STAGES) ? a : 0;
        app->dpi_count = ajazz_dpi_table_count(&f->dpi);
        dpi_loaded = TRUE;
    }
    if (f->color_ok)
        ajazz_parse_dpi_color_table(&f->color, app->dpi_color_cache);
    update_dpi_ui(app);

    loading = TRUE;
    if (f->rate_ok) {
        int hz = ajazz_report_rate_hz(f->rate.payload[0]);
        for (int i = 0; i < 4; i++)
            if (RATE_HZ[i] == hz) {
                app->rate_hz = hz;
                ui_segmented_set(P.rate_seg, i);
            }
    }
    if (f->sens_ok) {
        ajazz_sensor_t s;
        ajazz_parse_sensor_perf(&f->sens, &s);
        app->sensor.liftoff_mm = s.lift_off;
        app->sensor.angle_snap = s.angle_snap;
        app->sensor.ripple = s.ripple_control;
        app->sensor.motion_sync = s.motion_sync;
        ui_segmented_set(P.liftoff_seg, s.lift_off - 1);
        adw_switch_row_set_active(P.angle_sw, s.angle_snap);
        adw_switch_row_set_active(P.ripple_sw, s.ripple_control);
        adw_switch_row_set_active(P.msync_sw, s.motion_sync);
    }
    if (f->power_ok) {
        g_power_raw = f->power;
        g_power_have = TRUE;
        app->sensor.sleep_seconds = f->power.payload[0] * 10;
        app->sensor.wake_on_move = f->power.payload[1] != 0;
        app->sensor.idle_light_off = f->power.payload[2] != 0;
        app->sensor.debounce_ms = f->power.payload[3];
        app->sensor.valid = TRUE;

        guint best = 0;
        int bd = G_MAXINT;
        for (guint i = 0; i < N_SLEEP; i++) {
            int d = abs(SLEEP_PRESETS[i].seconds - app->sensor.sleep_seconds);
            if (d < bd) { bd = d; best = i; }
        }
        adw_combo_row_set_selected(P.sleep_combo, best);
        adw_switch_row_set_active(P.wake_sw, app->sensor.wake_on_move);
        adw_switch_row_set_active(P.idle_sw, app->sensor.idle_light_off);
        adw_spin_row_set_value(P.debounce_row, app->sensor.debounce_ms);
    }
    loading = FALSE;
    saver_sync_ui(app);
    refresh_home(app);
}

void refresh_performance(App *app)
{
    if (!app->connected)
        return;
    app_async(app, fetch_work, fetch_done, g_new0(Fetch, 1), g_free);
}

/* ---------------- profile apply ---------------- */

gboolean performance_apply_profile(App *app, const uint16_t dpi[AJAZZ_DPI_STAGES],
    const uint8_t colors[AJAZZ_DPI_STAGES][3], int selected_stage, int rate_hz, const AppSensor *sensor)
{
    if (!app->connected)
        return FALSE;
    memcpy(app->dpi_cache, dpi, sizeof(app->dpi_cache));
    memcpy(app->dpi_color_cache, colors, sizeof(app->dpi_color_cache));
    app->dpi_selected = (selected_stage >= 0 && selected_stage < AJAZZ_DPI_STAGES) ? selected_stage : 0;
    dpi_loaded = TRUE;

    SendJob *j = g_new0(SendJob, 1);
    ajazz_build_set_dpi_table(&j->req[0], app->dpi_cache, (uint8_t)app->dpi_selected, (uint8_t)app->dpi_count);
    ajazz_build_set_dpi_colors(&j->req[1], app->dpi_color_cache);
    int idx = 3;
    for (int i = 0; i < 4; i++)
        if (RATE_HZ[i] == rate_hz) idx = i;
    ajazz_build_set_report_rate(&j->req[2], (uint8_t)idx);
    app->rate_hz = RATE_HZ[idx];
    j->n = 3;
    j->fail = g_strdup(_("Some performance settings couldn't be applied."));
    app_async(app, send_work, send_done, j, send_free);

    if (sensor && sensor->valid) {
        app->sensor = *sensor;
        send_sensor(app);
        send_power(app);
    }
    update_dpi_ui(app);
    return TRUE;
}

/* ---------------- battery saver ---------------- */

static char *saver_file(void)
{
    char *dir = ajazzy_config_subdir(".");
    if (!dir) return NULL;
    char *p = g_build_filename(dir, "saver.json", NULL);
    g_free(dir);
    return p;
}

static void saver_save(App *app)
{
    char *path = saver_file();
    if (!path) return;
    GString *o = g_string_new("{\n  ");
    json_put_int(o, "active", app->saver_active); g_string_append(o, ",\n  ");
    json_put_int(o, "auto", g_saver_auto); g_string_append(o, ",\n  ");
    json_put_int(o, "rate", app->saver_prev.rate_hz); g_string_append(o, ",\n  ");
    json_put_int(o, "sleep", app->saver_prev.sleep_seconds); g_string_append(o, ",\n  ");
    json_put_int(o, "idle", app->saver_prev.idle_light_off); g_string_append(o, ",\n  ");
    const AppLight *l = &app->saver_prev.light;
    json_put_int(o, "l_effect", l->effect); g_string_append(o, ",\n  ");
    json_put_int(o, "l_bri", l->brightness); g_string_append(o, ",\n  ");
    json_put_int(o, "l_speed", l->speed); g_string_append(o, ",\n  ");
    int col[3] = {l->color[0], l->color[1], l->color[2]};
    json_put_int_array(o, "l_color", col, 3); g_string_append(o, ",\n  ");
    int pal[LIGHT_PALETTE_MAX * 3];
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        for (int c = 0; c < 3; c++) pal[i * 3 + c] = l->palette[i][c];
    json_put_int_array(o, "l_palette", pal, LIGHT_PALETTE_MAX * 3);
    g_string_append(o, "\n}\n");
    json_write_file(path, o->str);
    g_string_free(o, TRUE);
    g_free(path);
}

static void saver_load(App *app)
{
    char *path = saver_file();
    if (!path) return;
    char *t = json_read_file(path);
    g_free(path);
    if (!t) return;
    app->saver_active = json_get_int(t, "active", 0) != 0;
    g_saver_auto = json_get_int(t, "auto", 0) != 0;
    AppSaverSnapshot *s = &app->saver_prev;
    s->rate_hz = json_get_int(t, "rate", 1000);
    s->sleep_seconds = json_get_int(t, "sleep", 300);
    s->idle_light_off = json_get_int(t, "idle", 0) != 0;
    s->light.effect = json_get_int(t, "l_effect", 0);
    s->light.brightness = json_get_int(t, "l_bri", 4);
    s->light.speed = json_get_int(t, "l_speed", 2);
    int col[3] = {255, 0, 0};
    json_get_int_array(t, "l_color", col, 3);
    for (int c = 0; c < 3; c++) s->light.color[c] = (uint8_t)col[c];
    int pal[LIGHT_PALETTE_MAX * 3] = {0};
    json_get_int_array(t, "l_palette", pal, LIGHT_PALETTE_MAX * 3);
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        for (int c = 0; c < 3; c++) s->light.palette[i][c] = (uint8_t)pal[i * 3 + c];
    s->light.palette_n = LIGHT_PALETTE_MAX;
    s->valid = app->saver_active;
    g_free(t);
}

static void saver_sync_ui(App *app)
{
    if (!P.saver_sw)
        return;
    loading = TRUE;
    adw_switch_row_set_active(P.saver_sw, app->saver_active);
    adw_switch_row_set_active(P.saver_auto_sw, app->settings_auto_saver);
    ui_chip_set(P.saver_chip, app->saver_active ? _("On") : _("Off"), app->saver_active ? UI_CHIP_OK : UI_CHIP_NEUTRAL);
    loading = FALSE;
}

/* Battery saver trades a little comfort for a lot of runtime: lower polling,
 * a short sleep timer, lights off. Everything it touches is remembered and
 * put back when it's turned off. */
void powersaver_set(App *app, gboolean on, gboolean automatic)
{
    if (on == app->saver_active || !app->connected)
        return;

    if (on) {
        AppSaverSnapshot *s = &app->saver_prev;
        s->valid = TRUE;
        s->rate_hz = app->rate_hz > 0 ? app->rate_hz : 1000;
        s->sleep_seconds = app->sensor.sleep_seconds > 0 ? app->sensor.sleep_seconds : 300;
        s->idle_light_off = app->sensor.idle_light_off;
        s->light = app->light;
        g_saver_auto = automatic;

        if (app->rate_hz > 250) {
            app->rate_hz = 250;
            ajazz_report_t r;
            ajazz_build_set_report_rate(&r, 1);
            send_async(app, &r, NULL);
        }
        if (app->sensor.sleep_seconds > 60 || app->sensor.sleep_seconds == 0)
            app->sensor.sleep_seconds = 60;
        app->sensor.idle_light_off = TRUE;
        send_power(app);

        AppLight off = app->light;
        off.effect = 0; /* LIGHT_FX_OFF */
        lighting_apply(app, &off);
        app->saver_active = TRUE;
        toast(app, automatic ? _("Battery low \xE2\x80\x94 Battery saver turned on.") : _("Battery saver on."));
    } else {
        AppSaverSnapshot *s = &app->saver_prev;
        app->saver_active = FALSE;
        if (s->valid) {
            int idx = 3;
            for (int i = 0; i < 4; i++)
                if (RATE_HZ[i] == s->rate_hz) idx = i;
            app->rate_hz = RATE_HZ[idx];
            ajazz_report_t r;
            ajazz_build_set_report_rate(&r, (uint8_t)idx);
            send_async(app, &r, NULL);
            app->sensor.sleep_seconds = s->sleep_seconds;
            app->sensor.idle_light_off = s->idle_light_off;
            send_power(app);
            lighting_apply(app, &s->light);
        }
        toast(app, _("Battery saver off \xE2\x80\x94 settings restored."));
    }
    saver_save(app);
    saver_sync_ui(app);
    refresh_performance(app);
}

static void on_saver_toggled(GObject *sw, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    powersaver_set(app, adw_switch_row_get_active(ADW_SWITCH_ROW(sw)), FALSE);
    /* if the mouse isn't connected the call was a no-op; show the truth */
    saver_sync_ui(app);
}

static void on_saver_auto_toggled(GObject *sw, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->settings_auto_saver = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));
    settings_save(app);
}

/* Called by the settings page on each battery reading: turns the saver on
 * automatically when the battery crosses the low threshold, and back off
 * again (only if it turned itself on) once the mouse is charging. */
void powersaver_battery_update(App *app, int percent, gboolean charging)
{
    if (!app->settings_auto_saver || percent < 0)
        return;
    if (!app->saver_active && !charging && percent < app->settings_low_battery_threshold)
        powersaver_set(app, TRUE, TRUE);
    else if (app->saver_active && g_saver_auto && (charging || percent >= app->settings_low_battery_threshold + 10))
        powersaver_set(app, FALSE, TRUE);
}

/* ---------------- page ---------------- */

static GtkWidget *quick_chip(const char *label, int dpi)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "pill");
    g_object_set_data(G_OBJECT(b), "dpi", GINT_TO_POINTER(dpi));
    g_signal_connect(b, "clicked", G_CALLBACK(on_quick_dpi), NULL);
    return b;
}

GtkWidget *build_performance_page(App *app)
{
    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Performance"),
        _("Everything here applies to the mouse as soon as you change it."), NULL, &body);
    saver_load(app);

    P.dpi_deb = ui_debounce_new(450, dpi_deb_fire, app);
    P.color_deb = ui_debounce_new(300, color_deb_fire, app);
    P.power_deb = ui_debounce_new(500, power_deb_fire, app);

    GtkWidget *cb, *card, *list;

    /* ---- battery saver ---- */
    card = ui_card_new(_("Battery saver"),
        _("Stretches the mouse's battery: 250 Hz polling, a 1-minute sleep timer, lights off and "
          "light-off when idle. Your previous settings are restored when you switch it off."), &cb);
    list = ui_list_new();
    P.saver_sw = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.saver_sw), _("Battery saver"));
    g_signal_connect(P.saver_sw, "notify::active", G_CALLBACK(on_saver_toggled), app);
    P.saver_chip = ui_chip_new(_("Off"), UI_CHIP_NEUTRAL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(P.saver_sw), P.saver_chip);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.saver_sw));
    P.saver_auto_sw = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.saver_auto_sw), _("Turn on automatically when the battery is low"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.saver_auto_sw), _("Uses the threshold from Settings; switches off again once charging."));
    g_signal_connect(P.saver_auto_sw, "notify::active", G_CALLBACK(on_saver_auto_toggled), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.saver_auto_sw));
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    /* ---- DPI ---- */
    card = ui_card_new(_("DPI"), _("Pick a stage to edit it \xE2\x80\x94 it also becomes the active stage on the mouse."), &cb);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *stages = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(stages), TRUE);
    GtkToggleButton *first = NULL;
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        gtk_widget_add_css_class(b, "aj-stage");
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), first);
        else first = GTK_TOGGLE_BUTTON(b);
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(top, GTK_ALIGN_CENTER);
        GtkWidget *dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 12, 12);
        gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
        gpointer *dd = g_new(gpointer, 2);
        dd[0] = app; dd[1] = GINT_TO_POINTER(i);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dot), draw_dot, dd, g_free);
        char nm[16]; snprintf(nm, sizeof(nm), "%d", i + 1);
        GtkWidget *num = gtk_label_new(nm);
        gtk_widget_add_css_class(num, "aj-stage-num");
        gtk_box_append(GTK_BOX(top), dot);
        gtk_box_append(GTK_BOX(top), num);
        GtkWidget *val = gtk_label_new("\xE2\x80\x94");
        gtk_widget_add_css_class(val, "aj-stage-val");
        gtk_box_append(GTK_BOX(v), top);
        gtk_box_append(GTK_BOX(v), val);
        gtk_button_set_child(GTK_BUTTON(b), v);
        P.stage_btn[i] = b; P.stage_dot[i] = dot; P.stage_val[i] = val;
        gpointer *cd = g_new(gpointer, 2);
        cd[0] = app; cd[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "toggled", G_CALLBACK(on_stage_clicked), cd, free_closure_data, 0);
        gtk_box_append(GTK_BOX(stages), b);
    }
    gtk_box_append(GTK_BOX(inner), stages);

    /* editor for the selected stage */
    GtkWidget *edit = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    P.dpi_title = gtk_label_new(_("Stage 1"));
    gtk_widget_add_css_class(P.dpi_title, "heading");
    gtk_widget_set_halign(P.dpi_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(edit), P.dpi_title);

    P.dpi_adj = gtk_adjustment_new(800, DPI_MIN, DPI_MAX, 100, 1000, 0);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, P.dpi_adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    GtkWidget *spin = gtk_spin_button_new(P.dpi_adj, 100, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_box_append(GTK_BOX(row), scale);
    gtk_box_append(GTK_BOX(row), spin);
    gtk_box_append(GTK_BOX(edit), row);
    g_signal_connect(P.dpi_adj, "value-changed", G_CALLBACK(on_dpi_value_changed), app);

    GtkWidget *chips = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(chips), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(chips), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(chips), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(chips), 9);
    static const int quick[] = {400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 26000};
    for (guint i = 0; i < G_N_ELEMENTS(quick); i++) {
        char l[16]; snprintf(l, sizeof(l), "%d", quick[i]);
        gtk_flow_box_append(GTK_FLOW_BOX(chips), quick_chip(l, quick[i]));
    }
    gtk_box_append(GTK_BOX(edit), chips);

    GtkWidget *crow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *cl = gtk_label_new(_("Indicator colour"));
    gtk_widget_set_hexpand(cl, TRUE);
    gtk_widget_set_halign(cl, GTK_ALIGN_START);
    P.color_btn = gtk_color_dialog_button_new(gtk_color_dialog_new());
    g_signal_connect(P.color_btn, "notify::rgba", G_CALLBACK(on_color_changed), app);
    gtk_box_append(GTK_BOX(crow), cl);
    gtk_box_append(GTK_BOX(crow), P.color_btn);
    gtk_box_append(GTK_BOX(edit), crow);
    gtk_box_append(GTK_BOX(inner), edit);

    GtkWidget *defaults = ui_button_new(_("Restore DPI defaults"), "edit-undo-symbolic", "ghost");
    gtk_widget_set_halign(defaults, GTK_ALIGN_START);
    g_signal_connect(defaults, "clicked", G_CALLBACK(on_dpi_defaults), app);
    gtk_box_append(GTK_BOX(inner), defaults);
    gtk_box_append(GTK_BOX(cb), ui_card_pad(inner));

    list = ui_list_new();
    P.count_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, AJAZZ_DPI_STAGES, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.count_row), _("Stages used by the DPI button"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.count_row), _("The DPI button cycles through this many stages."));
    adw_spin_row_set_value(P.count_row, 5);
    g_signal_connect(P.count_row, "notify::value", G_CALLBACK(on_count_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.count_row));
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    /* ---- report rate ---- */
    card = ui_card_new(_("Report rate"), _("How often the mouse reports its position. Higher is smoother; lower saves battery."), &cb);
    const char *rates[] = {"125 Hz", "250 Hz", "500 Hz", "1000 Hz"};
    P.rate_seg = ui_segmented_new(rates, 4, 3, on_rate_selected, app);
    gtk_box_append(GTK_BOX(cb), ui_card_pad(P.rate_seg));
    gtk_box_append(GTK_BOX(body), card);

    /* ---- sensor ---- */
    card = ui_card_new(_("Sensor"), NULL, &cb);
    list = ui_list_new();
    const char *lift[] = {"1 mm", "2 mm"};
    P.liftoff_seg = ui_segmented_new(lift, 2, 0, on_liftoff_selected, app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Lift-off distance"),
        _("How far you can lift the mouse before it stops tracking."), P.liftoff_seg)));
    struct { AdwSwitchRow **out; const char *t, *s; GCallback cb; } sw[] = {
        {&P.angle_sw, _("Angle snapping"), _("Straightens slightly wobbly lines. Adds a little input lag."), G_CALLBACK(on_angle_changed)},
        {&P.ripple_sw, _("Ripple control"), _("Smooths the cursor above 9000 DPI."), G_CALLBACK(on_ripple_changed)},
        {&P.msync_sw, _("Motion sync"), _("Syncs sensor reads with the report rate for steadier tracking."), G_CALLBACK(on_msync_changed)},
    };
    for (guint i = 0; i < G_N_ELEMENTS(sw); i++) {
        *sw[i].out = ADW_SWITCH_ROW(adw_switch_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(*sw[i].out), sw[i].t);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(*sw[i].out), sw[i].s);
        g_signal_connect(*sw[i].out, "notify::active", sw[i].cb, app);
        gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(*sw[i].out));
    }
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    /* ---- power & buttons ---- */
    card = ui_card_new(_("Power and buttons"), NULL, &cb);
    list = ui_list_new();
    P.sleep_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.sleep_combo), _("Go to sleep after"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.sleep_combo), _("Idle time before the mouse sleeps to save battery."));
    const char *names[N_SLEEP + 1];
    for (guint i = 0; i < N_SLEEP; i++) names[i] = gettext(SLEEP_PRESETS[i].label);
    names[N_SLEEP] = NULL;
    adw_combo_row_set_model(P.sleep_combo, G_LIST_MODEL(gtk_string_list_new(names)));
    adw_combo_row_set_selected(P.sleep_combo, 4);
    g_signal_connect(P.sleep_combo, "notify::selected", G_CALLBACK(on_sleep_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.sleep_combo));

    P.wake_sw = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.wake_sw), _("Wake on movement"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.wake_sw), _("Moving the mouse wakes it; otherwise you have to click."));
    g_signal_connect(P.wake_sw, "notify::active", G_CALLBACK(on_wake_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.wake_sw));

    P.idle_sw = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.idle_sw), _("Turn the light off when idle"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.idle_sw), _("The LEDs are a big part of the battery drain."));
    g_signal_connect(P.idle_sw, "notify::active", G_CALLBACK(on_idle_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.idle_sw));

    P.debounce_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 30, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(P.debounce_row), _("Button debounce (ms)"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(P.debounce_row), _("Ignores bouncing after a click. Lower is faster; raise it if you get double clicks."));
    adw_spin_row_set_value(P.debounce_row, 8);
    g_signal_connect(P.debounce_row, "notify::value", G_CALLBACK(on_debounce_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(P.debounce_row));
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    saver_sync_ui(app);
    return page;
}
