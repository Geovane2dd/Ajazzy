/*
 * Home page: connection status, device identity, battery, and the
 * "Compatibility" panel that shows which capabilities are confirmed
 * against real hardware for this exact model.
 */

#include <glib/gi18n.h>
#include <string.h>

#include "app.h"
#include "../devices/device.h"

static void set_confirm_badge(GtkWidget *badge, gboolean confirmed);
static void on_home_goto_clicked(GtkButton *btn, gpointer user_data);
static void on_factory_reset_clicked(GtkButton *btn, gpointer user_data);

/* Tier boundaries must match ajazz_battery_label() in protocol/hid.c --
 * kept as a separate small function here (rather than parsing the label
 * string back apart) since the GUI needs a 0-4 count to light up bars,
 * not just a sentence. raw is a plain 0-100 percentage split into even
 * quarters, confirmed by decompiling the official driver -- see the
 * comment in protocol/hid.h. */
static int battery_tier(uint8_t raw)
{
    if (raw >= 75) return 4;
    if (raw >= 50) return 3;
    if (raw >= 25) return 2;
    return 1;
}

/* Standard icon-theme names -- freedesktop/Adwaita's "battery-level-N"
 * set, N in steps of 10 (0/10/20/.../100), each with a plain, a
 * "-charging-" and a "-plugged-in-" variant. Picking these over a
 * custom-drawn icon means the tray icon matches the desktop's own
 * battery indicator and follows the user's icon theme automatically --
 * this is the same icon set GNOME Shell's own battery percentage
 * indicator uses. Rounds `percent` down to the nearest step; pass
 * percent < 0 for "no reading" (falls back to the generic
 * battery-missing-symbolic, which isn't part of the level set). */
