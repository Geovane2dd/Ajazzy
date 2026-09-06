/*
 * Lighting page: RGB effect (off / constant / rainbow), brightness, and
 * rainbow speed. The mouse firmware only implements those three modes
 * -- see docs/reverse-engineering.md's "What's still unknown" section
 * for why there's no separate "breathing" effect.
 *
 * RGB_SOFT_BREATHING and RGB_SOFT_COLORCYCLE below are NOT firmware
 * effects -- they're this app repeatedly re-sending real SET commands on
 * a timer to animate something the firmware only supports as a static
 * value. Both rewrite EVERY DPI stage's indicator colour (family 0x04,
 * ajazz_build_set_dpi_colors()) on a tick, while "constant colour" mode
 * is on -- that mode lights the same LED the DPI stage indicator colour
 * drives (the constant-colour SET command carries no RGB of its own,
 * only brightness -- see ajazz_build_set_rgb_constant() in
 * protocol/hid.h). Writing the same colour to all 6 stages (not just
 * whichever one the UI last had selected) means the light stays lit and
 * animating regardless of which stage the mouse actually has active.
 * This is a small software effects engine layered on top of one real
 * firmware mode, not new wire commands, and it stops the moment the app
 * closes -- a real "breathing" or "wave" firmware mode does not exist on
 * this device (see AJAZZ_LED_* in protocol/hid.h), so this is as close
 * as software can get without inventing hardware capability. Two kinds,
 * not a gallery of them, since a fixed-colour LED only really supports
 * "smoothly oscillate its brightness" (breathing) and "smoothly rotate
 * its hue" (colour cycle) -- most gamer-mouse-software effect names
 * beyond those two are just those same two curves applied to more LEDs
 * than this mouse has.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "jsonutil.h"
#include "../devices/device.h"


enum { RGB_OFF, RGB_RAINBOW, RGB_CONSTANT, RGB_SOFT_BREATHING, RGB_SOFT_COLORCYCLE, RGB_SOFT_CUSTOM };
#define RGB_EFFECT_COUNT (RGB_SOFT_CUSTOM + 1)

static GtkWidget *g_lighting_presets_row; /* rebuilt by populate_custom_presets() */
static void populate_custom_presets(App *app);

static void update_rgb_row_visibility(App *app)
{
    guint sel = adw_combo_row_get_selected(app->rgb_effect);
    gtk_widget_set_visible(GTK_WIDGET(app->rgb_brightness), sel != RGB_OFF);
    gtk_widget_set_visible(GTK_WIDGET(app->rgb_speed_row),
        sel == RGB_RAINBOW || sel == RGB_SOFT_BREATHING || sel == RGB_SOFT_COLORCYCLE || sel == RGB_SOFT_CUSTOM);
    if (app->rgb_soft_color_row)
        gtk_widget_set_visible(GTK_WIDGET(app->rgb_soft_color_row), sel == RGB_SOFT_BREATHING || sel == RGB_SOFT_CUSTOM);
    if (app->rgb_custom_row)
        gtk_widget_set_visible(GTK_WIDGET(app->rgb_custom_row), sel == RGB_SOFT_CUSTOM);
}

static void sync_rgb_listbox_from_combo(App *app);
static void on_rgb_effect_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd; (void)pspec;
    App *app = user_data;
    update_rgb_row_visibility(app);
    sync_rgb_listbox_from_combo(app);
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

/* One frame of whichever software effect is running (app->rgb_soft_effect_kind),
 * shared by both since they only differ in how they turn app->rgb_soft_phase
 * into a colour -- everything else (the trylock send, the fail-count
 * watchdog, advancing the phase) is identical. Ticks every 150ms; the
 * Speed row controls how fast the phase advances. */
