/*
 * Lighting.
 *
 * Six effects run inside the mouse's firmware and keep going with Ajazzy
 * closed (off, steady, breathing, flowing, neon, colour wave -- the modes
 * the official driver sends; see AJAZZ_LIGHT_* in protocol/hid.h). Two more
 * are software effects (colour cycle, custom fade): Ajazzy keeps re-sending
 * a colour while it is open. They are clearly labelled, they stop when the
 * app exits, and they never touch the DPI indicator colours you have set --
 * those are restored when the effect ends.
 *
 * Every control applies immediately.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"
#include "../devices/device.h"
#include "../devices/registry.h"

typedef struct {
    const char *name;
    const char *note;
    gboolean software;
} FxInfo;

static const FxInfo FX[LFX_COUNT] = {
    [LFX_OFF]        = {N_("Off"),          N_("Lights off"), FALSE},
    [LFX_STEADY]     = {N_("Steady"),       N_("One solid colour"), FALSE},
    [LFX_BREATHING]  = {N_("Breathing"),    N_("Fades in and out, up to 7 colours"), FALSE},
    [LFX_FLOW]       = {N_("Flowing"),      N_("Built-in flowing light"), FALSE},
    [LFX_NEON]       = {N_("Neon"),         N_("Built-in neon effect"), FALSE},
    [LFX_WAVE]       = {N_("Colour wave"),  N_("Built-in rainbow wave"), FALSE},
    [LFX_SOFT_CYCLE] = {N_("Colour cycle"), N_("Smooth hue rotation"), TRUE},
    [LFX_SOFT_FADE]  = {N_("Custom fade"),  N_("Cross-fades your colours"), TRUE},
};

const char *lighting_effect_name(int effect)
{
    if (effect < 0 || effect >= LFX_COUNT)
        return "?";
    return gettext(FX[effect].name);
}

typedef struct {
    GtkWidget *fx_btn[LFX_COUNT];
    GtkWidget *fx_prev[LFX_COUNT];
    GtkWidget *bri_seg, *speed_seg;
    GtkWidget *bri_row, *speed_row, *steady_row, *palette_row, *soft_note;
    GtkWidget *steady_btn;
    GtkWidget *pal_btn[LIGHT_PALETTE_MAX];
    AdwSpinRow *pal_count;
    GtkWidget *transition_row;
    GtkWidget *presets_box;
    UiDebounce *apply_deb;
    guint tick_id;
} LightUI;

static LightUI L;
static gboolean fx_available(App *app, int fx);
static void update_visibility(App *app);
static gboolean loading;
static guint soft_timer;
static double soft_phase;
static int soft_fail;

/* ---------------- colour helpers ---------------- */

static void hsv_to_rgb(double h, uint8_t out[3])
{
    h = fmod(h, 360.0);
    if (h < 0) h += 360.0;
    double x = 1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0);
    double r, g, b;
    if (h < 60) { r = 1; g = x; b = 0; }
    else if (h < 120) { r = x; g = 1; b = 0; }
    else if (h < 180) { r = 0; g = 1; b = x; }
    else if (h < 240) { r = 0; g = x; b = 1; }
    else if (h < 300) { r = x; g = 0; b = 1; }
    else { r = 1; g = 0; b = x; }
    out[0] = (uint8_t)(r * 255 + 0.5);
    out[1] = (uint8_t)(g * 255 + 0.5);
    out[2] = (uint8_t)(b * 255 + 0.5);
}

/* the user's n colours -> the 7 slots the firmware wants, spread evenly so
 * every colour gets about the same share of the cycle */
static void expand_palette(const AppLight *l, uint8_t out[AJAZZ_LIGHT_PALETTE][3])
{
    int n = l->palette_n < 1 ? 1 : (l->palette_n > LIGHT_PALETTE_MAX ? LIGHT_PALETTE_MAX : l->palette_n);
    for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++)
        memcpy(out[i], l->palette[i * n / AJAZZ_LIGHT_PALETTE], 3);
}

/* inverse of expand_palette() for what the mouse reports: collapse runs */
static void collapse_palette(const uint8_t in[AJAZZ_LIGHT_PALETTE][3], AppLight *l)
{
    int n = 0;
    for (int i = 0; i < AJAZZ_LIGHT_PALETTE; i++) {
        if (i == 0 || memcmp(in[i], in[i - 1], 3) != 0)
            memcpy(l->palette[n++], in[i], 3);
    }
    l->palette_n = n;
}