static const char *battery_percent_icon(int percent, gboolean charging)
{
    static char name[40];

    if (percent < 0)
        return "battery-missing-symbolic";
    if (percent > 100) percent = 100;
    int level = (percent / 10) * 10;

    /* Adwaita's 100% asset is named "-charged-", not "-charging-", for
     * the plugged-in-and-full case -- everything else just gets
     * "-charging-". */
    const char *suffix = !charging ? ""
                        : (level == 100) ? "-charged"
                        : "-charging";
    snprintf(name, sizeof(name), "battery-level-%d%s-symbolic", level, suffix);
    return name;
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

/* Heap-allocated result of the battery-query fetch thread below, handed
 * to home_fetch_apply() on the main thread via g_idle_add(). */
typedef struct {
    App *app;
    gboolean ok;
    uint8_t raw;
} HomeFetchResult;

static gboolean home_fetch_apply(gpointer user_data)
{
    HomeFetchResult *res = user_data;
    App *app = res->app;
    app->home_fetch_pending = FALSE;

    /* The page may have been re-entered (or the mouse unplugged) while
     * this was in flight -- if we're no longer connected, the
     * disconnected-state branch in refresh_home() already applied, so
     * don't clobber it with a stale reading. */
    if (!app->connected) {
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    char device_line[256];
    snprintf(device_line, sizeof(device_line), "%s", app->dev.name);

    if (res->ok) {
        uint8_t raw = res->raw;
        int tier = battery_tier(raw);
        /* Translatable in the GUI's own strings, unlike
         * ajazz_battery_label() in protocol/hid.c -- that one exists
         * for the CLI, which has no i18n layer of its own, and stays
         * English-only by design; the GUI builds its own label instead
         * of reusing it so this text actually goes through po/pt_BR.po.
         * Just a short qualifier, not "(N/4 bars)" -- the real 0-100%
         * number is already shown right next to it, so restating it as
         * a quarter-count would just be noise. */
        static const char *tier_labels[] = {
            N_("Low"), N_("Medium"), N_("Good"), N_("Full"),
        };
        const char *label = gettext(tier_labels[tier - 1]);

        char pct_line[16];
        snprintf(pct_line, sizeof(pct_line), "%d%%", raw);
        gtk_label_set_text(app->lbl_battery, pct_line);
        char tooltip[32];
        snprintf(tooltip, sizeof(tooltip), "%d%% -- %s", raw, label);
        gtk_widget_set_tooltip_text(GTK_WIDGET(app->lbl_battery), tooltip);
        set_battery_bars(app, tier);
        app->battery_percent = raw;
        if (app->home_batt_ring) gtk_widget_queue_draw(app->home_batt_ring);
        if (app->home_batt_pct_lbl) gtk_label_set_text(app->home_batt_pct_lbl, pct_line);

        /* dev.charging is independent of which interface we actually
         * read the battery from (ajazz_open() prefers the 2.4G one when
         * both exist) -- see the comment on ajazz_dev_t::charging in
         * protocol/transport.h. Gated on AJAZZ_CAP_BATTERY so a
         * wired-only model (no battery at all) never shows this. */
        gboolean charging = (app->dev.confirmed & AJAZZ_CAP_BATTERY) && app->dev.charging;
        gtk_widget_set_visible(app->lbl_charging, charging);
        settings_check_low_battery(app, raw, charging);

        char batt_line[128];
        snprintf(batt_line, sizeof(batt_line), charging ? _("Battery: %d%% -- %s (charging)") : _("Battery: %d%% -- %s"), raw, label);
        ajazz_tray_set_status(app->tray, device_line, batt_line);
        ajazz_tray_set_icon(app->tray, battery_percent_icon(raw, charging));
    } else {
        gtk_label_set_text(app->lbl_battery, "?");
        gtk_widget_set_visible(app->lbl_charging, FALSE);
        set_battery_bars(app, -1);
        app->battery_percent = -1;
        if (app->home_batt_ring) gtk_widget_queue_draw(app->home_batt_ring);
        if (app->home_batt_pct_lbl) gtk_label_set_text(app->home_batt_pct_lbl, "?");
        ajazz_tray_set_status(app->tray, device_line, _("Battery: unknown"));
        ajazz_tray_set_icon(app->tray, battery_percent_icon(-1, FALSE));
    }

    g_free(res);
    return G_SOURCE_REMOVE;
}

/* Runs off the main thread -- MUST NOT touch any GTK widget, only
 * app->dev via the *_mt() wrapper (see App::hid_lock) and the heap
 * result handed to home_fetch_apply(). */
static gpointer home_fetch_thread(gpointer user_data)
{
    App *app = user_data;
    HomeFetchResult *res = g_new0(HomeFetchResult, 1);
    res->app = app;

    ajazz_report_t reply;
    res->ok = (ajazz_query_mt(app, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000) == 0);
    if (res->ok)
        res->raw = reply.payload[AJAZZ_BATTERY_PAYLOAD_OFFSET];

    g_idle_add(home_fetch_apply, res);
    return NULL;
}

/* Names must match RGB_OFF/RGB_RAINBOW/RGB_CONSTANT/RGB_SOFT_COLORCYCLE's
 * order in page_lighting.c's effect combo -- duplicated here (rather than
 * exported) since it's just for this one label's text, not shared state. */
static const char *home_light_effect_names[] = {
    N_("Off"), N_("Rainbow"), N_("Constant colour"), N_("Colour cycle"),
};

/* Refreshes the Home dashboard's top-row stat cards (DPI/Rate/Lighting)
 * from the shared state the Mouse/Lighting pages already keep -- no
 * separate fetch, just reflecting whatever's already cached. Safe to call
 * whether or not the mouse is connected (falls back to "--"). */
static void refresh_home_cards(App *app)
{
    if (app->home_dpi_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", app->dpi_cache[app->dpi_selected]);
        gtk_label_set_text(app->home_dpi_val, app->connected ? buf : "--");
    }
    if (app->home_rate_val) {
        char buf[16];
        if (app->connected && app->rate_hz > 0)
            snprintf(buf, sizeof(buf), "%d Hz", app->rate_hz);
        else
            snprintf(buf, sizeof(buf), "--");
        gtk_label_set_text(app->home_rate_val, buf);
    }
    if (app->home_light_val) {
        if (app->connected && app->rgb_effect) {
            guint sel = adw_combo_row_get_selected(app->rgb_effect);
            gtk_label_set_text(app->home_light_val,
                sel < G_N_ELEMENTS(home_light_effect_names) ? gettext(home_light_effect_names[sel]) : "--");
        } else {
            gtk_label_set_text(app->home_light_val, "--");
        }
    }
}

/* Called every time the Home page becomes visible (and every 5s from
 * the periodic tray tick) -- the parts that only need app->dev's
 * already-cached fields (populated by ensure_connected()/ajazz_open(),
 * not a fresh query) apply immediately here; the one part that needs a
 * live query (battery) runs on a background thread so a slow/unplugged
 * mouse can't freeze the window -- see home_fetch_thread()/
 * home_fetch_apply() and the comment on App::hid_lock. */
void refresh_home(App *app)
{
    if (!ensure_connected(app)) {
        gtk_label_set_text(app->lbl_name, _("Not connected"));
        gtk_label_set_text(app->lbl_vidpid, "--");
        gtk_label_set_text(app->lbl_devid, "--");
        gtk_label_set_text(app->lbl_battery, "--");
        gtk_widget_set_visible(app->lbl_charging, FALSE);
        gtk_label_set_text(app->lbl_confirmed, "");
        set_status_widgets(app->status_dot, app->status_lbl, FALSE);
        set_status_widgets(app->hero_status_dot, app->hero_status_lbl, FALSE);
        set_status_widgets(app->topbar_status_dot, app->topbar_status_lbl, FALSE);
        if (app->topbar_device_lbl)
            gtk_label_set_text(app->topbar_device_lbl, _("No device"));
        set_battery_bars(app, -1);
        app->battery_percent = -1;
        if (app->home_batt_ring) gtk_widget_queue_draw(app->home_batt_ring);
        if (app->home_batt_pct_lbl) gtk_label_set_text(app->home_batt_pct_lbl, "?");
        ajazz_tray_set_status(app->tray, _("No AJAZZ mouse detected"), "");
        ajazz_tray_set_icon(app->tray, battery_percent_icon(-1, FALSE));
        refresh_home_cards(app);
        return;
    }

    set_status_widgets(app->status_dot, app->status_lbl, TRUE);
    set_status_widgets(app->hero_status_dot, app->hero_status_lbl, TRUE);
    set_status_widgets(app->topbar_status_dot, app->topbar_status_lbl, TRUE);
    if (app->topbar_device_lbl)
        gtk_label_set_text(app->topbar_device_lbl, app->dev.name);
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

    refresh_home_cards(app);

    if (app->home_fetch_pending)
        return;
    app->home_fetch_pending = TRUE;
    g_thread_unref(g_thread_new("ajazzy-home-fetch", home_fetch_thread, app));
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

    app->lbl_charging = gtk_label_new(_("⚡ Charging"));
    gtk_widget_add_css_class(app->lbl_charging, "ajazz-stat-label");
    gtk_widget_add_css_class(app->lbl_charging, "ajazz-status-connected");
    gtk_widget_set_halign(app->lbl_charging, GTK_ALIGN_END);
    gtk_widget_set_visible(app->lbl_charging, FALSE);
    gtk_box_append(GTK_BOX(batt_box), app->lbl_charging);

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

/* Wire command reconstructed from decompiling the official driver's
 * "Restore Factory Settings" button (DeviceMainDlg::
 * SetMouseRestoreFactoryData) -- see the comment on
 * AJAZZ_FAM_FACTORY_RESET in protocol/hid.h. */
static void on_factory_reset_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    App *app = user_data;
    if (strcmp(response, "reset") != 0)
        return;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t req;
    ajazz_build_factory_reset(&req);
    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0) {
        toast(app, _("Factory reset sent."));
        refresh_home(app);
    } else {
        toast(app, _("Failed to send the factory reset."));
    }
}

static void on_factory_reset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Restore factory defaults?"),
        _("This resets DPI, DPI colours, report rate, lighting, sensor/power, "
          "button mapping and macros. There's no undo once sent.")));
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "reset", _("Restore"));
    adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_factory_reset_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