static gboolean rgb_soft_tick(gpointer user_data)
{
    App *app = user_data;
    if (!app->connected) {
        app->rgb_soft_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double speed = adw_spin_row_get_value(app->rgb_speed);
    uint8_t r, g, b;

    if (app->rgb_soft_effect_kind == RGB_SOFT_BREATHING) {
        /* Brightness (not hue) oscillates 0% -> 100% -> 0%, smoothly (a
         * sine wave), around a fixed user-picked colour -- the LED
         * itself only accepts an RGB value, not a separate brightness
         * knob fine enough for this, so "dimming" is done by scaling
         * the colour components down toward black and back up. */
        double step = 0.03 + speed * 0.05; /* radians of phase per tick */
        double frac = (sin(app->rgb_soft_phase) + 1.0) / 2.0; /* 0..1 */
        r = (uint8_t)(app->rgb_soft_color[0] * frac + 0.5);
        g = (uint8_t)(app->rgb_soft_color[1] * frac + 0.5);
        b = (uint8_t)(app->rgb_soft_color[2] * frac + 0.5);
        app->rgb_soft_phase += step;
        if (app->rgb_soft_phase >= 2 * G_PI)
            app->rgb_soft_phase -= 2 * G_PI;
    } else if (app->rgb_soft_effect_kind == RGB_SOFT_CUSTOM) {
        /* Custom: play through the user's own colour sequence, either
         * cross-fading from one to the next or hard-cutting between
         * them. rgb_soft_phase here is a position along the sequence
         * (0..count), not degrees/radians like the other two kinds. */
        int n = app->rgb_custom_count;
        if (n <= 0) {
            r = app->rgb_soft_color[0]; g = app->rgb_soft_color[1]; b = app->rgb_soft_color[2];
        } else if (n == 1) {
            r = app->rgb_custom_colors[0][0]; g = app->rgb_custom_colors[0][1]; b = app->rgb_custom_colors[0][2];
        } else {
            double step = 0.01 + speed * 0.02; /* sequence positions per tick */
            int idx = (int)app->rgb_soft_phase % n;
            int next = (idx + 1) % n;
            double frac = app->rgb_soft_phase - (double)(int)app->rgb_soft_phase;
            if (app->rgb_custom_transition == 0) {
                r = (uint8_t)(app->rgb_custom_colors[idx][0] * (1 - frac) + app->rgb_custom_colors[next][0] * frac + 0.5);
                g = (uint8_t)(app->rgb_custom_colors[idx][1] * (1 - frac) + app->rgb_custom_colors[next][1] * frac + 0.5);
                b = (uint8_t)(app->rgb_custom_colors[idx][2] * (1 - frac) + app->rgb_custom_colors[next][2] * frac + 0.5);
            } else {
                r = app->rgb_custom_colors[idx][0]; g = app->rgb_custom_colors[idx][1]; b = app->rgb_custom_colors[idx][2];
            }
            app->rgb_soft_phase += step;
            if (app->rgb_soft_phase >= n)
                app->rgb_soft_phase -= n;
        }
    } else {
        /* Colour cycle: hue rotates through the full spectrum. */
        double step = 2.0 + speed * 3.0; /* degrees of hue per tick */
        hsv_to_rgb(app->rgb_soft_phase, &r, &g, &b);
        app->rgb_soft_phase += step;
        if (app->rgb_soft_phase >= 360.0)
            app->rgb_soft_phase -= 360.0;
    }

    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        app->dpi_color_cache[i][0] = r;
        app->dpi_color_cache[i][1] = g;
        app->dpi_color_cache[i][2] = b;
    }

    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);

    /* trylock, not the blocking ajazz_send_mt(): this runs on the main
     * thread every 150ms, and a page's background refresh can hold
     * hid_lock for up to a second -- blocking here would freeze the
     * whole window for that long every time a page fetch and a tick
     * happen to overlap. A skipped tick just means the animation misses
     * one frame; it keeps advancing next tick. */
    int rc = ajazz_send_trylock_mt(app, &req);
    if (rc == 0 || rc == 1) {
        app->rgb_soft_fail_count = 0;
    } else {
        app->rgb_soft_fail_count++;
        if (app->rgb_soft_fail_count >= 5) {
            app->rgb_soft_timer_id = 0;
            app->rgb_soft_fail_count = 0;
            toast(app, _("Software lighting effect stopped (mouse not responding)."));
            return G_SOURCE_REMOVE;
        }
    }

    return G_SOURCE_CONTINUE;
}

/* Just caches the picked colour for the next breathing tick to use --
 * doesn't send anything itself, since breathing only actually starts
 * from on_rgb_apply(). */
static void on_rgb_soft_color_changed(GtkColorDialogButton *btn, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(btn);
    app->rgb_soft_color[0] = (uint8_t)(rgba->red * 255.0 + 0.5);
    app->rgb_soft_color[1] = (uint8_t)(rgba->green * 255.0 + 0.5);
    app->rgb_soft_color[2] = (uint8_t)(rgba->blue * 255.0 + 0.5);
}

/* Redraws the Custom effect's swatch row from app->rgb_custom_colors --
 * called after adding or removing a colour. Each swatch is itself a
 * button so clicking one removes it (no separate delete icon needed for
 * a list this short). */
static void on_custom_swatch_clicked(GtkButton *btn, gpointer user_data);

