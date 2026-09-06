/*
 * Mouse Config page: DPI stages and their indicator LED colours, report
 * rate, and sensor/power settings (lift-off distance, auto-sleep timer,
 * idle light-off).
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "app.h"
#include "../devices/device.h"

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
static void on_dpi_color_changed(GtkColorDialogButton *btn, GParamSpec *pspec, gpointer user_data);

/* Updates the single colour swatch button to reflect app->dpi_color_cache
 * for whichever stage is selected -- blocking the change handler while we
 * do, since this is just reflecting cached/device state into the UI, not
 * a user edit that should be sent back to the mouse. */
static void update_dpi_color_swatch(App *app)
{
    if (!app->dpi_color_btn)
        return;
    int stage = app->dpi_selected;
    GdkRGBA rgba = {
        .red = app->dpi_color_cache[stage][0] / 255.0f,
        .green = app->dpi_color_cache[stage][1] / 255.0f,
        .blue = app->dpi_color_cache[stage][2] / 255.0f,
        .alpha = 1.0f,
    };
    g_signal_handlers_block_matched(app->dpi_color_btn, G_SIGNAL_MATCH_FUNC,
        0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
    gtk_color_dialog_button_set_rgba(app->dpi_color_btn, &rgba);
    g_signal_handlers_unblock_matched(app->dpi_color_btn, G_SIGNAL_MATCH_FUNC,
        0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
}

/* "DPI Stages Preview" line chart: plots the 6 (stage, dpi) points on a
 * log-like Y axis (fixed ticks 100/200/400/800/1600/3200/6400/12800,
 * matching the values this mouse's DPI table can actually hold -- see
 * ajazz_build_set_dpi_table()), connected by a line, points coloured with
 * each stage's own ajazz-dpi-N colour so it reads as an extension of the
 * stage buttons above it rather than a separate generic chart. */
static const double dpi_chart_ticks[] = {100, 200, 400, 800, 1600, 3200, 6400, 12800};
#define N_DPI_CHART_TICKS (int)(sizeof(dpi_chart_ticks) / sizeof(dpi_chart_ticks[0]))
static const struct { double r, g, b; } dpi_stage_colors[AJAZZ_DPI_STAGES] = {
    {1.0, 0.231, 0.231}, {0.180, 0.800, 0.251}, {0.231, 0.420, 1.0},
    {0.0, 0.820, 0.820}, {1.0, 0.831, 0.0}, {0.910, 0.220, 0.910},
};

static double dpi_chart_y_frac(double dpi)
{
    if (dpi < dpi_chart_ticks[0]) dpi = dpi_chart_ticks[0];
    if (dpi > dpi_chart_ticks[N_DPI_CHART_TICKS - 1]) dpi = dpi_chart_ticks[N_DPI_CHART_TICKS - 1];
    double lo = log(dpi_chart_ticks[0]), hi = log(dpi_chart_ticks[N_DPI_CHART_TICKS - 1]);
    return (log(dpi) - lo) / (hi - lo);
}

static void draw_dpi_chart(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    App *app = data;
    (void)area;
    double left = 54, right = width - 12, top = 10, bottom = height - 10;
    double plot_w = right - left, plot_h = bottom - top;

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    for (int i = 0; i < N_DPI_CHART_TICKS; i++) {
        double frac = dpi_chart_y_frac(dpi_chart_ticks[i]);
        double y = bottom - frac * plot_h;
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, right, y);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        char label[16];
        if (dpi_chart_ticks[i] >= 1000)
            snprintf(label, sizeof(label), "%.0fk", dpi_chart_ticks[i] / 1000.0);
        else
            snprintf(label, sizeof(label), "%.0f", dpi_chart_ticks[i]);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        cairo_move_to(cr, left - ext.width - 8, y + ext.height / 2);
        cairo_show_text(cr, label);
    }

    /* connecting line */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_set_line_width(cr, 1.6);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        double x = left + plot_w * (i / (double)(AJAZZ_DPI_STAGES - 1));
        double y = bottom - dpi_chart_y_frac(app->dpi_cache[i]) * plot_h;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    /* points, per-stage colour, selected stage drawn larger */
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        double x = left + plot_w * (i / (double)(AJAZZ_DPI_STAGES - 1));
        double y = bottom - dpi_chart_y_frac(app->dpi_cache[i]) * plot_h;
        double radius = (i == app->dpi_selected) ? 6.0 : 4.0;
        cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
        cairo_set_source_rgb(cr, dpi_stage_colors[i].r, dpi_stage_colors[i].g, dpi_stage_colors[i].b);
        cairo_fill(cr);
        if (i == app->dpi_selected) {
            cairo_arc(cr, x, y, radius + 2.5, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
    }
}

/* Updates each of the 6 stage buttons' colour-chip + value label to
 * match app->dpi_cache/dpi_color_cache, and the selected one's thick red
 * border -- called whenever a stage is picked, a value/colour changes,
 * or the fetch thread applies real device state. */
static void update_dpi_stage_buttons(App *app)
{
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        if (!app->dpi_btn[i]) continue;
        if (i == app->dpi_selected)
            gtk_widget_add_css_class(GTK_WIDGET(app->dpi_btn[i]), "selected");
        else
            gtk_widget_remove_css_class(GTK_WIDGET(app->dpi_btn[i]), "selected");

        GtkWidget *value_lbl = g_object_get_data(G_OBJECT(app->dpi_btn[i]), "value-lbl");
        if (value_lbl) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", app->dpi_cache[i]);
            gtk_label_set_text(GTK_LABEL(value_lbl), buf);
        }
    }
    if (app->dpi_chart)
        gtk_widget_queue_draw(GTK_WIDGET(app->dpi_chart));
}