static void default_light(AppLight *l)
{
    memset(l, 0, sizeof(*l));
    l->effect = LFX_STEADY;
    l->brightness = 4;
    l->speed = 2;
    l->color[0] = 0xe8; l->color[1] = 0x39; l->color[2] = 0x4a;
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        memcpy(l->palette[i], AJAZZ_LIGHT_DEFAULT_PALETTE[i], 3);
    l->palette_n = LIGHT_PALETTE_MAX;
}

/* ---------------- sending ---------------- */

typedef struct {
    ajazz_report_t req[2];
    int n, rc;
    gboolean toast_fail;
} LJob;

static void ljob_work(App *app, gpointer d)
{
    LJob *j = d;
    j->rc = 0;
    for (int i = 0; i < j->n; i++)
        if (ajazz_send_and_wait_mt(app, &j->req[i], 1000) != 0) { j->rc = -1; break; }
}

static void ljob_done(App *app, gpointer d)
{
    LJob *j = d;
    if (j->rc != 0 && j->toast_fail)
        toast(app, _("Couldn't change the lighting."));
}

static void soft_stop(App *app, gboolean restore_dpi_colors)
{
    if (soft_timer) {
        g_source_remove(soft_timer);
        soft_timer = 0;
        if (restore_dpi_colors && app->connected) {
            LJob *j = g_new0(LJob, 1);
            ajazz_build_set_dpi_colors(&j->req[0], app->dpi_color_cache);
            j->n = 1;
            app_async(app, ljob_work, NULL, j, g_free);
        }
    }
}

void lighting_shutdown(App *app)
{
    soft_stop(app, FALSE);
}

static void soft_color_at(const AppLight *l, double phase, uint8_t out[3])
{
    if (l->effect == LFX_SOFT_CYCLE) {
        hsv_to_rgb(phase * 360.0, out);
        return;
    }
    int n = l->palette_n < 1 ? 1 : l->palette_n;
    if (n == 1) { memcpy(out, l->palette[0], 3); return; }
    double pos = fmod(phase, 1.0) * n;
    int i = (int)pos % n, j = (i + 1) % n;
    double f = pos - floor(pos);
    if (l->soft_transition == 1) f = 0;
    for (int c = 0; c < 3; c++)
        out[c] = (uint8_t)(l->palette[i][c] * (1 - f) + l->palette[j][c] * f + 0.5);
}

