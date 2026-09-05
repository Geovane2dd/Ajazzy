/*
 * Lighting page: RGB effect (off / constant / rainbow), brightness, and
 * rainbow speed. The mouse firmware only implements those three modes
 * -- see docs/reverse-engineering.md's "What's still unknown" section
 * for why there's no separate "breathing" effect.
 *
 * The two "software" effects below are NOT firmware effects -- they're
 * this app repeatedly re-sending real SET commands on a timer to
 * animate something the firmware only supports as a static value:
 *
 * - "Software colour cycle" rewrites the ACTIVE DPI stage's indicator
 *   colour (family 0x04, ajazz_build_set_dpi_colors()) with a
 *   continuously rotating hue, while "constant colour" mode is on --
 *   that mode lights the same LED the active DPI stage's colour drives
 *   (the constant-colour SET command carries no RGB of its own, only
 *   brightness -- see ajazz_build_set_rgb_constant() in
 *   protocol/hid.h), so changing the DPI colour changes what's lit.
 *   This is genuine real-time colour changes on the mouse, not a
 *   brightness fade -- what was actually asked for.
 * - "Software breathing" fades that same constant-colour brightness up
 *   and down instead of changing hue, for a slower ambient pulse at
 *   whatever colour is already set.
 *
 * Both are software animations layered on top of real firmware modes,
 * not new wire commands, and both stop the moment the app closes.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "../devices/device.h"


enum { RGB_OFF, RGB_RAINBOW, RGB_CONSTANT, RGB_SOFT_COLORCYCLE, RGB_SOFT_BREATHE };

/* There's no arbitrary colour picker -- rainbow uses a fixed palette and
 * off/constant don't carry a colour choice at all -- so this shows the
 * effect itself: the fixed rainbow gradient, the brand accent for
 * constant (scaled by brightness), a dark swatch for off. */