static void dpi_select(App *app, int stage)
{
    app->dpi_selected = stage;
    adw_spin_row_set_value(app->dpi_value_spin, app->dpi_cache[stage]);
    char buf[32];
    snprintf(buf, sizeof(buf), _("DPI value (stage %d)"), stage + 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->dpi_value_row), buf);
    update_dpi_color_swatch(app);
    update_dpi_stage_buttons(app);
}

/* Called from Home's DPI card chips (see mouse_page_select_stage() in
 * app.h) so picking a stage there updates this already-built page's
 * selection too, without switching to it first. */
void mouse_page_select_stage(App *app, int stage)
{
    if (stage < 0 || stage >= AJAZZ_DPI_STAGES)
        return;
    dpi_select(app, stage);
}

/* Public wrapper for gui/page_profiles.c -- see the declaration in
 * app.h. Sends both the DPI table and the DPI colour table from a saved
 * profile, then refreshes the stage grid/chart/swatch to match. */
gboolean mouse_page_apply_profile_dpi(App *app, const uint16_t dpi[AJAZZ_DPI_STAGES],
    const uint8_t colors[AJAZZ_DPI_STAGES][3], int selected_stage)
{
    if (!ensure_connected(app))
        return FALSE;
    if (selected_stage < 0 || selected_stage >= AJAZZ_DPI_STAGES)
        selected_stage = 0;

    memcpy(app->dpi_cache, dpi, sizeof(app->dpi_cache));
    memcpy(app->dpi_color_cache, colors, sizeof(app->dpi_color_cache));

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, app->dpi_cache, (uint8_t)selected_stage);
    gboolean ok1 = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);

    ajazz_report_t req2;
    ajazz_build_set_dpi_colors(&req2, app->dpi_color_cache);
    gboolean ok2 = (ajazz_send_and_wait_mt(app, &req2, 1000) == 0);

    dpi_select(app, selected_stage);
    return ok1 && ok2;
}

/* Public wrapper for gui/page_profiles.c -- see the declaration in
 * app.h. Maps `hz` to the nearest of the 4 supported rates and sends it. */
gboolean mouse_page_apply_profile_rate(App *app, int hz)
{
    if (!ensure_connected(app))
        return FALSE;
    static const int rate_values[4] = {125, 250, 500, 1000};
    int idx = 3, best_diff = abs(hz - rate_values[3]);
    for (int i = 0; i < 3; i++) {
        int diff = abs(hz - rate_values[i]);
        if (diff < best_diff) { best_diff = diff; idx = i; }
    }
    ajazz_report_t req;
    ajazz_build_set_report_rate(&req, (uint8_t)idx);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);
    if (ok) {
        gtk_toggle_button_set_active(app->rate_btn[idx], TRUE);
        app->rate_hz = rate_values[idx];
    }
    return ok;
}

