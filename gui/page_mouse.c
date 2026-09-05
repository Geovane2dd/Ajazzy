/*
 * Mouse Config page: DPI stages and their indicator LED colours, report
 * rate, and sensor/power settings (lift-off distance, auto-sleep timer,
 * idle light-off).
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>

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

static void on_dpi_color_changed(GtkColorDialogButton *btn, GParamSpec *pspec, gpointer user_data);

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
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
            GdkRGBA rgba = {
                .red = app->dpi_color_cache[i][0] / 255.0f,
                .green = app->dpi_color_cache[i][1] / 255.0f,
                .blue = app->dpi_color_cache[i][2] / 255.0f,
                .alpha = 1.0f,
            };
            /* Setting rgba fires notify::rgba, which would re-send this
             * same colour back to the device -- block the handler while
             * we're just reflecting device state into the UI. */
            guint n = g_signal_handlers_block_matched(app->dpi_color_btn[i], G_SIGNAL_MATCH_FUNC,
                0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
            gtk_color_dialog_button_set_rgba(app->dpi_color_btn[i], &rgba);
            g_signal_handlers_unblock_matched(app->dpi_color_btn[i], G_SIGNAL_MATCH_FUNC,
                0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
            (void)n;
        }
    }

    if (res->rate_ok && res->rate_idx >= 0)
        gtk_toggle_button_set_active(app->rate_btn[res->rate_idx], TRUE);

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
    App *app = ((gpointer *)user_data)[0];
    int stage = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
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

    if (ok) {
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
            GdkRGBA rgba = {
                .red = app->dpi_color_cache[i][0] / 255.0f,
                .green = app->dpi_color_cache[i][1] / 255.0f,
                .blue = app->dpi_color_cache[i][2] / 255.0f,
                .alpha = 1.0f,
            };
            guint n = g_signal_handlers_block_matched(app->dpi_color_btn[i], G_SIGNAL_MATCH_FUNC,
                0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
            gtk_color_dialog_button_set_rgba(app->dpi_color_btn[i], &rgba);
            g_signal_handlers_unblock_matched(app->dpi_color_btn[i], G_SIGNAL_MATCH_FUNC,
                0, 0, NULL, G_CALLBACK(on_dpi_color_changed), NULL);
            (void)n;
        }
    }
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
    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0)
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

GtkWidget *build_mouse_config_page(App *app)
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

    GtkWidget *dpi_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(dpi_btn_box, 4);

    GtkWidget *dpi_apply = gtk_button_new_with_label(_("Apply DPI"));
    gtk_widget_add_css_class(dpi_apply, "suggested-action");
    gtk_widget_add_css_class(dpi_apply, "pill");
    g_signal_connect(dpi_apply, "clicked", G_CALLBACK(on_dpi_apply), app);
    gtk_box_append(GTK_BOX(dpi_btn_box), dpi_apply);

    GtkWidget *dpi_reset = gtk_button_new_with_label(_("Reset to factory defaults"));
    gtk_widget_add_css_class(dpi_reset, "pill");
    g_signal_connect(dpi_reset, "clicked", G_CALLBACK(on_dpi_reset_clicked), app);
    gtk_box_append(GTK_BOX(dpi_btn_box), dpi_reset);

    gtk_widget_set_halign(dpi_btn_box, GTK_ALIGN_START);
    adw_preferences_group_add(dpi_group, dpi_btn_box);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dpi_group);

    /* dpi indicator colour -- family 0x04, see docs/reverse-engineering.md.
     * Each stage's colour applies immediately on pick (matches the
     * official driver's own colour-picker UX, no separate Apply button). */
    AdwPreferencesGroup *dpi_color_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(dpi_color_group, _("DPI indicator colour"));
    adw_preferences_group_set_description(dpi_color_group,
        _("Colour of the DPI LED for each stage"));
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        char title[16];
        snprintf(title, sizeof(title), _("Stage %d"), i + 1);
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

        GtkColorDialog *dialog = gtk_color_dialog_new();
        GtkWidget *btn = gtk_color_dialog_button_new(dialog);
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
        app->dpi_color_btn[i] = GTK_COLOR_DIALOG_BUTTON(btn);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(btn, "notify::rgba", G_CALLBACK(on_dpi_color_changed), data, free_closure_data, 0);

        adw_action_row_add_suffix(row, btn);
        adw_preferences_group_add(dpi_color_group, GTK_WIDGET(row));
    }
    GtkWidget *dpicolor_reset = gtk_button_new_with_label(_("Reset to factory defaults"));
    gtk_widget_add_css_class(dpicolor_reset, "pill");
    gtk_widget_set_halign(dpicolor_reset, GTK_ALIGN_START);
    gtk_widget_set_margin_top(dpicolor_reset, 4);
    g_signal_connect(dpicolor_reset, "clicked", G_CALLBACK(on_dpicolor_reset_clicked), app);
    adw_preferences_group_add(dpi_color_group, dpicolor_reset);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dpi_color_group);

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
    adw_action_row_set_subtitle(lift_row, _("Not in the official driver's UI for this mouse -- the firmware accepts it, AJAZZ's own app just doesn't expose it. May also control \"wake mouse on motion\" -- see Home"));
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