static void rebuild_custom_swatches(App *app)
{
    if (!app->rgb_custom_swatches_box)
        return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->rgb_custom_swatches_box)))
        gtk_box_remove(GTK_BOX(app->rgb_custom_swatches_box), child);

    for (int i = 0; i < app->rgb_custom_count; i++) {
        GtkWidget *swatch = gtk_button_new();
        gtk_widget_add_css_class(swatch, "circular");
        gtk_widget_set_size_request(swatch, 28, 28);
        gtk_widget_set_tooltip_text(swatch, _("Click to remove"));
        char name[24];
        snprintf(name, sizeof(name), "ajazz-swatch-%d", i);
        gtk_widget_set_name(swatch, name);
        char css[96];
        snprintf(css, sizeof(css), "#%s { background-color: #%02x%02x%02x; }", name,
            app->rgb_custom_colors[i][0], app->rgb_custom_colors[i][1], app->rgb_custom_colors[i][2]);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(swatch, "clicked", G_CALLBACK(on_custom_swatch_clicked), data, free_closure_data, 0);
        gtk_box_append(GTK_BOX(app->rgb_custom_swatches_box), swatch);
    }
}

static void on_custom_swatch_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    int idx = GPOINTER_TO_INT(data[1]);
    if (idx < 0 || idx >= app->rgb_custom_count)
        return;
    for (int i = idx; i < app->rgb_custom_count - 1; i++)
        memcpy(app->rgb_custom_colors[i], app->rgb_custom_colors[i + 1], 3);
    app->rgb_custom_count--;
    rebuild_custom_swatches(app);
}

static void on_custom_transition_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    app->rgb_custom_transition = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(dd));
}

static void on_custom_add_color_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (app->rgb_custom_count >= 8) {
        toast(app, _("Custom sequences hold up to 8 colours."));
        return;
    }
    memcpy(app->rgb_custom_colors[app->rgb_custom_count], app->rgb_soft_color, 3);
    app->rgb_custom_count++;
    rebuild_custom_swatches(app);
}

static void on_rgb_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    guint effect = adw_combo_row_get_selected(app->rgb_effect);
    uint8_t brightness = (uint8_t)adw_spin_row_get_value(app->rgb_brightness);

    if (effect == RGB_SOFT_BREATHING || effect == RGB_SOFT_COLORCYCLE || effect == RGB_SOFT_CUSTOM) {
        if (effect == RGB_SOFT_CUSTOM && app->rgb_custom_count == 0) {
            toast(app, _("Add at least one colour to the sequence first."));
            return;
        }
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
        app->rgb_soft_effect_kind = (int)effect;
        app->rgb_soft_phase = 0;
        app->rgb_soft_fail_count = 0;
        app->rgb_soft_timer_id = g_timeout_add(150, rgb_soft_tick, app);
        toast(app, effect == RGB_SOFT_BREATHING ? _("Breathing effect started -- fades the mouse's colour live.")
            : effect == RGB_SOFT_CUSTOM ? _("Custom effect started -- plays your colour sequence live.")
            : _("Rainbow started -- rotates the mouse's colour live."));
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

/* The visible effect picker is a plain GtkListBox on the left; the
 * pre-existing app->rgb_effect AdwComboRow still holds the actual
 * "selected effect" state (every other function in this file reads/sets
 * it), it's just never shown -- keeping it as the single source of truth
 * means on_rgb_apply()/lighting_fetch_apply() need no changes at all. */
static void on_rgb_listbox_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row)
        return;
    adw_combo_row_set_selected(app->rgb_effect, (guint)gtk_list_box_row_get_index(row));
}

/* Keeps the left-column listbox in sync whenever app->rgb_effect's
 * selection changes programmatically (device fetch applying real state,
 * or the software colour-cycle path) rather than by clicking a row. */
static GtkListBox *g_rgb_listbox;
static void sync_rgb_listbox_from_combo(App *app)
{
    if (!g_rgb_listbox)
        return;
    guint sel = adw_combo_row_get_selected(app->rgb_effect);
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(g_rgb_listbox, (int)sel);
    if (row)
        gtk_list_box_select_row(g_rgb_listbox, row);
}

/* Lighting preset tile: sets the effect combo + brightness/colour to the
 * preset's fixed values and immediately applies (on_rgb_apply()), same
 * as picking the effect from the list and clicking Apply by hand. For a
 * constant-colour preset, the colour is sent via the DPI indicator
 * colour command (see the file comment -- that's the LED "constant
 * colour" mode lights). */