static gboolean soft_tick(gpointer user_data)
{
    App *app = user_data;
    if (!app->connected) {
        soft_timer = 0;
        return G_SOURCE_REMOVE;
    }
    double speed = app->light.speed; /* 0-4 */
    double revolutions_per_s = 0.04 + speed * 0.05;
    soft_phase = fmod(soft_phase + revolutions_per_s * 0.12, 1.0);

    uint8_t rgb[3];
    soft_color_at(&app->light, soft_phase, rgb);
    uint8_t cols[AJAZZ_DPI_STAGES][3];
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
        memcpy(cols[i], rgb, 3);
    ajazz_report_t req;
    ajazz_build_set_dpi_colors(&req, cols);

    /* never block the UI thread on the device lock */
    int rc = ajazz_send_trylock_mt(app, &req);
    if (rc == 0 || rc == 1) {
        soft_fail = 0;
    } else if (++soft_fail >= 5) {
        soft_timer = 0;
        soft_fail = 0;
        toast(app, _("Software lighting effect stopped (mouse not responding)."));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

gboolean lighting_apply(App *app, const AppLight *l)
{
    if (!app->connected)
        return FALSE;
    app->light = *l;
    if (!fx_available(app, app->light.effect))
        app->light.effect = LFX_BREATHING; /* e.g. an old profile that picked an effect this model doesn't run */
    l = &app->light;
    gboolean was_soft = soft_timer != 0;
    soft_stop(app, FALSE);

    LJob *j = g_new0(LJob, 1);
    j->toast_fail = TRUE;
    uint8_t b = (uint8_t)l->brightness, s = (uint8_t)l->speed;

    switch (l->effect) {
        case LFX_OFF:
            ajazz_build_set_rgb_off(&j->req[0]);
            break;
        case LFX_STEADY:
            ajazz_build_set_light_steady(&j->req[0], b, s, l->color);
            break;
        case LFX_BREATHING: {
            uint8_t pal[AJAZZ_LIGHT_PALETTE][3];
            expand_palette(l, pal);
            ajazz_build_set_light_breathing(&j->req[0], b, s, pal);
            break;
        }
        case LFX_FLOW:
            ajazz_build_set_light_simple(&j->req[0], AJAZZ_LIGHT_FLOW, b, s);
            break;
        case LFX_NEON:
            ajazz_build_set_light_simple(&j->req[0], AJAZZ_LIGHT_NEON, b, s);
            break;
        case LFX_WAVE:
            ajazz_build_set_light_simple(&j->req[0], AJAZZ_LIGHT_WAVE, b, s);
            break;
        default: {
            /* software effects ride on the steady mode */
            uint8_t first[3] = {255, 255, 255};
            soft_color_at(l, 0, first);
            ajazz_build_set_light_steady(&j->req[0], b, s, first);
            break;
        }
    }
    j->n = 1;
    /* a software effect leaves the DPI colours rewritten; put them back */
    if (was_soft && l->effect < LFX_SOFT_CYCLE) {
        ajazz_build_set_dpi_colors(&j->req[1], app->dpi_color_cache);
        j->n = 2;
    }
    app_async(app, ljob_work, ljob_done, j, g_free);

    if (l->effect >= LFX_SOFT_CYCLE) {
        soft_phase = 0;
        soft_fail = 0;
        soft_timer = g_timeout_add(120, soft_tick, app);
    }
    return TRUE;
}

/* ---------------- UI sync ---------------- */

/* Flowing, neon and colour wave are accepted by the firmware but the official
 * driver disables them for every model we have a config for (device/mouse_*.xml,
 * enable="0"), so they're only shown when the model's table says it runs them. */
static gboolean fx_available(App *app, int fx)
{
    int hw = -1;
    switch (fx) {
        case LFX_OFF: hw = AJAZZ_LIGHT_OFF; break;
        case LFX_STEADY: hw = AJAZZ_LIGHT_STEADY; break;
        case LFX_BREATHING: hw = AJAZZ_LIGHT_BREATHING; break;
        case LFX_FLOW: hw = AJAZZ_LIGHT_FLOW; break;
        case LFX_NEON: hw = AJAZZ_LIGHT_NEON; break;
        case LFX_WAVE: hw = AJAZZ_LIGHT_WAVE; break;
        default: return TRUE; /* software effects run anywhere */
    }
    uint32_t modes = app->connected ? ajazz_lookup_light_modes(app->dev.dev_id)
                                    : ((1u << AJAZZ_LIGHT_OFF) | (1u << AJAZZ_LIGHT_BREATHING) | (1u << AJAZZ_LIGHT_STEADY));
    return (modes >> hw) & 1u;
}

static void update_visibility(App *app)
{
    for (int i = 0; i < LFX_COUNT; i++)
        gtk_widget_set_visible(gtk_widget_get_parent(L.fx_btn[i]), fx_available(app, i));

    int fx = app->light.effect;
    gboolean hw_colour = fx == LFX_STEADY;
    gboolean palette = fx == LFX_BREATHING || fx == LFX_SOFT_FADE;
    gtk_widget_set_visible(L.bri_row, fx != LFX_OFF);
    gtk_widget_set_visible(L.speed_row, fx != LFX_OFF && fx != LFX_STEADY);
    gtk_widget_set_visible(L.steady_row, hw_colour);
    gtk_widget_set_visible(L.palette_row, palette);
    gtk_widget_set_visible(L.transition_row, fx == LFX_SOFT_FADE);
    gtk_widget_set_visible(L.soft_note, FX[fx].software);
    int n = app->light.palette_n;
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        gtk_widget_set_visible(L.pal_btn[i], i < n);
}

static void sync_ui(App *app)
{
    loading = TRUE;
    const AppLight *l = &app->light;
    for (int i = 0; i < LFX_COUNT; i++)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(L.fx_btn[i]), i == l->effect);
    ui_segmented_set(L.bri_seg, l->brightness < 0 ? 0 : l->brightness > 4 ? 4 : l->brightness);
    ui_segmented_set(L.speed_seg, l->speed < 0 ? 0 : l->speed > 4 ? 4 : l->speed);
    GdkRGBA rgba;
    ui_rgba_from_rgb(&rgba, l->color);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(L.steady_btn), &rgba);
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++) {
        ui_rgba_from_rgb(&rgba, l->palette[i]);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(L.pal_btn[i]), &rgba);
    }
    adw_spin_row_set_value(L.pal_count, l->palette_n);
    update_visibility(app);
    loading = FALSE;
}

static gboolean apply_deb_fire(gpointer user_data)
{
    App *app = user_data;
    lighting_apply(app, &app->light);
    return G_SOURCE_REMOVE;
}

static void commit_now(App *app)
{
    ui_debounce_cancel(L.apply_deb);
    update_visibility(app);
    lighting_apply(app, &app->light);
}

/* ---------------- handlers ---------------- */