static void on_dpi_stage_clicked(GtkButton *btn, gpointer user_data)
{
    App *app = ((gpointer *)user_data)[0];
    int stage = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
    (void)btn;
    dpi_select(app, stage);
}

/* Heap-allocated result of the fetch thread below, handed to
 * config_fetch_apply() on the main thread via g_idle_add(). Each *_ok
 * flag is independent since each query can fail on its own -- matches
 * the original code's per-query if-blocks, just deferred to apply time. */
typedef struct {
    App *app;

    gboolean dpi_ok;
    uint16_t dpi_cache[AJAZZ_DPI_STAGES];
    int active_stage;

    gboolean color_ok;
    uint8_t dpi_color_cache[AJAZZ_DPI_STAGES][3];

    gboolean rate_ok;
    int rate_idx;

    gboolean sensor_ok;
    gboolean liftoff2;
    gboolean idle_off;
    int sleep_seconds;
} ConfigFetchResult;

static gboolean config_fetch_apply(gpointer user_data)
{
    ConfigFetchResult *res = user_data;
    App *app = res->app;
    app->config_fetch_pending = FALSE;

    if (!app->connected) {
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    if (res->dpi_ok) {
        memcpy(app->dpi_cache, res->dpi_cache, sizeof(app->dpi_cache));
        int active = res->active_stage;
        if (active < 0 || active >= AJAZZ_DPI_STAGES) active = 0;
        dpi_select(app, active);
    }

    if (res->color_ok) {
        memcpy(app->dpi_color_cache, res->dpi_color_cache, sizeof(app->dpi_color_cache));
        /* Setting rgba fires notify::rgba, which would re-send this same
         * colour back to the device -- update_dpi_color_swatch() blocks
         * the handler while just reflecting device state into the UI. */
        update_dpi_color_swatch(app);
    }

    if (res->rate_ok && res->rate_idx >= 0) {
        gtk_toggle_button_set_active(app->rate_btn[res->rate_idx], TRUE);
        static const int rate_values[4] = {125, 250, 500, 1000};
        app->rate_hz = rate_values[res->rate_idx];
    }

    if (res->sensor_ok) {
        gtk_check_button_set_active(res->liftoff2 ? app->liftoff2 : app->liftoff1, TRUE);
        adw_switch_row_set_active(app->idle_light_switch, res->idle_off);

        guint best = 0;
        int best_diff = G_MAXINT;
        for (guint i = 0; i < G_N_ELEMENTS(sleep_presets); i++) {
            int diff = ABS(sleep_presets[i].seconds - res->sleep_seconds);
            if (diff < best_diff) { best_diff = diff; best = i; }
        }
        adw_combo_row_set_selected(app->sleep_combo, best);
    }

    g_free(res);
    return G_SOURCE_REMOVE;
}

/* Runs off the main thread -- MUST NOT touch any GTK widget, only
 * app->dev via the *_mt() wrapper (see App::hid_lock) and the heap
 * result handed to config_fetch_apply(). */
static gpointer config_fetch_thread(gpointer user_data)
{
    App *app = user_data;
    ConfigFetchResult *res = g_new0(ConfigFetchResult, 1);
    res->app = app;

    ajazz_report_t reply;
    res->dpi_ok = (ajazz_query_mt(app, AJAZZ_QUERY_DPI_TABLE, &reply, 1000) == 0);
    if (res->dpi_ok) {
        ajazz_parse_dpi_table(&reply, res->dpi_cache);
        /* Active stage lives in this same reply's payload[0] high
         * nibble -- there's no separate "get active DPI stage" query. */
        res->active_stage = ajazz_dpi_table_active_stage(&reply);
    }

    /* One retry: this query has been observed to fail/return stale data
     * on the first try right after the mouse wakes up or reconnects
     * (same flakiness documented for other queries in
     * docs/reverse-engineering.md) -- without it, the colour swatches
     * would silently stay at their zeroed (black) default until the
     * user leaves and re-enters this page. */
    ajazz_report_t color_reply;
    res->color_ok = (ajazz_query_mt(app, AJAZZ_QUERY_DPI_COLOR_TABLE, &color_reply, 1000) == 0);
    if (!res->color_ok)
        res->color_ok = (ajazz_query_mt(app, AJAZZ_QUERY_DPI_COLOR_TABLE, &color_reply, 1000) == 0);
    if (res->color_ok)
        ajazz_parse_dpi_color_table(&color_reply, res->dpi_color_cache);

    ajazz_report_t rate_reply;
    res->rate_ok = (ajazz_query_mt(app, AJAZZ_QUERY_REPORT_RATE, &rate_reply, 1000) == 0);
    if (res->rate_ok) {
        int hz = ajazz_report_rate_hz(rate_reply.payload[0]);
        res->rate_idx = (hz == 125) ? 0 : (hz == 250) ? 1 : (hz == 500) ? 2 : (hz == 1000) ? 3 : -1;
    }

    ajazz_report_t sensor_reply;
    res->sensor_ok = (ajazz_query_mt(app, AJAZZ_QUERY_SENSOR, &sensor_reply, 1000) == 0);
    if (res->sensor_ok) {
        res->liftoff2 = sensor_reply.payload[1] != 0;
        res->idle_off = sensor_reply.payload[2] != 0;
        res->sleep_seconds = sensor_reply.payload[0] * 10;
    }

    g_idle_add(config_fetch_apply, res);
    return NULL;
}

/* Called every time the Mouse Config page becomes visible, so what's on
 * screen is always what the mouse actually has right now -- there's no
 * "load" button to remember to click. Reads DPI table, DPI colour
 * table, report rate and sensor settings (4-5 queries) on a background
 * thread so a slow/unplugged mouse can't freeze the window -- see
 * config_fetch_thread()/config_fetch_apply() and the comment on
 * App::hid_lock. */
void refresh_config(App *app)
{
    if (!app->connected)
        return;
    if (app->config_fetch_pending)
        return;
    app->config_fetch_pending = TRUE;
    g_thread_unref(g_thread_new("ajazzy-config-fetch", config_fetch_thread, app));
}

/* Active DPI stage goes through a full table rewrite (family 0x03), not
 * a separate "switch stage" command -- see ajazz_build_set_dpi_table(). */
static void on_dpi_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    app->dpi_cache[app->dpi_selected] = (uint16_t)adw_spin_row_get_value(app->dpi_value_spin);
    update_dpi_stage_buttons(app);

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, app->dpi_cache, (uint8_t)app->dpi_selected);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);

    toast(app, ok ? _("DPI applied.") : _("Failed to apply DPI."));
}