static void on_lighting_preset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = ((gpointer *)user_data)[0];
    guint idx = GPOINTER_TO_UINT(((gpointer *)user_data)[1]);
    static const struct { guint effect; uint8_t r, g, b; } presets[] = {
        {RGB_CONSTANT, 255, 40, 40}, {RGB_CONSTANT, 40, 220, 90},
        {RGB_CONSTANT, 60, 110, 255}, {RGB_RAINBOW, 0, 0, 0},
    };
    if (idx >= G_N_ELEMENTS(presets)) return;

    adw_combo_row_set_selected(app->rgb_effect, presets[idx].effect);
    adw_spin_row_set_value(app->rgb_brightness, 4);

    if (presets[idx].effect == RGB_CONSTANT && ensure_connected(app)) {
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
            app->dpi_color_cache[i][0] = presets[idx].r;
            app->dpi_color_cache[i][1] = presets[idx].g;
            app->dpi_color_cache[i][2] = presets[idx].b;
        }
        ajazz_report_t req;
        ajazz_build_set_dpi_colors(&req, app->dpi_color_cache);
        ajazz_send_and_wait_mt(app, &req, 1000);
    }
    on_rgb_apply(NULL, app);
}

/* Public wrapper for gui/page_profiles.c -- see the declaration in
 * app.h. Sets the widgets to the saved values, same as picking them by
 * hand, then reuses on_rgb_apply() so the actual send/soft-effect-start
 * logic isn't duplicated. */
void lighting_apply_profile(App *app, int effect, int brightness, int speed, const uint8_t color[3])
{
    /* Custom (its whole colour sequence, not just one colour) isn't
     * representable through this single-colour signature -- profiles
     * and the hardcoded preset shortcuts don't carry a colour list, so
     * a saved Custom effect can't be restored through this path.
     * Custom's own presets (page_lighting.c's on_custom_preset_clicked)
     * bypass this function entirely for that reason. */
    if (effect < RGB_OFF || effect > RGB_SOFT_COLORCYCLE)
        return;
    adw_combo_row_set_selected(app->rgb_effect, (guint)effect);
    adw_spin_row_set_value(app->rgb_brightness, brightness);
    adw_spin_row_set_value(app->rgb_speed, speed);
    if (color && app->rgb_soft_color_btn) {
        app->rgb_soft_color[0] = color[0];
        app->rgb_soft_color[1] = color[1];
        app->rgb_soft_color[2] = color[2];
        GdkRGBA rgba = {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, 1.0f};
        g_signal_handlers_block_matched(app->rgb_soft_color_btn, G_SIGNAL_MATCH_FUNC,
            0, 0, NULL, G_CALLBACK(on_rgb_soft_color_changed), NULL);
        gtk_color_dialog_button_set_rgba(app->rgb_soft_color_btn, &rgba);
        g_signal_handlers_unblock_matched(app->rgb_soft_color_btn, G_SIGNAL_MATCH_FUNC,
            0, 0, NULL, G_CALLBACK(on_rgb_soft_color_changed), NULL);
    }
    update_rgb_row_visibility(app);
    sync_rgb_listbox_from_combo(app);
    on_rgb_apply(NULL, app);
}

/* Applies a custom saved preset -- same idea as on_lighting_preset_clicked()
 * above, just reading {effect, colour, brightness, speed} from a JSON
 * file instead of a hardcoded C array. Reuses lighting_apply_profile()
 * (see app.h) so this file only has ONE place that actually pokes the
 * effect/brightness/speed/colour widgets and calls on_rgb_apply(). */
static void on_custom_preset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    char *path = data[1];

    char *text = json_read_file(path);
    if (!text) { toast(app, _("Failed to read that preset.")); return; }

    int effect = json_get_int(text, "effect", RGB_CONSTANT);
    int brightness = json_get_int(text, "brightness", 4);
    int speed = json_get_int(text, "speed", 2);

    if (effect == RGB_SOFT_CUSTOM) {
        /* Custom presets carry a whole colour list, which
         * lighting_apply_profile()'s single-colour signature can't
         * take -- set every widget by hand here instead. */
        int flat[24] = {0};
        int n = json_get_int_array(text, "custom_colors", flat, 24) / 3;
        if (n > 8) n = 8;
        int transition = json_get_int(text, "transition", 0);
        g_free(text);

        if (n <= 0) { toast(app, _("That preset has no colours saved.")); return; }

        app->rgb_custom_count = n;
        for (int i = 0; i < n; i++) {
            app->rgb_custom_colors[i][0] = (uint8_t)flat[i * 3 + 0];
            app->rgb_custom_colors[i][1] = (uint8_t)flat[i * 3 + 1];
            app->rgb_custom_colors[i][2] = (uint8_t)flat[i * 3 + 2];
        }
        app->rgb_custom_transition = transition;
        rebuild_custom_swatches(app);
        adw_combo_row_set_selected(app->rgb_custom_transition_row, (guint)transition);
        adw_combo_row_set_selected(app->rgb_effect, RGB_SOFT_CUSTOM);
        adw_spin_row_set_value(app->rgb_brightness, brightness);
        adw_spin_row_set_value(app->rgb_speed, speed);
        on_rgb_apply(NULL, app);
        return;
    }

    int color_ints[3] = {255, 255, 255};
    json_get_int_array(text, "color", color_ints, 3);
    uint8_t color[3] = {(uint8_t)color_ints[0], (uint8_t)color_ints[1], (uint8_t)color_ints[2]};
    g_free(text);

    lighting_apply_profile(app, effect, brightness, speed, color);
}