static void on_fx_toggled(GtkToggleButton *b, gpointer user_data)
{
    gpointer *d = user_data;
    App *app = d[0];
    int fx = GPOINTER_TO_INT(d[1]);
    if (loading || !gtk_toggle_button_get_active(b))
        return;
    if (app->light.effect == fx)
        return;
    app->light.effect = fx;
    commit_now(app);
}

static void on_bri(int i, gpointer user_data)
{
    App *app = user_data;
    if (loading) return;
    app->light.brightness = i;
    commit_now(app);
}

static void on_speed(int i, gpointer user_data)
{
    App *app = user_data;
    if (loading) return;
    app->light.speed = i;
    commit_now(app);
}

static void on_steady_color(GObject *btn, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    ui_rgb_from_rgba(app->light.color, gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(btn)));
    ui_debounce_poke(L.apply_deb);
}

static void on_pal_color(GObject *btn, GParamSpec *p, gpointer user_data)
{
    (void)p;
    gpointer *d = user_data;
    App *app = d[0];
    int i = GPOINTER_TO_INT(d[1]);
    if (loading) return;
    ui_rgb_from_rgba(app->light.palette[i], gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(btn)));
    ui_debounce_poke(L.apply_deb);
}

static void on_pal_count(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->light.palette_n = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    update_visibility(app);
    ui_debounce_poke(L.apply_deb);
}

static void on_transition(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    app->light.soft_transition = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    ui_debounce_poke(L.apply_deb);
}

static void on_multicolour(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        memcpy(app->light.palette[i], AJAZZ_LIGHT_DEFAULT_PALETTE[i], 3);
    app->light.palette_n = LIGHT_PALETTE_MAX;
    sync_ui(app);
    commit_now(app);
}

/* ---------------- animated previews ---------------- */

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