/* Small helper: an .ajazz-card box with an uppercase title label, used to
 * lay out the Home page as a grid of cards rather than a single vertical
 * list of AdwPreferencesGroups. */
static GtkWidget *make_card(const char *title, GtkWidget *content)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "ajazz-card");
    if (title) {
        GtkWidget *lbl = gtk_label_new(title);
        gtk_widget_add_css_class(lbl, "ajazz-card-title");
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(card), lbl);
    }
    gtk_box_append(GTK_BOX(card), content);
    return card;
}

/* Circular progress ring for the Battery stat card -- a plain arc, not a
 * fancy gauge, since this is a dashboard glance not a detail page. Drawn
 * in the card's own accent-adjacent grey when unknown (-1). */
static void draw_battery_ring(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    App *app = data;
    (void)area;
    double cx = width / 2.0, cy = height / 2.0;
    double r = (MIN(width, height) - 8) / 2.0;
    double start = -G_PI_2;

    cairo_set_line_width(cr, 6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_stroke(cr);

    int pct = app->battery_percent;
    if (pct >= 0) {
        double frac = pct / 100.0;
        cairo_arc(cr, cx, cy, r, start, start + frac * 2 * G_PI);
        cairo_set_source_rgb(cr, 0.910, 0.224, 0.290); /* accent #e8394a */
        cairo_stroke(cr);
    }
}

/* Battery card: same card shell as build_stat_card(), but the value slot
 * is a circular ring (see draw_battery_ring()) with the percentage
 * overlaid in its centre, instead of a plain number. */
static GtkWidget *build_battery_stat_card(App *app, const char *goto_label, const char *goto_page)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "ajazz-card");
    gtk_widget_set_vexpand(card, TRUE);

    GtkWidget *title_lbl = gtk_label_new(_("Battery"));
    gtk_widget_add_css_class(title_lbl, "ajazz-card-title");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), title_lbl);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(overlay, 6);
    GtkWidget *ring = gtk_drawing_area_new();
    gtk_widget_set_size_request(ring, 72, 72);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ring), draw_battery_ring, app, NULL);
    app->home_batt_ring = ring;
    gtk_overlay_set_child(GTK_OVERLAY(overlay), ring);
    GtkWidget *pct_lbl = gtk_label_new("--");
    gtk_widget_add_css_class(pct_lbl, "ajazz-home-stat-value-sm");
    gtk_widget_set_halign(pct_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(pct_lbl, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), pct_lbl);
    app->home_batt_pct_lbl = GTK_LABEL(pct_lbl);
    gtk_box_append(GTK_BOX(card), overlay);

    GtkWidget *sub_lbl = gtk_label_new(_("Battery"));
    gtk_widget_add_css_class(sub_lbl, "dim-label");
    gtk_widget_add_css_class(sub_lbl, "caption");
    gtk_widget_set_halign(sub_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(sub_lbl, 4);
    gtk_box_append(GTK_BOX(card), sub_lbl);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 8);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(card), sep);

    GtkWidget *goto_btn = gtk_button_new_with_label(goto_label);
    gtk_widget_add_css_class(goto_btn, "flat");
    g_object_set_data(G_OBJECT(goto_btn), "target-page", (gpointer)goto_page);
    g_signal_connect(goto_btn, "clicked", G_CALLBACK(on_home_goto_clicked), app);
    gtk_box_append(GTK_BOX(card), goto_btn);
    return card;
}