static void free_gpointer_pair_lighting(gpointer data, GClosure *closure)
{
    (void)closure;
    gpointer *pair = data;
    g_free(pair[1]);
    g_free(pair);
}

/* Scans ~/.config/ajazzy/lighting_presets/ for saved preset JSON files and appends one tile per
 * file to g_lighting_presets_row, after the 4 hardcoded ones -- called
 * once at page build and again after saving a new preset. */
static void populate_custom_presets(App *app)
{
    /* Remove any previously-added custom tiles (marked via this data key)
     * before repopulating, so re-saving doesn't duplicate them. */
    GtkWidget *child = gtk_widget_get_first_child(g_lighting_presets_row);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "ajazzy-custom-preset"))
            gtk_box_remove(GTK_BOX(g_lighting_presets_row), child);
        child = next;
    }

    char *dir = ajazzy_config_subdir("lighting_presets");
    if (!dir) return;
    GDir *gdir = g_dir_open(dir, 0, NULL);
    if (!gdir) { g_free(dir); return; }

    const char *entry;
    while ((entry = g_dir_read_name(gdir))) {
        if (!g_str_has_suffix(entry, ".json"))
            continue;
        char *path = g_build_filename(dir, entry, NULL);
        char *text = json_read_file(path);
        if (!text) { g_free(path); continue; }

        char name[64];
        if (!json_get_string(text, "name", name, sizeof(name)))
            g_strlcpy(name, entry, sizeof(name));
        g_free(text);

        GtkWidget *tile = gtk_button_new();
        gtk_widget_add_css_class(tile, "flat");
        g_object_set_data(G_OBJECT(tile), "ajazzy-custom-preset", GINT_TO_POINTER(1));
        GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic");
        GtkWidget *name_lbl = gtk_label_new(name);
        GtkWidget *type_lbl = gtk_label_new(_("Custom preset"));
        gtk_widget_add_css_class(type_lbl, "dim-label");
        gtk_widget_add_css_class(type_lbl, "caption");
        gtk_box_append(GTK_BOX(tbox), icon);
        gtk_box_append(GTK_BOX(tbox), name_lbl);
        gtk_box_append(GTK_BOX(tbox), type_lbl);
        gtk_button_set_child(GTK_BUTTON(tile), tbox);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = path; /* owned by the closure now */
        g_signal_connect_data(tile, "clicked", G_CALLBACK(on_custom_preset_clicked), data, free_gpointer_pair_lighting, 0);
        gtk_box_append(GTK_BOX(g_lighting_presets_row), tile);
    }
    g_dir_close(gdir);
    g_free(dir);
}