static void draw_preview(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
    (void)area;
    gpointer *d = data;
    App *app = d[0];
    int fx = GPOINTER_TO_INT(d[1]);
    const AppLight *l = &app->light;
    double t = g_get_monotonic_time() / 1e6;

    rounded_rect(cr, 0, 0, w, h, 10);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.07);
    cairo_paint(cr);

    uint8_t rgb[3];
    switch (fx) {
        case LFX_OFF:
            cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
            cairo_arc(cr, w / 2.0, h / 2.0, 3, 0, 2 * G_PI);
            cairo_fill(cr);
            return;
        case LFX_STEADY: {
            cairo_set_source_rgb(cr, l->color[0] / 255.0, l->color[1] / 255.0, l->color[2] / 255.0);
            cairo_paint(cr);
            break;
        }
        case LFX_BREATHING: {
            int n = l->palette_n < 1 ? 1 : l->palette_n;
            double period = 2.4;
            int idx = (int)(t / period) % n;
            double a = 0.5 - 0.5 * cos(2 * G_PI * fmod(t, period) / period);
            memcpy(rgb, l->palette[idx], 3);
            cairo_set_source_rgba(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 0.12 + 0.88 * a);
            cairo_paint(cr);
            break;
        }
        case LFX_FLOW:
            for (int x = 0; x < w; x += 3) {
                hsv_to_rgb(x * 1.6 - t * 120, rgb);
                double a = 0.35 + 0.65 * (0.5 + 0.5 * sin(x * 0.07 - t * 5));
                cairo_set_source_rgba(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, a);
                cairo_rectangle(cr, x, 0, 3, h);
                cairo_fill(cr);
            }
            break;
        case LFX_NEON: {
            hsv_to_rgb(t * 80, rgb);
            double pulse = 0.55 + 0.45 * sin(t * 4);
            cairo_set_source_rgba(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 0.20 * pulse);
            cairo_paint(cr);
            cairo_set_source_rgba(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 0.95);
            cairo_set_line_width(cr, 3);
            rounded_rect(cr, 6, 6, w - 12, h - 12, 8);
            cairo_stroke(cr);
            break;
        }
        case LFX_WAVE:
            for (int x = 0; x < w; x += 3) {
                hsv_to_rgb(x * 2.2 - t * 90, rgb);
                double y = h / 2.0 + sin(x * 0.09 - t * 4) * h * 0.28;
                cairo_set_source_rgba(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 0.95);
                cairo_rectangle(cr, x, y - 4, 3, 8);
                cairo_fill(cr);
            }
            break;
        case LFX_SOFT_CYCLE:
            hsv_to_rgb(t * 60, rgb);
            cairo_set_source_rgb(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
            cairo_paint(cr);
            break;
        case LFX_SOFT_FADE: {
            AppLight tmp = *l;
            tmp.effect = LFX_SOFT_FADE;
            soft_color_at(&tmp, fmod(t / 6.0, 1.0), rgb);
            cairo_set_source_rgb(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
            cairo_paint(cr);
            break;
        }
    }
}

static gboolean preview_tick(GtkWidget *w, GdkFrameClock *clock, gpointer user_data)
{
    (void)clock; (void)user_data;
    for (int i = 0; i < LFX_COUNT; i++)
        gtk_widget_queue_draw(L.fx_prev[i]);
    (void)w;
    return G_SOURCE_CONTINUE;
}

/* ---------------- presets ---------------- */

typedef struct { const char *name; AppLight l; } BuiltinPreset;

static void builtin_presets(BuiltinPreset out[], int *n)
{
    int k = 0;
    AppLight base;
    default_light(&base);

    struct { const char *name; int fx; uint8_t rgb[3]; } steady[] = {
        {N_("Crimson"), LFX_STEADY, {0xe8, 0x39, 0x4a}},
        {N_("Ocean"), LFX_STEADY, {0x1e, 0x90, 0xff}},
        {N_("Mint"), LFX_STEADY, {0x2f, 0xd2, 0x7a}},
        {N_("White"), LFX_STEADY, {0xff, 0xff, 0xff}},
    };
    for (guint i = 0; i < G_N_ELEMENTS(steady); i++, k++) {
        out[k].name = steady[i].name;
        out[k].l = base;
        out[k].l.effect = steady[i].fx;
        memcpy(out[k].l.color, steady[i].rgb, 3);
    }
    out[k].name = N_("Rainbow breathing");
    out[k].l = base;
    out[k].l.effect = LFX_BREATHING;
    k++;
    out[k].name = N_("Violet breathing");
    out[k].l = base;
    out[k].l.effect = LFX_BREATHING;
    out[k].l.palette_n = 1;
    out[k].l.palette[0][0] = 0xa0; out[k].l.palette[0][1] = 0x20; out[k].l.palette[0][2] = 0xf0;
    k++;
    out[k].name = N_("Cyan breathing");
    out[k].l = base;
    out[k].l.effect = LFX_BREATHING;
    out[k].l.palette_n = 1;
    out[k].l.palette[0][0] = 0x00; out[k].l.palette[0][1] = 0xd1; out[k].l.palette[0][2] = 0xd1;
    k++;
    *n = k;
}

static void on_preset_clicked(GtkButton *b, gpointer user_data)
{
    App *app = user_data;
    const AppLight *l = g_object_get_data(G_OBJECT(b), "light");
    if (!l) return;
    app->light = *l;
    sync_ui(app);
    commit_now(app);
}

static char *presets_dir(void) { return ajazzy_config_subdir("lighting"); }

static void preset_save_file(const char *name, const AppLight *l)
{
    char *dir = presets_dir();
    if (!dir) return;
    char *safe = g_strdup(name);
    for (char *p = safe; *p; p++) if (*p == '/' || *p == '\\') *p = '_';
    char *fn = g_strdup_printf("%s.json", safe);
    char *path = g_build_filename(dir, fn, NULL);

    GString *o = g_string_new("{\n  ");
    json_put_string(o, "name", name); g_string_append(o, ",\n  ");
    json_put_int(o, "effect", l->effect); g_string_append(o, ",\n  ");
    json_put_int(o, "brightness", l->brightness); g_string_append(o, ",\n  ");
    json_put_int(o, "speed", l->speed); g_string_append(o, ",\n  ");
    json_put_int(o, "transition", l->soft_transition); g_string_append(o, ",\n  ");
    json_put_int(o, "palette_n", l->palette_n); g_string_append(o, ",\n  ");
    int col[3] = {l->color[0], l->color[1], l->color[2]};
    json_put_int_array(o, "color", col, 3); g_string_append(o, ",\n  ");
    int pal[LIGHT_PALETTE_MAX * 3];
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        for (int c = 0; c < 3; c++) pal[i * 3 + c] = l->palette[i][c];
    json_put_int_array(o, "palette", pal, LIGHT_PALETTE_MAX * 3);
    g_string_append(o, "\n}\n");
    json_write_file(path, o->str);
    g_string_free(o, TRUE);
    g_free(path); g_free(fn); g_free(safe); g_free(dir);
}

static gboolean preset_load_file(const char *file, char *name, size_t nlen, AppLight *l)
{
    char *dir = presets_dir();
    if (!dir) return FALSE;
    char *path = g_build_filename(dir, file, NULL);
    char *t = json_read_file(path);
    g_free(path); g_free(dir);
    if (!t) return FALSE;
    default_light(l);
    json_get_string(t, "name", name, nlen);
    l->effect = json_get_int(t, "effect", LFX_STEADY);
    if (l->effect < 0 || l->effect >= LFX_COUNT) l->effect = LFX_STEADY;
    l->brightness = json_get_int(t, "brightness", 4);
    l->speed = json_get_int(t, "speed", 2);
    l->soft_transition = json_get_int(t, "transition", 0);
    l->palette_n = json_get_int(t, "palette_n", LIGHT_PALETTE_MAX);
    if (l->palette_n < 1 || l->palette_n > LIGHT_PALETTE_MAX) l->palette_n = LIGHT_PALETTE_MAX;
    int col[3] = {255, 0, 0};
    json_get_int_array(t, "color", col, 3);
    for (int c = 0; c < 3; c++) l->color[c] = (uint8_t)col[c];
    int pal[LIGHT_PALETTE_MAX * 3] = {0};
    if (json_get_int_array(t, "palette", pal, LIGHT_PALETTE_MAX * 3) > 0)
        for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
            for (int c = 0; c < 3; c++) l->palette[i][c] = (uint8_t)pal[i * 3 + c];
    g_free(t);
    return TRUE;
}

static void populate_presets(App *app);

static void on_preset_delete(GtkButton *b, gpointer user_data)
{
    App *app = user_data;
    const char *file = g_object_get_data(G_OBJECT(b), "file");
    char *dir = presets_dir();
    if (dir && file) {
        char *p = g_build_filename(dir, file, NULL);
        g_remove(p);
        g_free(p);
    }
    g_free(dir);
    populate_presets(app);
}

static GtkWidget *preset_chip(App *app, const char *label, const AppLight *l, const char *file)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "linked");
    GtkWidget *b = gtk_button_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *sw = gtk_drawing_area_new();
    gtk_widget_set_size_request(sw, 14, 14);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(sw, GTK_ALIGN_CENTER);
    AppLight *copy = g_new(AppLight, 1);
    *copy = *l;
    g_object_set_data_full(G_OBJECT(b), "light", copy, g_free);
    gtk_box_append(GTK_BOX(row), sw);
    gtk_box_append(GTK_BOX(row), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(b), row);
    gtk_widget_add_css_class(b, "aj-btn");
    gtk_widget_add_css_class(b, "aj-btn-secondary");
    g_signal_connect(b, "clicked", G_CALLBACK(on_preset_clicked), app);
    gtk_box_append(GTK_BOX(box), b);

    /* swatch: colour the preset leads with */
    const uint8_t *c = (l->effect == LFX_STEADY || l->effect == LFX_OFF) ? l->color : l->palette[0];
    char css[96];
    char name[32];
    snprintf(name, sizeof(name), "pre%p", (void *)sw);
    gtk_widget_set_name(sw, name);
    snprintf(css, sizeof(css), "#%s { background: #%02x%02x%02x; border-radius: 99px; }", name, c[0], c[1], c[2]);
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);

    if (file) {
        GtkWidget *del = gtk_button_new_from_icon_name("window-close-symbolic");
        gtk_widget_add_css_class(del, "aj-btn");
        gtk_widget_add_css_class(del, "aj-btn-secondary");
        gtk_widget_set_tooltip_text(del, _("Delete this preset"));
        g_object_set_data_full(G_OBJECT(del), "file", g_strdup(file), g_free);
        g_signal_connect(del, "clicked", G_CALLBACK(on_preset_delete), app);
        gtk_box_append(GTK_BOX(box), del);
    }
    return box;
}