static void on_home_goto_clicked(GtkButton *btn, gpointer user_data)
{
    App *app = user_data;
    const char *page = g_object_get_data(G_OBJECT(btn), "target-page");
    if (page) switch_to_page(app, page);
}

/* Top-row stat card: title, a big value (red), a subtitle, an optional
 * row of quick chips, and a full-width button to the detail page --
 * matches every card in the reference mockup's top row (DPI/Rate/
 * Battery/Lighting) except Battery, which swaps the plain value label
 * for a drawing-area ring (see build_battery_stat_card()). */
static GtkWidget *build_stat_card(const char *title, GtkLabel **out_value,
    const char *subtitle, GtkWidget *chips_row, const char *goto_label, const char *goto_page, App *app)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "ajazz-card");
    gtk_widget_set_vexpand(card, TRUE);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_add_css_class(title_lbl, "ajazz-card-title");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), title_lbl);

    GtkWidget *value_lbl = gtk_label_new("--");
    gtk_widget_add_css_class(value_lbl, "ajazz-home-stat-value");
    gtk_widget_set_halign(value_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(value_lbl, TRUE);
    gtk_widget_set_margin_top(value_lbl, 6);
    gtk_widget_set_margin_bottom(value_lbl, 2);
    gtk_box_append(GTK_BOX(card), value_lbl);
    *out_value = GTK_LABEL(value_lbl);

    GtkWidget *sub_lbl = gtk_label_new(subtitle);
    gtk_widget_add_css_class(sub_lbl, "dim-label");
    gtk_widget_add_css_class(sub_lbl, "caption");
    gtk_widget_set_halign(sub_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), sub_lbl);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 8);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(card), sep);

    if (chips_row)
        gtk_box_append(GTK_BOX(card), chips_row);

    GtkWidget *goto_btn = gtk_button_new_with_label(goto_label);
    gtk_widget_add_css_class(goto_btn, "flat");
    gtk_widget_set_margin_top(goto_btn, chips_row ? 4 : 0);
    g_object_set_data(G_OBJECT(goto_btn), "target-page", (gpointer)goto_page);
    g_signal_connect(goto_btn, "clicked", G_CALLBACK(on_home_goto_clicked), app);
    gtk_box_append(GTK_BOX(card), goto_btn);
    return card;
}