static GtkWidget *g_save_preset_name_entry;
static void on_save_lighting_preset_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    App *app = user_data;
    if (strcmp(response, "save") != 0)
        return;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(g_save_preset_name_entry));
    if (!name || !*name) { toast(app, _("Enter a preset name.")); return; }

    char *dir = ajazzy_config_subdir("lighting_presets");
    if (!dir) { toast(app, _("Failed to save the preset.")); return; }
    char *safe_name = g_strdup(name);
    for (char *p = safe_name; *p; p++)
        if (*p == '/' || *p == '\\') *p = '_';
    char *filename = g_strdup_printf("%s.json", safe_name);
    char *path = g_build_filename(dir, filename, NULL);

    GString *out = g_string_new("{\n  ");
    json_put_string(out, "name", name); g_string_append(out, ",\n  ");
    json_put_int(out, "effect", (int)adw_combo_row_get_selected(app->rgb_effect)); g_string_append(out, ",\n  ");
    json_put_int(out, "brightness", (int)adw_spin_row_get_value(app->rgb_brightness)); g_string_append(out, ",\n  ");
    json_put_int(out, "speed", (int)adw_spin_row_get_value(app->rgb_speed)); g_string_append(out, ",\n  ");
    int color_ints[3] = {app->rgb_soft_color[0], app->rgb_soft_color[1], app->rgb_soft_color[2]};
    json_put_int_array(out, "color", color_ints, 3);
    if ((int)adw_combo_row_get_selected(app->rgb_effect) == RGB_SOFT_CUSTOM) {
        g_string_append(out, ",\n  ");
        int flat[24];
        for (int i = 0; i < app->rgb_custom_count; i++) {
            flat[i * 3 + 0] = app->rgb_custom_colors[i][0];
            flat[i * 3 + 1] = app->rgb_custom_colors[i][1];
            flat[i * 3 + 2] = app->rgb_custom_colors[i][2];
        }
        json_put_int_array(out, "custom_colors", flat, app->rgb_custom_count * 3);
        g_string_append(out, ",\n  ");
        json_put_int(out, "transition", app->rgb_custom_transition);
    }
    g_string_append(out, "\n}\n");

    gboolean ok = json_write_file(path, out->str);
    g_string_free(out, TRUE);
    g_free(dir); g_free(safe_name); g_free(filename); g_free(path);

    toast(app, ok ? _("Preset saved.") : _("Failed to save the preset."));
    if (ok)
        populate_custom_presets(app);
}

static void on_save_lighting_preset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Save as preset"),
        _("Saves the current effect, brightness, speed and colour as a one-click preset, stored on this PC.")));
    g_save_preset_name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_save_preset_name_entry), _("Preset name"));
    adw_alert_dialog_set_extra_child(dialog, g_save_preset_name_entry);
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "save", _("Save"));
    adw_alert_dialog_set_response_appearance(dialog, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "save");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_save_lighting_preset_dialog_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