/* AJAZZ_DEFAULT_DPI -- factory values from the official installer's own
 * per-model config, see the comment in protocol/hid.h. */
static void on_dpi_reset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    memcpy(app->dpi_cache, AJAZZ_DEFAULT_DPI, sizeof(app->dpi_cache));

    ajazz_report_t req;
    ajazz_build_set_dpi_table(&req, app->dpi_cache, 0);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);

    if (ok)
        dpi_select(app, 0);
    toast(app, ok ? _("DPI reset to factory defaults.") : _("Failed to reset DPI."));
}

/* Applies immediately on pick (matches the official driver's own DPI
 * colour picker), sending the full 6-stage table with just this one
 * stage changed -- see ajazz_build_set_dpi_colors(). */
static void on_dpi_color_changed(GtkColorDialogButton *btn, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    int stage = app->dpi_selected;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(btn);
    app->dpi_color_cache[stage][0] = (uint8_t)(rgba->red * 255.0 + 0.5);
    app->dpi_color_cache[stage][1] = (uint8_t)(rgba->green * 255.0 + 0.5);
    app->dpi_color_cache[stage][2] = (uint8_t)(rgba->blue * 255.0 + 0.5);

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);

    toast(app, ok ? _("DPI colour applied.") : _("Failed to apply DPI colour."));
}

/* AJAZZ_DEFAULT_DPI_COLOR -- factory values from the official
 * installer's own per-model config, see the comment in protocol/hid.h. */