static void on_home_dpi_chip_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = ((gpointer *)user_data)[0];
    int stage = GPOINTER_TO_INT(((gpointer *)user_data)[1]);
    mouse_page_select_stage(app, stage);
    refresh_home_cards(app);
}

static void on_home_refresh_action(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    refresh_home((App *)user_data);
}

static void on_home_device_info_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }
    char body[512];
    snprintf(body, sizeof(body), "%s\n%04X:%04X\n%s",
        app->dev.name, app->dev.vid, app->dev.pid, app->dev.dev_id[0] ? app->dev.dev_id : "?");
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Device information"), body));
    adw_alert_dialog_add_response(dialog, "ok", _("OK"));
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

/* 2-column tile grid of quick actions, restricted to real existing
 * actions (refresh / factory reset / device info dialog) -- the
 * reference mockup also showed "Default profile"/"Export profile"/
 * "Import profile" tiles, which this app has no backing feature for
 * (there's no multi-profile storage in the mouse or the app), so they're
 * intentionally left out rather than wired to nothing. */
static GtkWidget *build_quick_actions_card(App *app)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "ajazz-card");
    gtk_widget_set_size_request(card, 220, -1);
    GtkWidget *title = gtk_label_new(_("Quick actions"));
    gtk_widget_add_css_class(title, "ajazz-card-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), title);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);

    struct { const char *icon, *title, *sub; GCallback cb; } tiles[] = {
        {"view-refresh-symbolic", N_("Refresh"), N_("Re-read from mouse"), G_CALLBACK(on_home_refresh_action)},
        {"edit-undo-symbolic", N_("Factory reset"), N_("Restore all defaults"), G_CALLBACK(on_factory_reset_clicked)},
        {"dialog-information-symbolic", N_("Device info"), N_("VID/PID, firmware id"), G_CALLBACK(on_home_device_info_clicked)},
    };
    for (guint i = 0; i < G_N_ELEMENTS(tiles); i++) {
        GtkWidget *tile = gtk_button_new();
        gtk_widget_add_css_class(tile, "flat");
        GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *icon = gtk_image_new_from_icon_name(tiles[i].icon);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
        GtkWidget *t_lbl = gtk_label_new(gettext(tiles[i].title));
        gtk_widget_add_css_class(t_lbl, "heading");
        GtkWidget *s_lbl = gtk_label_new(gettext(tiles[i].sub));
        gtk_widget_add_css_class(s_lbl, "dim-label");
        gtk_widget_add_css_class(s_lbl, "caption");
        gtk_label_set_wrap(GTK_LABEL(s_lbl), TRUE);
        gtk_box_append(GTK_BOX(tbox), icon);
        gtk_box_append(GTK_BOX(tbox), t_lbl);
        gtk_box_append(GTK_BOX(tbox), s_lbl);
        gtk_button_set_child(GTK_BUTTON(tile), tbox);
        g_signal_connect(tile, "clicked", tiles[i].cb, app);
        gtk_grid_attach(GTK_GRID(grid), tile, i % 2, i / 2, 1, 1);
    }
    gtk_box_append(GTK_BOX(card), grid);
    return card;
}

