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
        const char *label = ajazz_battery_label(raw);

        char pct_line[16];
        snprintf(pct_line, sizeof(pct_line), "%d%%", raw);
        gtk_label_set_text(app->lbl_battery, pct_line);
        gtk_widget_set_tooltip_text(GTK_WIDGET(app->lbl_battery), label);
        set_battery_bars(app, tier);

        /* dev.charging is independent of which interface we actually
         * read the battery from (ajazz_open() prefers the 2.4G one when
         * both exist) -- see the comment on ajazz_dev_t::charging in
         * protocol/transport.h. Gated on AJAZZ_CAP_BATTERY so a
         * wired-only model (no battery at all) never shows this. */
        gboolean charging = (app->dev.confirmed & AJAZZ_CAP_BATTERY) && app->dev.charging;
        gtk_widget_set_visible(app->lbl_charging, charging);

        char batt_line[128];
        snprintf(batt_line, sizeof(batt_line), charging ? _("Battery: %d%% -- %s (charging)") : _("Battery: %d%% -- %s"), raw, label);
        ajazz_tray_set_status(app->tray, device_line, batt_line);
        ajazz_tray_set_icon(app->tray, battery_percent_icon(raw, charging));
    } else {
        gtk_label_set_text(app->lbl_battery, "?");
        gtk_widget_set_visible(app->lbl_charging, FALSE);
        set_battery_bars(app, -1);
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
        set_battery_bars(app, -1);
        ajazz_tray_set_status(app->tray, _("No AJAZZ mouse detected"), "");
        ajazz_tray_set_icon(app->tray, battery_percent_icon(-1, FALSE));
        return;
    }

    set_status_widgets(app->status_dot, app->status_lbl, TRUE);
    set_status_widgets(app->hero_status_dot, app->hero_status_lbl, TRUE);
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

GtkWidget *build_home_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *hero_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_add(hero_group, build_hero(app));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), hero_group);

    AdwPreferencesGroup *dev_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(dev_group, _("Device details"));
    adw_preferences_group_add(dev_group, GTK_WIDGET(info_row(_("VID:PID"), &app->lbl_vidpid)));
    adw_preferences_group_add(dev_group, GTK_WIDGET(info_row(_("Firmware id"), &app->lbl_devid)));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dev_group);

    AdwPreferencesGroup *compat_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(compat_group, _("Compatibility"));
    app->lbl_confirmed = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(app->lbl_confirmed), "dim-label");
    gtk_label_set_wrap(app->lbl_confirmed, TRUE);
    gtk_widget_set_halign(GTK_WIDGET(app->lbl_confirmed), GTK_ALIGN_START);
    gtk_widget_set_margin_start(GTK_WIDGET(app->lbl_confirmed), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(app->lbl_confirmed), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(app->lbl_confirmed), 4);
    adw_preferences_group_add(compat_group, GTK_WIDGET(app->lbl_confirmed));

    for (size_t i = 0; i < N_CAP_ROWS; i++) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), gettext(CAP_ROWS[i].label));
        app->cap_badge[i] = make_confirm_badge();
        adw_action_row_add_suffix(row, app->cap_badge[i]);
        adw_preferences_group_add(compat_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), compat_group);

    AdwPreferencesGroup *issues_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(issues_group, _("Known limitations"));
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
    gtk_widget_set_margin_start(issues, 6);
    gtk_widget_set_margin_end(issues, 6);
    adw_preferences_group_add(issues_group, issues);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), issues_group);

    AdwPreferencesGroup *danger_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(danger_group, _("Danger zone"));
    GtkWidget *btn_factory_reset = gtk_button_new_with_label(_("Restore factory defaults"));
    gtk_widget_add_css_class(btn_factory_reset, "destructive-action");
    gtk_widget_add_css_class(btn_factory_reset, "pill");
    gtk_widget_set_halign(btn_factory_reset, GTK_ALIGN_START);
    g_signal_connect(btn_factory_reset, "clicked", G_CALLBACK(on_factory_reset_clicked), app);
    adw_preferences_group_add(danger_group, btn_factory_reset);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), danger_group);

    return page;
}