static void update_rgb_preview(App *app)
{
    guint sel = adw_combo_row_get_selected(app->rgb_effect);
    const char *want = (sel == RGB_OFF) ? "ajazz-rgb-off"
                      : (sel == RGB_CONSTANT || sel == RGB_SOFT_BREATHE) ? "ajazz-rgb-constant"
                      : (sel == RGB_SOFT_COLORCYCLE) ? "ajazz-rgb-rainbow"
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
    gtk_widget_set_visible(GTK_WIDGET(app->rgb_speed_row),
        sel == RGB_RAINBOW || sel == RGB_SOFT_BREATHE || sel == RGB_SOFT_COLORCYCLE);
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

/* Stops whichever software effect loop is running, if any. Safe to
 * call unconditionally (e.g. before applying any other effect, or on
 * shutdown). */
static void stop_rgb_soft_effect(App *app)
{
    if (app->rgb_soft_timer_id != 0) {
        g_source_remove(app->rgb_soft_timer_id);
        app->rgb_soft_timer_id = 0;
    }
}

/* Standard HSV(0-360, 0-1, 0-1) -> RGB(0-255) conversion, hue only
 * needing to sweep the full circle for a colour-cycle effect. */
static void hsv_to_rgb(double h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double c = 1.0;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double rp, gp, bp;
    if      (h < 60)  { rp = c; gp = x; bp = 0; }
    else if (h < 120) { rp = x; gp = c; bp = 0; }
    else if (h < 180) { rp = 0; gp = c; bp = x; }
    else if (h < 240) { rp = 0; gp = x; bp = c; }
    else if (h < 300) { rp = x; gp = 0; bp = c; }
    else              { rp = c; gp = 0; bp = x; }
    *r = (uint8_t)(rp * 255.0 + 0.5);
    *g = (uint8_t)(gp * 255.0 + 0.5);
    *b = (uint8_t)(bp * 255.0 + 0.5);
}

/* One frame of the software colour-cycle effect: rewrites the active
 * DPI stage's indicator colour (see the file comment) to a hue that
 * rotates through the full spectrum over time, so the LED that
 * "constant colour" mode lights actually changes colour live -- not
 * just brightness. Ticks every 150ms; the Speed row controls how fast
 * the hue rotates. */
static gboolean rgb_soft_colorcycle_tick(gpointer user_data)
{
    App *app = user_data;
    if (!app->connected) {
        app->rgb_soft_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double speed = adw_spin_row_get_value(app->rgb_speed);
    double step = 2.0 + speed * 3.0; /* degrees of hue per tick */

    int stage = app->dpi_selected;
    if (stage < 0 || stage >= AJAZZ_DPI_STAGES) stage = 0;
    hsv_to_rgb(app->rgb_soft_phase,
        &app->dpi_color_cache[stage][0], &app->dpi_color_cache[stage][1], &app->dpi_color_cache[stage][2]);

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);

    if (ajazz_send_mt(app, &req) == 0) {
        app->rgb_soft_fail_count = 0;
    } else {
        app->rgb_soft_fail_count++;
        if (app->rgb_soft_fail_count >= 5) {
            app->rgb_soft_timer_id = 0;
            app->rgb_soft_fail_count = 0;
            toast(app, _("Software colour cycle stopped (mouse not responding)."));
            return G_SOURCE_REMOVE;
        }
    }

    app->rgb_soft_phase += step;
    if (app->rgb_soft_phase >= 360.0)
        app->rgb_soft_phase -= 360.0;

    return G_SOURCE_CONTINUE;
}

/* One frame of the software breathing effect: re-sends "constant
 * colour" with a brightness that rises and falls in a sine wave, so the
 * LED fades in and out at whatever colour the active DPI stage's
 * indicator is already set to (see the file comment). Ticks every
 * 120ms; the Speed row (reused from rainbow, 0-4) controls how fast the
 * wave moves, and Brightness caps how bright the peak gets. Uses a
 * fire-and-forget send (no ack wait) so a slow/unplugged mouse can't
 * stall the GTK main loop -- stops itself after a few consecutive
 * failures instead. */
static gboolean rgb_soft_breathe_tick(gpointer user_data)
{
    App *app = user_data;
    if (!app->connected) {
        app->rgb_soft_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double peak = adw_spin_row_get_value(app->rgb_brightness);
    double speed = adw_spin_row_get_value(app->rgb_speed);
    double step = 0.05 + speed * 0.05; /* radians per tick */

    double wave = (sin(app->rgb_soft_phase) + 1.0) / 2.0; /* 0..1 */
    uint8_t brightness = (uint8_t)(wave * peak + 0.5);

    ajazz_report_t req;
    ajazz_build_set_rgb_constant(&req, brightness);

    if (ajazz_send_mt(app, &req) == 0) {
        app->rgb_soft_fail_count = 0;
    } else {
        app->rgb_soft_fail_count++;
        if (app->rgb_soft_fail_count >= 5) {
            app->rgb_soft_timer_id = 0;
            app->rgb_soft_fail_count = 0;
            toast(app, _("Software breathing stopped (mouse not responding)."));
            return G_SOURCE_REMOVE;
        }
    }

    app->rgb_soft_phase += step;
    if (app->rgb_soft_phase > 2 * G_PI)
        app->rgb_soft_phase -= 2 * G_PI;

    return G_SOURCE_CONTINUE;
}

static void on_rgb_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    guint effect = adw_combo_row_get_selected(app->rgb_effect);
    uint8_t brightness = (uint8_t)adw_spin_row_get_value(app->rgb_brightness);

    if (effect == RGB_SOFT_COLORCYCLE || effect == RGB_SOFT_BREATHE) {
        /* Turn the LED on at constant colour first (a confirmed,
         * synchronous SET), then hand off to the timer to animate it --
         * rather than starting the loop on a possibly-off LED and
         * waiting for the first tick to light it. */
        ajazz_report_t req;
        ajazz_build_set_rgb_constant(&req, brightness);
        if (ajazz_send_and_wait_mt(app, &req, 1000) != 0) {
            toast(app, _("Failed to apply lighting."));
            return;
        }
        stop_rgb_soft_effect(app);
        app->rgb_soft_phase = 0;
        app->rgb_soft_fail_count = 0;
        if (effect == RGB_SOFT_COLORCYCLE) {
            app->rgb_soft_timer_id = g_timeout_add(150, rgb_soft_colorcycle_tick, app);
            toast(app, _("Software colour cycle started -- rotates the active DPI stage's colour live."));
        } else {
            app->rgb_soft_timer_id = g_timeout_add(120, rgb_soft_breathe_tick, app);
            toast(app, _("Software breathing started -- fades the current DPI stage colour in and out."));
        }
        return;
    }

    stop_rgb_soft_effect(app);

    ajazz_report_t req;
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

    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0)
        toast(app, _("Lighting applied."));
    else
        toast(app, _("Failed to apply lighting."));
}