GtkWidget *build_lighting_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("RGB lighting"));
    adw_preferences_group_set_description(group,
        _("\"Off\" and \"Constant colour\" work exactly as shown. The others animate the light live from this app -- they stop if it closes."));

    /* backing model for the actual selection -- not shown, see the
     * comment on on_rgb_listbox_row_selected() above */
    app->rgb_effect = ADW_COMBO_ROW(adw_combo_row_new());
    const char *effect_names[] = {N_("Off"), N_("Colour cycle (built-in)"), N_("Constant colour"),
                                   N_("Breathing"), N_("Rainbow"), N_("Custom"), NULL};
    const char *effect_names_tr[RGB_EFFECT_COUNT + 1];
    for (int i = 0; i < RGB_EFFECT_COUNT; i++) effect_names_tr[i] = gettext(effect_names[i]);
    effect_names_tr[RGB_EFFECT_COUNT] = NULL;
    adw_combo_row_set_model(app->rgb_effect, G_LIST_MODEL(gtk_string_list_new(effect_names_tr)));

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    /* left column: effect list */
    GtkWidget *left_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(left_card, "ajazz-card");
    gtk_widget_set_size_request(left_card, 220, -1);
    GtkWidget *left_title = gtk_label_new(_("Effect"));
    gtk_widget_add_css_class(left_title, "ajazz-card-title");
    gtk_widget_set_halign(left_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_card), left_title);

    GtkWidget *listbox = gtk_list_box_new();
    gtk_widget_add_css_class(listbox, "boxed-list");
    g_rgb_listbox = GTK_LIST_BOX(listbox);
    for (int i = 0; i < RGB_EFFECT_COUNT; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(effect_names_tr[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_set_margin_top(lbl, 8);
        gtk_widget_set_margin_bottom(lbl, 8);
        gtk_widget_set_margin_start(lbl, 10);
        gtk_widget_set_margin_end(lbl, 10);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(listbox), row);
    }
    g_signal_connect(listbox, "row-selected", G_CALLBACK(on_rgb_listbox_row_selected), app);
    gtk_box_append(GTK_BOX(left_card), listbox);
    gtk_box_append(GTK_BOX(columns), left_card);

    /* right column: detail panel for whichever effect is selected */
    GtkWidget *right_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(right_card, "ajazz-card");
    gtk_widget_set_hexpand(right_card, TRUE);
    GtkWidget *right_title = gtk_label_new(_("Settings"));
    gtk_widget_add_css_class(right_title, "ajazz-card-title");
    gtk_widget_set_halign(right_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right_card), right_title);

    GtkWidget *detail_list = gtk_list_box_new();
    gtk_widget_add_css_class(detail_list, "boxed-list");

    app->rgb_brightness = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_brightness), _("Brightness"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(app->rgb_brightness), _("0 to 4 (25% steps)"));
    adw_spin_row_set_value(app->rgb_brightness, 4);
    gtk_list_box_append(GTK_LIST_BOX(detail_list), GTK_WIDGET(app->rgb_brightness));

    app->rgb_speed = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 4, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_speed), _("Speed"));
    adw_spin_row_set_value(app->rgb_speed, 2);
    app->rgb_speed_row = ADW_ACTION_ROW(app->rgb_speed);
    gtk_list_box_append(GTK_LIST_BOX(detail_list), GTK_WIDGET(app->rgb_speed));

    /* Breathing's base colour -- only this effect needs one; rainbow and
     * colour cycle already sweep every hue, and constant/off reuse
     * whatever the DPI page's indicator colour already has. */
    app->rgb_soft_color[0] = 0xe8; app->rgb_soft_color[1] = 0x39; app->rgb_soft_color[2] = 0x4a; /* accent red */
    AdwActionRow *soft_color_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(soft_color_row), _("Colour"));
    GtkColorDialog *soft_dialog = gtk_color_dialog_new();
    GtkWidget *soft_color_btn = gtk_color_dialog_button_new(soft_dialog);
    GdkRGBA soft_rgba = {app->rgb_soft_color[0] / 255.0f, app->rgb_soft_color[1] / 255.0f,
        app->rgb_soft_color[2] / 255.0f, 1.0f};
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(soft_color_btn), &soft_rgba);
    gtk_widget_set_valign(soft_color_btn, GTK_ALIGN_CENTER);
    app->rgb_soft_color_btn = GTK_COLOR_DIALOG_BUTTON(soft_color_btn);
    g_signal_connect(soft_color_btn, "notify::rgba", G_CALLBACK(on_rgb_soft_color_changed), app);
    adw_action_row_add_suffix(soft_color_row, soft_color_btn);
    app->rgb_soft_color_row = soft_color_row;
    gtk_list_box_append(GTK_LIST_BOX(detail_list), GTK_WIDGET(soft_color_row));

    /* Custom effect: build a colour sequence by picking a colour above
     * and adding it here, then choose how it plays back. */
    AdwActionRow *custom_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(custom_row), _("Sequence"));
    adw_action_row_set_subtitle(custom_row, _("Pick a colour above, then tap + to add it. Tap a swatch to remove it."));
    GtkWidget *seq_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_valign(seq_box, GTK_ALIGN_CENTER);
    app->rgb_custom_swatches_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(seq_box), app->rgb_custom_swatches_box);
    GtkWidget *add_color_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(add_color_btn, "circular");
    g_signal_connect(add_color_btn, "clicked", G_CALLBACK(on_custom_add_color_clicked), app);
    gtk_box_append(GTK_BOX(seq_box), add_color_btn);
    adw_action_row_add_suffix(custom_row, seq_box);
    app->rgb_custom_row = custom_row;
    gtk_list_box_append(GTK_LIST_BOX(detail_list), GTK_WIDGET(custom_row));

    app->rgb_custom_transition_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->rgb_custom_transition_row), _("Playback"));
    const char *transition_names[] = {N_("Fade between colours"), N_("Hard cut"), NULL};
    const char *transition_names_tr[3] = {gettext(transition_names[0]), gettext(transition_names[1]), NULL};
    adw_combo_row_set_model(app->rgb_custom_transition_row, G_LIST_MODEL(gtk_string_list_new(transition_names_tr)));
    g_signal_connect(app->rgb_custom_transition_row, "notify::selected", G_CALLBACK(on_custom_transition_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(detail_list), GTK_WIDGET(app->rgb_custom_transition_row));

    gtk_box_append(GTK_BOX(right_card), detail_list);

    GtkWidget *apply_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(apply_row, 8);

    GtkWidget *btn_apply = gtk_button_new_with_label(_("Apply to mouse"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    gtk_widget_set_hexpand(btn_apply, TRUE);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_rgb_apply), app);
    gtk_box_append(GTK_BOX(apply_row), btn_apply);

    GtkWidget *btn_save_preset = gtk_button_new_with_label(_("Save as preset"));
    gtk_widget_add_css_class(btn_save_preset, "pill");
    g_signal_connect(btn_save_preset, "clicked", G_CALLBACK(on_save_lighting_preset_clicked), app);
    gtk_box_append(GTK_BOX(apply_row), btn_save_preset);

    gtk_box_append(GTK_BOX(right_card), apply_row);

    gtk_box_append(GTK_BOX(columns), right_card);
    adw_preferences_group_add(group, columns);

    /* Info banner: "off"/"rainbow"/"constant" are real firmware modes
     * stored on the mouse (confirmed against real hardware -- see
     * AJAZZ_QUERY_LED_MODE in protocol/hid.h); "software colour cycle" is
     * not, see the file comment at the top. This distinguishes the two
     * groups without a separate tabbed UI, since there are only 4 total
     * effects -- a full hardware/software tab switcher would be more
     * chrome than content for a list this short. */
    GtkWidget *hw_note = gtk_label_new(
        _("\"Off\", rainbow cycle and constant colour are stored in the mouse and work without the software running; breathing and colour cycle need Ajazzy open."));
    gtk_widget_add_css_class(hw_note, "dim-label");
    gtk_widget_add_css_class(hw_note, "caption");
    gtk_label_set_wrap(GTK_LABEL(hw_note), TRUE);
    gtk_widget_set_halign(hw_note, GTK_ALIGN_START);
    adw_preferences_group_add(group, hw_note);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), group);

    /* Lighting presets: a handful of hardcoded one-click shortcuts (fixed
     * colour + effect) plus any custom ones saved from this page's "Save
     * as preset" button -- see on_save_lighting_preset_clicked() and
     * populate_custom_presets() below. Custom presets are plain JSON
     * files under ~/.config/ajazzy/lighting_presets/ (see jsonutil.h/
     * gui/page_profiles.c for the same mechanism used for full profiles);
     * this is purely an app-side convenience, not something the mouse's
     * protocol has any storage for. */
    AdwPreferencesGroup *presets_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(presets_group, _("Lighting presets"));
    GtkWidget *presets_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(presets_card, "ajazz-card");
    GtkWidget *presets_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_lighting_presets_row = presets_row;
    static const struct { const char *name; guint effect; uint8_t r, g, b; } presets[] = {
        {N_("Red"),     RGB_CONSTANT, 255, 40, 40},
        {N_("Green"),   RGB_CONSTANT, 40, 220, 90},
        {N_("Blue"),    RGB_CONSTANT, 60, 110, 255},
        {N_("Rainbow"), RGB_RAINBOW,  0, 0, 0},
    };
    for (guint i = 0; i < G_N_ELEMENTS(presets); i++) {
        GtkWidget *tile = gtk_button_new();
        gtk_widget_add_css_class(tile, "flat");
        GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *swatch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_size_request(swatch, 48, 14);
        /* Reuses the same ajazz-dpi-N swatch colours as the DPI stage
         * buttons/palette (see main.c's CSS) rather than a one-off
         * per-widget provider -- red/green/blue map onto stages 1/2/3. */
        if (presets[i].effect == RGB_CONSTANT) {
            static const char *swatch_class[] = {"ajazz-dpi-1", "ajazz-dpi-2", "ajazz-dpi-3"};
            gtk_widget_add_css_class(swatch, swatch_class[i % 3]);
        } else {
            gtk_widget_add_css_class(swatch, "ajazz-dpi-6");
        }
        GtkWidget *name_lbl = gtk_label_new(gettext(presets[i].name));
        GtkWidget *type_lbl = gtk_label_new(presets[i].effect == RGB_RAINBOW ? _("Hardware effect") : _("Constant colour"));
        gtk_widget_add_css_class(type_lbl, "dim-label");
        gtk_widget_add_css_class(type_lbl, "caption");
        gtk_box_append(GTK_BOX(tbox), swatch);
        gtk_box_append(GTK_BOX(tbox), name_lbl);
        gtk_box_append(GTK_BOX(tbox), type_lbl);
        gtk_button_set_child(GTK_BUTTON(tile), tbox);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GUINT_TO_POINTER(i);
        g_signal_connect_data(tile, "clicked", G_CALLBACK(on_lighting_preset_clicked), data, free_closure_data, 0);
        gtk_box_append(GTK_BOX(presets_row), tile);
    }
    populate_custom_presets(app);
    gtk_box_append(GTK_BOX(presets_card), presets_row);
    adw_preferences_group_add(presets_group, presets_card);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), presets_group);

    /* now that the listbox exists, wire the combo row's own selection
     * changes (device fetch, software-cycle apply) back into it, on top
     * of the visibility logic it already drove */
    g_signal_connect(app->rgb_effect, "notify::selected", G_CALLBACK(on_rgb_effect_changed), app);
    adw_combo_row_set_selected(app->rgb_effect, RGB_RAINBOW);
    sync_rgb_listbox_from_combo(app);
    update_rgb_row_visibility(app);

    return page;
}