static void on_dpicolor_reset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    memcpy(app->dpi_color_cache, AJAZZ_DEFAULT_DPI_COLOR, sizeof(app->dpi_color_cache));

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);

    if (ok)
        update_dpi_color_swatch(app);
    toast(app, ok ? _("DPI colours reset to factory defaults.") : _("Failed to reset DPI colours."));
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
    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0) {
        static const int rate_values[4] = {125, 250, 500, 1000};
        app->rate_hz = rate_values[rate_idx];
        toast(app, _("Report rate applied."));
    } else {
        toast(app, _("Failed to apply report rate."));
    }
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
    if (ajazz_query_mt(app, AJAZZ_QUERY_SENSOR, &reply, 1000) != 0) {
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

    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0)
        toast(app, _("Sensor and power settings applied."));
    else
        toast(app, _("Failed to apply sensor settings."));
}

/* Quick DPI chip: sets the currently-selected stage's value (previewed
 * immediately in the grid/chart) without sending anything to the mouse
 * yet -- still requires "Apply DPI to mouse", same as typing into the
 * spinner would. */
static void on_quick_dpi_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = ((gpointer *)user_data)[0];
    int value = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
    adw_spin_row_set_value(app->dpi_value_spin, value);
    app->dpi_cache[app->dpi_selected] = (uint16_t)value;
    update_dpi_stage_buttons(app);
}

/* Colour palette dot: applies one of the mouse's own factory default
 * per-stage colours (AJAZZ_DEFAULT_DPI_COLOR) to whichever stage is
 * currently selected, immediately -- same wire command and apply-on-pick
 * behaviour as the swatch button above. */
static void on_palette_dot_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = ((gpointer *)user_data)[0];
    int palette_idx = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    int stage = app->dpi_selected;
    app->dpi_color_cache[stage][0] = AJAZZ_DEFAULT_DPI_COLOR[palette_idx][0];
    app->dpi_color_cache[stage][1] = AJAZZ_DEFAULT_DPI_COLOR[palette_idx][1];
    app->dpi_color_cache[stage][2] = AJAZZ_DEFAULT_DPI_COLOR[palette_idx][2];

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);
    if (ok)
        update_dpi_color_swatch(app);
    toast(app, ok ? _("DPI colour applied.") : _("Failed to apply DPI colour."));
}