/* Compact "Button Mapping" readout: one line per physical button showing
 * its current function, kept in sync by page_buttons.c's
 * buttons_fetch_apply() via app->home_key_lbl[] -- the full interactive
 * mouse diagram lives on the Buttons page itself (see build_mouse_diagram()
 * there); duplicating that Cairo drawing here for a dashboard glance
 * would be a lot of code for a summary card, so this reuses the same
 * default-name labels instead. */
/* Same factory-default names as page_buttons.c's default_button_names --
 * duplicated (not shared) since that array is file-static there and this
 * is purely a placeholder shown until buttons_fetch_apply() overwrites it
 * with what's actually on the mouse. */
static const char *home_default_button_names[AJAZZ_KEY_SLOTS] = {
    N_("Left click"), N_("Right click"), N_("Middle click"), N_("Back"), N_("Forward"),
};

static GtkWidget *build_button_mapping_card(App *app)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "ajazz-card");
    gtk_widget_set_hexpand(card, TRUE);
    GtkWidget *title = gtk_label_new(_("Button mapping"));
    gtk_widget_add_css_class(title, "ajazz-card-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), title);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(rowbox, 8);
        gtk_widget_set_margin_bottom(rowbox, 8);
        gtk_widget_set_margin_start(rowbox, 10);
        gtk_widget_set_margin_end(rowbox, 10);
        char name[32];
        snprintf(name, sizeof(name), _("Button %d"), i + 1);
        GtkWidget *name_lbl = gtk_label_new(name);
        gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
        gtk_widget_set_hexpand(name_lbl, TRUE);
        GtkWidget *fn_lbl = gtk_label_new(gettext(home_default_button_names[i]));
        gtk_widget_add_css_class(fn_lbl, "dim-label");
        app->home_key_lbl[i] = GTK_LABEL(fn_lbl);
        gtk_box_append(GTK_BOX(rowbox), name_lbl);
        gtk_box_append(GTK_BOX(rowbox), fn_lbl);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowbox);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    gtk_box_append(GTK_BOX(card), list);

    GtkWidget *goto_btn = gtk_button_new_with_label(_("Open Buttons page"));
    gtk_widget_add_css_class(goto_btn, "flat");
    g_object_set_data(G_OBJECT(goto_btn), "target-page", (gpointer)"buttons");
    g_signal_connect(goto_btn, "clicked", G_CALLBACK(on_home_goto_clicked), app);
    gtk_box_append(GTK_BOX(card), goto_btn);
    return card;
}