static void populate_presets(App *app)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(L.presets_box)))
        gtk_flow_box_remove(GTK_FLOW_BOX(L.presets_box), c);

    BuiltinPreset bp[16];
    int n = 0;
    builtin_presets(bp, &n);
    for (int i = 0; i < n; i++)
        gtk_flow_box_append(GTK_FLOW_BOX(L.presets_box), preset_chip(app, gettext(bp[i].name), &bp[i].l, NULL));

    char *dir = presets_dir();
    GDir *d = dir ? g_dir_open(dir, 0, NULL) : NULL;
    if (d) {
        const char *e;
        while ((e = g_dir_read_name(d))) {
            if (!g_str_has_suffix(e, ".json")) continue;
            char name[64] = "";
            AppLight l;
            if (!preset_load_file(e, name, sizeof(name), &l)) continue;
            gtk_flow_box_append(GTK_FLOW_BOX(L.presets_box), preset_chip(app, name[0] ? name : e, &l, e));
        }
        g_dir_close(d);
    }
    g_free(dir);
}

static GtkWidget *g_save_entry;
static void on_save_preset_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    App *app = user_data;
    if (strcmp(resp, "save") != 0) return;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(g_save_entry));
    if (!name || !*name) { toast(app, _("Give the preset a name.")); return; }
    preset_save_file(name, &app->light);
    populate_presets(app);
    toast(app, _("Preset saved."));
}