/* Heap-allocated result of the fetch thread below, handed to
 * lighting_fetch_apply() on the main thread via g_idle_add(). */
typedef struct {
    App *app;
    gboolean ok;
    gboolean recognized;
    ajazz_led_state_t st;
} LightingFetchResult;

static gboolean lighting_fetch_apply(gpointer user_data)
{
    LightingFetchResult *res = user_data;
    App *app = res->app;
    app->lighting_fetch_pending = FALSE;

    if (!app->connected || !res->ok || !res->recognized) {
        /* Not connected, query failed, or an unrecognized opcode --
         * leave the controls as they are, same as the original code. */
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    guint effect_sel;
    switch (res->st.effect) {
        case AJAZZ_LED_OFF:      effect_sel = RGB_OFF;      break;
        case AJAZZ_LED_CONSTANT: effect_sel = RGB_CONSTANT; break;
        default:                 effect_sel = RGB_RAINBOW;  break;
    }
    adw_combo_row_set_selected(app->rgb_effect, effect_sel);
    if (effect_sel != RGB_OFF)
        adw_spin_row_set_value(app->rgb_brightness, res->st.brightness);
    if (effect_sel == RGB_RAINBOW)
        adw_spin_row_set_value(app->rgb_speed, res->st.speed);
    update_rgb_row_visibility(app);

    g_free(res);
    return G_SOURCE_REMOVE;
}

/* Runs off the main thread -- MUST NOT touch any GTK widget, only
 * app->dev via the *_mt() wrapper (see App::hid_lock) and the heap
 * result handed to lighting_fetch_apply(). */
static gpointer lighting_fetch_thread(gpointer user_data)
{
    App *app = user_data;
    LightingFetchResult *res = g_new0(LightingFetchResult, 1);
    res->app = app;

    ajazz_report_t reply;
    res->ok = (ajazz_query_mt(app, AJAZZ_QUERY_LED_MODE, &reply, 1000) == 0);
    if (res->ok)
        res->recognized = (ajazz_parse_led_mode(&reply, &res->st) == 0);

    g_idle_add(lighting_fetch_apply, res);
    return NULL;
}

/* Called every time the Lighting page becomes visible, same as the other
 * pages. All three effects (off/constant/rainbow) and their
 * brightness/speed are confirmed against real hardware, see the comment
 * on AJAZZ_QUERY_LED_MODE in protocol/hid.h. An unrecognized opcode just
 * leaves the controls alone rather than guessing. Reads on a background
 * thread so a slow/unplugged mouse can't freeze the window -- see
 * lighting_fetch_thread()/lighting_fetch_apply() and the comment on
 * App::hid_lock. */
void refresh_lighting(App *app)
{
    if (!app->connected)
        return;
    if (app->lighting_fetch_pending)
        return;
    app->lighting_fetch_pending = TRUE;
    g_thread_unref(g_thread_new("ajazzy-lighting-fetch", lighting_fetch_thread, app));
}

GtkWidget *build_lighting_page(App *app)
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
        _("\"Off\", rainbow cycle and constant colour are real firmware modes, confirmed against real hardware. The two \"Software\" effects aren't -- they're this app repeatedly re-sending real colour/brightness commands on a timer, so they stop if the app closes."));

    app->rgb_effect = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_effect), _("Effect"));
    const char *effect_names[] = {N_("Off"), N_("Rainbow cycle"), N_("Constant colour"),
                                   N_("Software colour cycle (real-time)"),
                                   N_("Software breathing (DPI stage colour)"), NULL};
    const char *effect_names_tr[6];
    for (int i = 0; i < 5; i++) effect_names_tr[i] = gettext(effect_names[i]);
    effect_names_tr[5] = NULL;
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
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Apply lighting"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    gtk_widget_set_halign(btn_apply, GTK_ALIGN_START);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_rgb_apply), app);
    adw_preferences_group_add(apply_group, btn_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), apply_group);

    return page;
}