GtkWidget *build_home_page(App *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 16);
    gtk_widget_set_margin_start(outer, 16);
    gtk_widget_set_margin_end(outer, 16);

    gtk_box_append(GTK_BOX(outer), build_hero(app));

    /* Row 1: 4 equal-width stat cards (DPI / Report rate / Battery /
     * Lighting), each a glance-and-jump into its detail page. */
    GtkWidget *stat_row = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stat_row), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(stat_row), TRUE);
    gtk_box_append(GTK_BOX(outer), stat_row);

    /* A GtkFlowBox (not a plain horizontal box) so 6 chips never force
     * this card -- and with it the whole 4-column stat row -- wider
     * than the space actually available; it wraps to a second line
     * instead of demanding extra width. */
    GtkWidget *dpi_chips = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(dpi_chips), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(dpi_chips), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(dpi_chips), 3);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(dpi_chips), 1);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(dpi_chips), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(dpi_chips), 4);
    gtk_widget_set_halign(dpi_chips, GTK_ALIGN_CENTER);
    static const int home_dpi_chip_values[AJAZZ_DPI_STAGES] = {400, 800, 1600, 3200, 6400, 12800};
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%d", home_dpi_chip_values[i]);
        GtkWidget *chip = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(chip, "pill");
        gtk_widget_add_css_class(chip, "flat");
        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(chip, "clicked", G_CALLBACK(on_home_dpi_chip_clicked), data, free_closure_data, 0);
        gtk_flow_box_append(GTK_FLOW_BOX(dpi_chips), chip);
    }
    gtk_grid_attach(GTK_GRID(stat_row),
        build_stat_card(_("DPI"), &app->home_dpi_val, _("Current DPI"), dpi_chips, _("DPI Stages"), "config", app),
        0, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(stat_row),
        build_stat_card(_("Polling rate"), &app->home_rate_val, _("Report rate"), NULL, _("Performance settings"), "config", app),
        1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(stat_row),
        build_battery_stat_card(app, _("Sensor & power"), "config"),
        2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(stat_row),
        build_stat_card(_("Lighting"), &app->home_light_val, _("Current effect"), NULL, _("Lighting settings"), "lighting", app),
        3, 0, 1, 1);

    /* Row 2: Button mapping (2/3 width) + Quick actions (1/3 width). */
    GtkWidget *row2 = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(row2), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(row2), TRUE);
    gtk_box_append(GTK_BOX(outer), row2);
    GtkWidget *mapping_card = build_button_mapping_card(app);
    gtk_grid_attach(GTK_GRID(row2), mapping_card, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(row2), build_quick_actions_card(app), 2, 0, 1, 1);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_box_append(GTK_BOX(outer), grid);

    /* Device details card */
    GtkWidget *dev_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(dev_box), GTK_WIDGET(info_row(_("VID:PID"), &app->lbl_vidpid)));
    gtk_box_append(GTK_BOX(dev_box), GTK_WIDGET(info_row(_("Firmware id"), &app->lbl_devid)));
    GtkWidget *dev_card = make_card(_("Device details"), dev_box);
    gtk_grid_attach(GTK_GRID(grid), dev_card, 0, 0, 1, 1);

    /* Compatibility card */
    GtkWidget *compat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    app->lbl_confirmed = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_confirmed), "dim-label");
    gtk_label_set_wrap(app->lbl_confirmed, TRUE);
    gtk_widget_set_halign(GTK_WIDGET(app->lbl_confirmed), GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(GTK_WIDGET(app->lbl_confirmed), 4);
    gtk_box_append(GTK_BOX(compat_box), GTK_WIDGET(app->lbl_confirmed));
    for (size_t i = 0; i < N_CAP_ROWS; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *lbl = gtk_label_new(gettext(CAP_ROWS[i].label));
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_set_hexpand(lbl, TRUE);
        app->cap_badge[i] = make_confirm_badge();
        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), app->cap_badge[i]);
        gtk_box_append(GTK_BOX(compat_box), row);
    }
    GtkWidget *compat_card = make_card(_("Compatibility"), compat_box);
    gtk_grid_attach(GTK_GRID(grid), compat_card, 1, 0, 1, 2);

    /* Known limitations card */
    GtkWidget *issues = gtk_label_new(
        _("- Macros fit 3 key/button press+release actions per button in a\n"
          "  single write; longer macros (up to 50) use a multi-packet write.\n"
          "- Lighting effect names beyond \"off\", \"rainbow\" and \"constant\"\n"
          "  are not reliably distinguished -- some share the same wire command.\n"
          "- Lift-off distance may also control \"wake mouse on motion\" --\n"
          "  a capture of that toggle landed on the exact same byte."));
    gtk_widget_add_css_class(issues, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(issues), TRUE);
    gtk_widget_set_halign(issues, GTK_ALIGN_START);
    GtkWidget *issues_card = make_card(_("Known limitations"), issues);
    gtk_grid_attach(GTK_GRID(grid), issues_card, 0, 1, 1, 1);

    /* Danger zone card */
    GtkWidget *btn_factory_reset = gtk_button_new_with_label(_("Restore factory defaults"));
    gtk_widget_add_css_class(btn_factory_reset, "destructive-action");
    gtk_widget_add_css_class(btn_factory_reset, "pill");
    gtk_widget_set_halign(btn_factory_reset, GTK_ALIGN_START);
    g_signal_connect(btn_factory_reset, "clicked", G_CALLBACK(on_factory_reset_clicked), app);
    GtkWidget *danger_card = make_card(_("Danger zone"), btn_factory_reset);
    gtk_grid_attach(GTK_GRID(grid), danger_card, 0, 2, 2, 1);

    return outer;
}