static void on_save_preset(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Save lighting preset"),
        _("Saves the current effect, colours, brightness and speed.")));
    g_save_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_save_entry), _("Preset name"));
    gtk_entry_set_activates_default(GTK_ENTRY(g_save_entry), TRUE);
    adw_alert_dialog_set_extra_child(dlg, g_save_entry);
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "save", _("Save"));
    adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "save");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_save_preset_response), app);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
}

/* ---------------- refresh ---------------- */

typedef struct {
    gboolean ok, parsed;
    ajazz_report_t reply;
} LFetch;

static void lfetch_work(App *app, gpointer d)
{
    LFetch *f = d;
    f->ok = ajazz_query_mt(app, AJAZZ_QUERY_LED_MODE, &f->reply, 1000) == 0;
}

static void lfetch_done(App *app, gpointer d)
{
    LFetch *f = d;
    if (!app->connected || !f->ok || soft_timer)
        return; /* a running software effect owns the display */
    ajazz_light_state_t st;
    if (ajazz_parse_light(&f->reply, &st) != 0)
        return;
    AppLight *l = &app->light;
    if (l->palette_n < 1)
        default_light(l);
    switch (st.mode) {
        case AJAZZ_LIGHT_OFF:       l->effect = LFX_OFF; break;
        case AJAZZ_LIGHT_STEADY:    l->effect = LFX_STEADY; memcpy(l->color, st.rgb, 3); break;
        case AJAZZ_LIGHT_BREATHING: l->effect = LFX_BREATHING; collapse_palette(st.palette, l); break;
        case AJAZZ_LIGHT_FLOW:      l->effect = LFX_FLOW; break;
        case AJAZZ_LIGHT_NEON:      l->effect = LFX_NEON; break;
        case AJAZZ_LIGHT_WAVE:      l->effect = LFX_WAVE; break;
    }
    if (st.mode != AJAZZ_LIGHT_OFF) {
        l->brightness = st.brightness;
        l->speed = st.speed;
    }
    sync_ui(app);
    refresh_home(app);
}

void refresh_lighting(App *app)
{
    update_visibility(app);
    if (!app->connected)
        return;
    app_async(app, lfetch_work, lfetch_done, g_new0(LFetch, 1), g_free);
}

/* ---------------- page ---------------- */

static GtkWidget *seg_row(const char *title, const char *sub, GtkWidget *seg)
{
    return GTK_WIDGET(ui_row_new(title, sub, seg));
}