GtkWidget *build_mouse_config_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    /* dpi -- header, a 6-column grid of stage buttons (colour chip + DPI
     * value, thick red border on the selected one), the stages preview
     * chart, a row of common-value quick chips, and Apply/Reset -- all in
     * the left column, with a fixed-width "Stage N Settings" detail panel
     * (value spinner + colour palette) to the right. NOTE: the reference
     * mockup this was drawn from also showed a "DPI Sync" button and
     * per-stage "Enable"/"Set As Default" toggles -- neither exists in
     * this mouse's protocol (there's no per-stage enable bit, and no
     * "default stage" concept beyond whichever one is currently active),
     * so both are intentionally left out rather than faked. */
    AdwPreferencesGroup *dpi_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    GtkWidget *dpi_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *dpi_title = gtk_label_new(_("DPI"));
    gtk_widget_add_css_class(dpi_title, "title-2");
    gtk_widget_set_halign(dpi_title, GTK_ALIGN_START);
    GtkWidget *dpi_subtitle = gtk_label_new(_("Configure the mouse's 6 DPI stages and their indicator colours."));
    gtk_widget_add_css_class(dpi_subtitle, "dim-label");
    gtk_widget_set_halign(dpi_subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dpi_header), dpi_title);
    gtk_box_append(GTK_BOX(dpi_header), dpi_subtitle);
    adw_preferences_group_add(dpi_group, dpi_header);

    GtkWidget *dpi_columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    /* left column: stage grid + chart + quick DPI + apply/reset */
    GtkWidget *dpi_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(dpi_card, "ajazz-card");
    gtk_widget_set_hexpand(dpi_card, TRUE);

    GtkWidget *dpi_grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(dpi_grid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(dpi_grid), 8);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        char klass[16];
        snprintf(klass, sizeof(klass), "ajazz-dpi-%d", i + 1);
        gtk_widget_add_css_class(b, "ajazz-dpi-btn");
        gtk_widget_add_css_class(b, klass);
        app->dpi_btn[i] = GTK_TOGGLE_BUTTON(b);

        GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char stage_lbl[16];
        snprintf(stage_lbl, sizeof(stage_lbl), _("Stage %d"), i + 1);
        GtkWidget *stage_name = gtk_label_new(stage_lbl);
        gtk_widget_add_css_class(stage_name, "caption");
        GtkWidget *value_lbl = gtk_label_new("0");
        gtk_widget_add_css_class(value_lbl, "ajazz-stat-value");
        gtk_box_append(GTK_BOX(content), stage_name);
        gtk_box_append(GTK_BOX(content), value_lbl);
        gtk_button_set_child(GTK_BUTTON(b), content);
        g_object_set_data(G_OBJECT(b), "value-lbl", value_lbl);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_dpi_stage_clicked), data, free_closure_data, 0);

        gtk_grid_attach(GTK_GRID(dpi_grid), b, i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(dpi_card), dpi_grid);

    /* "DPI Stages Preview" chart */
    GtkWidget *chart_title = gtk_label_new(_("DPI Stages Preview"));
    gtk_widget_add_css_class(chart_title, "ajazz-card-title");
    gtk_widget_set_halign(chart_title, GTK_ALIGN_START);
    gtk_widget_set_margin_top(chart_title, 4);
    gtk_box_append(GTK_BOX(dpi_card), chart_title);

    GtkWidget *chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(chart, -1, 130);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(chart), draw_dpi_chart, app, NULL);
    app->dpi_chart = GTK_DRAWING_AREA(chart);
    gtk_box_append(GTK_BOX(dpi_card), chart);

    GtkWidget *chart_note = gtk_label_new(_("Values snap to what the mouse's DPI table can actually hold; the highlighted point is the selected stage."));
    gtk_widget_add_css_class(chart_note, "dim-label");
    gtk_widget_add_css_class(chart_note, "caption");
    gtk_label_set_wrap(GTK_LABEL(chart_note), TRUE);
    gtk_widget_set_halign(chart_note, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dpi_card), chart_note);

    /* Quick DPI: common values, applied to whichever stage is selected */
    GtkWidget *quick_title = gtk_label_new(_("Quick DPI"));
    gtk_widget_add_css_class(quick_title, "ajazz-card-title");
    gtk_widget_set_halign(quick_title, GTK_ALIGN_START);
    gtk_widget_set_margin_top(quick_title, 4);
    gtk_box_append(GTK_BOX(dpi_card), quick_title);

    GtkWidget *quick_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    static const int quick_dpi_values[] = {400, 800, 1600, 3200, 6400, 12800};
    for (guint i = 0; i < G_N_ELEMENTS(quick_dpi_values); i++) {
        char label[16];
        snprintf(label, sizeof(label), "%d", quick_dpi_values[i]);
        GtkWidget *chip = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(chip, "pill");
        gtk_widget_add_css_class(chip, "flat");
        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(quick_dpi_values[i]);
        g_signal_connect_data(chip, "clicked", G_CALLBACK(on_quick_dpi_clicked), data, free_closure_data, 0);
        gtk_box_append(GTK_BOX(quick_row), chip);
    }
    gtk_box_append(GTK_BOX(dpi_card), quick_row);

    GtkWidget *dpi_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(dpi_btn_box, 4);

    GtkWidget *dpi_apply = gtk_button_new_with_label(_("Apply DPI to mouse"));
    gtk_widget_add_css_class(dpi_apply, "suggested-action");
    gtk_widget_add_css_class(dpi_apply, "pill");
    gtk_widget_add_css_class(dpi_apply, "ajazz-cta");
    gtk_widget_set_hexpand(dpi_apply, TRUE);
    g_signal_connect(dpi_apply, "clicked", G_CALLBACK(on_dpi_apply), app);
    gtk_box_append(GTK_BOX(dpi_btn_box), dpi_apply);

    GtkWidget *dpi_reset = gtk_button_new_with_label(_("Reset to factory defaults"));
    gtk_widget_add_css_class(dpi_reset, "pill");
    g_signal_connect(dpi_reset, "clicked", G_CALLBACK(on_dpi_reset_clicked), app);
    gtk_box_append(GTK_BOX(dpi_btn_box), dpi_reset);

    gtk_box_append(GTK_BOX(dpi_card), dpi_btn_box);
    gtk_box_append(GTK_BOX(dpi_columns), dpi_card);

    /* right column: fixed-width "Stage N Settings" detail panel */
    GtkWidget *side_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(side_card, "ajazz-card");
    gtk_widget_set_size_request(side_card, 300, -1);
    GtkWidget *side_title = gtk_label_new(_("Stage settings"));
    gtk_widget_add_css_class(side_title, "ajazz-card-title");
    gtk_widget_set_halign(side_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(side_card), side_title);

    GtkWidget *detail_group_box = gtk_list_box_new();
    gtk_widget_add_css_class(detail_group_box, "boxed-list");

    app->dpi_value_row = ADW_ACTION_ROW(adw_action_row_new());
    app->dpi_value_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(50, 26000, 50));
    adw_spin_row_set_value(app->dpi_value_spin, 800);
    gtk_widget_set_valign(GTK_WIDGET(app->dpi_value_spin), GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(app->dpi_value_row, GTK_WIDGET(app->dpi_value_spin));
    gtk_list_box_append(GTK_LIST_BOX(detail_group_box), GTK_WIDGET(app->dpi_value_row));

    /* dpi indicator colour -- family 0x04, see docs/reverse-engineering.md.
     * One swatch button that always shows/edits the selected stage's
     * colour; applies immediately on pick (matches the official driver's
     * own colour-picker UX, no separate Apply button). */
    AdwActionRow *color_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(color_row), _("Indicator colour"));
    GtkColorDialog *dialog = gtk_color_dialog_new();
    GtkWidget *color_btn = gtk_color_dialog_button_new(dialog);
    gtk_widget_set_valign(color_btn, GTK_ALIGN_CENTER);
    app->dpi_color_btn = GTK_COLOR_DIALOG_BUTTON(color_btn);
    g_signal_connect(color_btn, "notify::rgba", G_CALLBACK(on_dpi_color_changed), app);
    adw_action_row_add_suffix(color_row, color_btn);
    gtk_list_box_append(GTK_LIST_BOX(detail_group_box), GTK_WIDGET(color_row));

    gtk_box_append(GTK_BOX(side_card), detail_group_box);

    /* colour palette: the mouse's own default per-stage colours (see
     * AJAZZ_DEFAULT_DPI_COLOR), each a one-click shortcut for the
     * currently-selected stage's indicator colour, plus the swatch button
     * above for anything custom. */
    GtkWidget *palette_title = gtk_label_new(_("Quick colours"));
    gtk_widget_add_css_class(palette_title, "ajazz-card-title");
    gtk_widget_set_halign(palette_title, GTK_ALIGN_START);
    gtk_widget_set_margin_top(palette_title, 4);
    gtk_box_append(GTK_BOX(side_card), palette_title);

    GtkWidget *palette_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(palette_row, GTK_ALIGN_START);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        GtkWidget *dot = gtk_button_new();
        gtk_widget_add_css_class(dot, "circular");
        char klass[16];
        snprintf(klass, sizeof(klass), "ajazz-dpi-%d", i + 1);
        gtk_widget_add_css_class(dot, klass);
        gtk_widget_set_size_request(dot, 26, 26);
        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(dot, "clicked", G_CALLBACK(on_palette_dot_clicked), data, free_closure_data, 0);
        gtk_box_append(GTK_BOX(palette_row), dot);
    }
    gtk_box_append(GTK_BOX(side_card), palette_row);

    GtkWidget *dpicolor_reset = gtk_button_new_with_label(_("Reset colours to factory defaults"));
    gtk_widget_add_css_class(dpicolor_reset, "pill");
    gtk_widget_set_margin_top(dpicolor_reset, 6);
    gtk_widget_set_halign(dpicolor_reset, GTK_ALIGN_START);
    g_signal_connect(dpicolor_reset, "clicked", G_CALLBACK(on_dpicolor_reset_clicked), app);
    gtk_box_append(GTK_BOX(side_card), dpicolor_reset);

    gtk_box_append(GTK_BOX(dpi_columns), side_card);
    adw_preferences_group_add(dpi_group, dpi_columns);
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
    adw_action_row_set_subtitle(lift_row, _("Didn't show a visible effect in testing on this mouse -- may not do anything."));
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