GtkWidget *build_lighting_page(App *app)
{
    default_light(&app->light);

    GtkWidget *save = ui_button_new(_("Save preset"), "document-save-symbolic", "secondary");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_preset), app);

    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Lighting"),
        _("Pick an effect \xE2\x80\x94 it applies to the mouse immediately."), save, &body);

    L.apply_deb = ui_debounce_new(300, apply_deb_fire, app);

    GtkWidget *cb;
    GtkWidget *card = ui_card_new(_("Effect"), NULL, &cb);
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 10);
    GtkToggleButton *first = NULL;
    for (int i = 0; i < LFX_COUNT; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        gtk_widget_add_css_class(b, "aj-effect");
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), first);
        else first = GTK_TOGGLE_BUTTON(b);
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *pv = gtk_drawing_area_new();
        gtk_widget_set_size_request(pv, 150, 58);
        gtk_widget_set_margin_top(pv, 8); gtk_widget_set_margin_start(pv, 8); gtk_widget_set_margin_end(pv, 8);
        gpointer *dd = g_new(gpointer, 2);
        dd[0] = app; dd[1] = GINT_TO_POINTER(i);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(pv), draw_preview, dd, g_free);
        GtkWidget *name = gtk_label_new(gettext(FX[i].name));
        gtk_widget_add_css_class(name, "aj-effect-name");
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_widget_set_margin_start(name, 12); gtk_widget_set_margin_top(name, 8);
        GtkWidget *note = gtk_label_new(FX[i].software ? _("Software \xC2\xB7 needs Ajazzy open") : gettext(FX[i].note));
        gtk_widget_add_css_class(note, "aj-effect-note");
        gtk_widget_set_halign(note, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(note), 0);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_set_margin_start(note, 12); gtk_widget_set_margin_end(note, 8); gtk_widget_set_margin_bottom(note, 10);
        gtk_box_append(GTK_BOX(v), pv);
        gtk_box_append(GTK_BOX(v), name);
        gtk_box_append(GTK_BOX(v), note);
        gtk_button_set_child(GTK_BUTTON(b), v);
        L.fx_btn[i] = b;
        L.fx_prev[i] = pv;
        gpointer *cd = g_new(gpointer, 2);
        cd[0] = app; cd[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "toggled", G_CALLBACK(on_fx_toggled), cd, free_closure_data, 0);
        gtk_flow_box_append(GTK_FLOW_BOX(flow), b);
    }
    gtk_box_append(GTK_BOX(cb), ui_card_pad(flow));
    gtk_box_append(GTK_BOX(body), card);
    L.tick_id = gtk_widget_add_tick_callback(flow, preview_tick, NULL, NULL);

    /* ---- settings for the chosen effect ---- */
    card = ui_card_new(_("Settings"), NULL, &cb);
    GtkWidget *list = ui_list_new();

    const char *bri[] = {"0", "25%", "50%", "75%", "100%"};
    L.bri_seg = ui_segmented_new(bri, 5, 4, on_bri, app);
    L.bri_row = seg_row(_("Brightness"), NULL, L.bri_seg);
    gtk_list_box_append(GTK_LIST_BOX(list), L.bri_row);

    const char *spd[] = {"1", "2", "3", "4", "5"};
    L.speed_seg = ui_segmented_new(spd, 5, 2, on_speed, app);
    L.speed_row = seg_row(_("Speed"), _("1 is slowest, 5 is fastest."), L.speed_seg);
    gtk_list_box_append(GTK_LIST_BOX(list), L.speed_row);

    L.steady_btn = gtk_color_dialog_button_new(gtk_color_dialog_new());
    g_signal_connect(L.steady_btn, "notify::rgba", G_CALLBACK(on_steady_color), app);
    L.steady_row = GTK_WIDGET(ui_row_new(_("Colour"), NULL, L.steady_btn));
    gtk_list_box_append(GTK_LIST_BOX(list), L.steady_row);

    /* palette editor (breathing + custom fade) */
    GtkWidget *pal = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++) {
        L.pal_btn[i] = gtk_color_dialog_button_new(gtk_color_dialog_new());
        gpointer *pd = g_new(gpointer, 2);
        pd[0] = app; pd[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(L.pal_btn[i], "notify::rgba", G_CALLBACK(on_pal_color), pd, free_closure_data, 0);
        gtk_box_append(GTK_BOX(pal), L.pal_btn[i]);
    }
    L.palette_row = GTK_WIDGET(ui_row_new(_("Colours"), _("Used one after another."), pal));
    gtk_list_box_append(GTK_LIST_BOX(list), L.palette_row);

    L.pal_count = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, LIGHT_PALETTE_MAX, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(L.pal_count), _("Number of colours"));
    g_signal_connect(L.pal_count, "notify::value", G_CALLBACK(on_pal_count), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(L.pal_count));

    AdwComboRow *tr = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tr), _("Transition"));
    const char *trn[] = {_("Smooth fade"), _("Hard cut"), NULL};
    adw_combo_row_set_model(tr, G_LIST_MODEL(gtk_string_list_new(trn)));
    g_signal_connect(tr, "notify::selected", G_CALLBACK(on_transition), app);
    L.transition_row = GTK_WIDGET(tr);
    gtk_list_box_append(GTK_LIST_BOX(list), L.transition_row);

    GtkWidget *note = gtk_label_new(_("Software effects run only while Ajazzy is open, and they keep the mouse's radio busy, "
        "which costs battery. Pick a built-in effect to save power. Your DPI indicator colours are restored when you switch away."));
    gtk_widget_add_css_class(note, "aj-callout");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_widget_set_margin_start(note, 6); gtk_widget_set_margin_end(note, 6); gtk_widget_set_margin_bottom(note, 8);
    L.soft_note = note;

    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(cb), note);

    GtkWidget *mc = ui_button_new(_("Use the multicolour set"), "emblem-default-symbolic", "ghost");
    gtk_widget_set_halign(mc, GTK_ALIGN_START);
    gtk_widget_set_margin_start(mc, 6); gtk_widget_set_margin_bottom(mc, 8);
    g_signal_connect(mc, "clicked", G_CALLBACK(on_multicolour), app);
    gtk_box_append(GTK_BOX(cb), mc);
    gtk_box_append(GTK_BOX(body), card);

    /* ---- presets ---- */
    card = ui_card_new(_("Presets"), _("One click to restore a look."), &cb);
    L.presets_box = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(L.presets_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(L.presets_box), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(L.presets_box), 8);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(L.presets_box), 6);
    gtk_box_append(GTK_BOX(cb), ui_card_pad(L.presets_box));
    gtk_box_append(GTK_BOX(body), card);
    populate_presets(app);

    sync_ui(app);
    return page;
}
