/*
 * Overview: the mouse at a glance -- identity, battery, the four numbers
 * that matter, your profiles, and what each button does. Everything on it
 * is a shortcut into the page that owns the setting.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "../devices/device.h"
#include "../devices/registry.h"

typedef struct {
    GtkWidget *name, *meta, *mode_chip, *tested_chip, *charge_chip;
    GtkWidget *ring, *pct, *pct_sub;
    GtkWidget *hero_pic;
    char hero_stem[16];
    GtkWidget *dpi_val, *dpi_sub, *rate_val, *rate_sub, *light_val, *light_sub, *saver_val, *saver_sub;
    GtkWidget *profiles_box, *profiles_card;
    AdwActionRow *button_row[AJAZZ_KEY_SLOTS];
    AdwActionRow *r_model, *r_fw, *r_vidpid, *r_iface, *r_node;
    GtkWidget *cap_chip[7];
} HomeUI;

static HomeUI H;

static const struct { uint32_t cap; const char *label; } CAPS[] = {
    {AJAZZ_CAP_DPI, N_("DPI")}, {AJAZZ_CAP_RATE, N_("Report rate")}, {AJAZZ_CAP_RGB, N_("Lighting")},
    {AJAZZ_CAP_SENSOR, N_("Sensor")}, {AJAZZ_CAP_KEYS, N_("Buttons")}, {AJAZZ_CAP_BATTERY, N_("Battery")},
    {AJAZZ_CAP_MACRO, N_("Macros")},
};

/* ---------------- battery ---------------- */

static const char *battery_icon(int percent, gboolean charging)
{
    static char name[40];
    if (percent < 0)
        return "battery-missing-symbolic";
    if (percent > 100) percent = 100;
    int level = (percent / 10) * 10;
    const char *suffix = !charging ? "" : (level == 100) ? "-charged" : "-charging";
    snprintf(name, sizeof(name), "battery-level-%d%s-symbolic", level, suffix);
    return name;
}

static const char *battery_word(int percent)
{
    if (percent >= 75) return _("Full");
    if (percent >= 50) return _("Good");
    if (percent >= 25) return _("Medium");
    return _("Low");
}

static App *g_app;

static void draw_ring(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer data)
{
    (void)a;
    App *app = data;
    double cx = w / 2.0, cy = h / 2.0, r = MIN(w, h) / 2.0 - 7;
    cairo_set_line_width(cr, 8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_stroke(cr);
    int p = app->battery_percent;
    if (p < 0)
        return;
    double rr, g, b;
    if (app->charging) { rr = 0.91; g = 0.22; b = 0.29; }
    else if (p < 20) { rr = 0.90; g = 0.28; b = 0.30; }
    else if (p < 50) { rr = 0.91; g = 0.64; b = 0.09; }
    else { rr = 0.18; g = 0.72; b = 0.45; }
    cairo_arc(cr, cx, cy, r, -G_PI_2, -G_PI_2 + (p / 100.0) * 2 * G_PI);
    cairo_set_source_rgb(cr, rr, g, b);
    cairo_stroke(cr);
}

void home_set_battery(App *app, int percent, gboolean charging)
{
    (void)charging;
    char dev[300], batt[96];
    snprintf(dev, sizeof(dev), "%s", app->connected ? app->dev.name : _("No AJAZZ mouse detected"));
    if (percent >= 0 && app->connected) {
        snprintf(batt, sizeof(batt), app->charging ? _("Battery: %d%% (charging)") : _("Battery: %d%%"), percent);
        if (app->tray) ajazz_tray_set_icon(app->tray, battery_icon(percent, app->charging));
    } else {
        batt[0] = '\0';
        if (app->tray) ajazz_tray_set_icon(app->tray, battery_icon(-1, FALSE));
    }
    if (app->tray)
        ajazz_tray_set_status(app->tray, dev, batt);

    if (!H.pct)
        return;
    if (percent >= 0 && app->connected) {
        char s[16];
        snprintf(s, sizeof(s), "%d%%", percent);
        gtk_label_set_text(GTK_LABEL(H.pct), s);
        gtk_label_set_text(GTK_LABEL(H.pct_sub), app->charging ? _("Charging") : battery_word(percent));
    } else {
        gtk_label_set_text(GTK_LABEL(H.pct), "\xE2\x80\x94");
        gtk_label_set_text(GTK_LABEL(H.pct_sub), _("Unknown"));
    }
    gtk_widget_set_visible(H.charge_chip, app->connected && app->charging);
    gtk_widget_queue_draw(H.ring);
}

/* ---------------- tiles ---------------- */

static void on_tile(GtkButton *b, gpointer user_data)
{
    switch_to_page(user_data, g_object_get_data(G_OBJECT(b), "page"));
}

static GtkWidget *tile(App *app, const char *label, const char *page, GtkWidget **val, GtkWidget **sub)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "aj-tile");
    gtk_widget_set_hexpand(b, TRUE);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_add_css_class(l, "aj-tile-label");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    *val = gtk_label_new("\xE2\x80\x94");
    gtk_widget_add_css_class(*val, "aj-tile-value");
    gtk_widget_set_halign(*val, GTK_ALIGN_START);
    *sub = gtk_label_new("");
    gtk_widget_add_css_class(*sub, "aj-tile-sub");
    gtk_widget_set_halign(*sub, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(v), l);
    gtk_box_append(GTK_BOX(v), *val);
    gtk_box_append(GTK_BOX(v), *sub);
    gtk_button_set_child(GTK_BUTTON(b), v);
    g_object_set_data(G_OBJECT(b), "page", (gpointer)page);
    g_signal_connect(b, "clicked", G_CALLBACK(on_tile), app);
    return b;
}

/* ---------------- profile strip ---------------- */

static void on_profile_chip(GtkButton *b, gpointer user_data)
{
    App *app = user_data;
    const char *name = g_object_get_data(G_OBJECT(b), "name");
    if (profiles_apply_by_name(app, name))
        toastf(app, _("Applying \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6"), name);
    else
        toast(app, _("Couldn't apply that profile."));
}

static void rebuild_profiles(App *app)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(H.profiles_box)))
        gtk_flow_box_remove(GTK_FLOW_BOX(H.profiles_box), c);
    GPtrArray *names = profiles_list_names();
    for (guint i = 0; i < names->len; i++) {
        const char *n = g_ptr_array_index(names, i);
        GtkWidget *b = ui_button_new(n, NULL, strcmp(n, app->settings_last_profile) == 0 ? "primary" : "secondary");
        g_object_set_data_full(G_OBJECT(b), "name", g_strdup(n), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(on_profile_chip), app);
        gtk_flow_box_append(GTK_FLOW_BOX(H.profiles_box), b);
    }
    gtk_widget_set_visible(H.profiles_card, names->len > 0);
    g_ptr_array_free(names, TRUE);
}

/* ---------------- factory reset ---------------- */

static gboolean refresh_later(gpointer user_data)
{
    app_refresh_all(user_data);
    return G_SOURCE_REMOVE;
}

static void reset_work(App *app, gpointer d)
{
    (void)d;
    ajazz_report_t r;
    ajazz_build_factory_reset(&r);
    ajazz_send_and_wait_mt(app, &r, 1500);
}

static void reset_done(App *app, gpointer d)
{
    (void)d;
    toast(app, _("Factory settings restored."));
    g_timeout_add(1500, refresh_later, app);
}

static void on_reset_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    App *app = user_data;
    if (strcmp(resp, "reset") == 0 && app->connected)
        app_async(app, reset_work, reset_done, NULL, NULL);
}

static void on_reset_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Restore factory settings?"),
        _("DPI and colours, report rate, lighting, sensor and power settings, buttons and macros all go back "
          "to what the mouse shipped with. This can't be undone (your saved profiles are kept).")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "reset", _("Restore"));
    adw_alert_dialog_set_response_appearance(dlg, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_reset_response), app);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
}

static void on_buttons_row(GtkListBoxRow *r, gpointer user_data)
{
    (void)r;
    switch_to_page(user_data, "buttons");
}

/* ---------------- refresh ---------------- */

void refresh_home(App *app)
{
    if (!H.name)
        return;
    gboolean on = app->connected;
    gtk_label_set_text(GTK_LABEL(H.name), on ? app->dev.name : _("No mouse connected"));

    const char *stem = on ? ajazz_lookup_image(app->dev.name) : "generic";
    if (strcmp(stem, H.hero_stem) != 0) {
        g_strlcpy(H.hero_stem, stem, sizeof(H.hero_stem));
        GdkTexture *tex = ui_mouse_texture(stem, 64, 118);
        if (tex) {
            gtk_picture_set_paintable(GTK_PICTURE(H.hero_pic), GDK_PAINTABLE(tex));
            g_object_unref(tex);
        }
    }

    if (on) {
        char meta[160];
        snprintf(meta, sizeof(meta), "%04X:%04X  \xC2\xB7  %s", app->dev.vid, app->dev.pid,
            app->dev.dev_id[0] ? app->dev.dev_id : "?");
        gtk_label_set_text(GTK_LABEL(H.meta), meta);
        const char *mode = app->dev.mode[0] ? app->dev.mode : "USB";
        ui_chip_set(H.mode_chip, strcmp(mode, "2.4G") == 0 ? _("2.4G wireless") : _("USB cable"),
            strcmp(mode, "2.4G") == 0 ? UI_CHIP_ACCENT : UI_CHIP_OK);
        gboolean tested = app->dev.confirmed != 0;
        ui_chip_set(H.tested_chip, tested ? _("Verified model") : _("Untested model"),
            tested ? UI_CHIP_OK : UI_CHIP_WARN);
    } else {
        gtk_label_set_text(GTK_LABEL(H.meta), _("Plug in the mouse or its dongle."));
    }
    gtk_widget_set_visible(H.mode_chip, on);
    gtk_widget_set_visible(H.tested_chip, on);

    char b[48];
    if (on && app->dpi_cache[app->dpi_selected]) {
        snprintf(b, sizeof(b), "%u", app->dpi_cache[app->dpi_selected]);
        gtk_label_set_text(GTK_LABEL(H.dpi_val), b);
        int of = (app->dpi_count >= 1 && app->dpi_count <= AJAZZ_DPI_STAGES) ? app->dpi_count : AJAZZ_DPI_STAGES;
        snprintf(b, sizeof(b), _("Stage %d of %d"), app->dpi_selected + 1, of);
        gtk_label_set_text(GTK_LABEL(H.dpi_sub), b);
    } else {
        gtk_label_set_text(GTK_LABEL(H.dpi_val), "\xE2\x80\x94");
        gtk_label_set_text(GTK_LABEL(H.dpi_sub), "");
    }
    if (on && app->rate_hz > 0) {
        snprintf(b, sizeof(b), "%d Hz", app->rate_hz);
        gtk_label_set_text(GTK_LABEL(H.rate_val), b);
        gtk_label_set_text(GTK_LABEL(H.rate_sub), app->rate_hz >= 1000 ? _("Smoothest") : app->rate_hz <= 250 ? _("Battery friendly") : _("Balanced"));
    } else {
        gtk_label_set_text(GTK_LABEL(H.rate_val), "\xE2\x80\x94");
        gtk_label_set_text(GTK_LABEL(H.rate_sub), "");
    }
    gtk_label_set_text(GTK_LABEL(H.light_val), on ? lighting_effect_name(app->light.effect) : "\xE2\x80\x94");
    if (on && app->light.effect != LFX_OFF) {
        snprintf(b, sizeof(b), _("Brightness %d%%"), app->light.brightness * 25);
        gtk_label_set_text(GTK_LABEL(H.light_sub), b);
    } else {
        gtk_label_set_text(GTK_LABEL(H.light_sub), "");
    }
    gtk_label_set_text(GTK_LABEL(H.saver_val), app->saver_active ? _("On") : _("Off"));
    gtk_label_set_text(GTK_LABEL(H.saver_sub), app->saver_active ? _("Saving power") : _("Full performance"));

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char d[96];
        buttons_describe(app, i, d, sizeof(d));
        adw_action_row_set_subtitle(H.button_row[i], on ? d : "");
    }

    if (on) {
        adw_action_row_set_subtitle(H.r_model, app->dev.name);
        adw_action_row_set_subtitle(H.r_fw, app->dev.dev_id[0] ? app->dev.dev_id : "?");
        char vp[32];
        snprintf(vp, sizeof(vp), "%04X:%04X", app->dev.vid, app->dev.pid);
        adw_action_row_set_subtitle(H.r_vidpid, vp);
        adw_action_row_set_subtitle(H.r_iface, app->dev.mode[0] ? app->dev.mode : "USB");
        adw_action_row_set_subtitle(H.r_node, app->dev.path);
        for (guint i = 0; i < G_N_ELEMENTS(CAPS); i++) {
            gboolean ok = app->dev.confirmed & CAPS[i].cap;
            char t[64];
            snprintf(t, sizeof(t), "%s %s", ok ? "\xE2\x9C\x93" : "\xC2\xB7", gettext(CAPS[i].label));
            ui_chip_set(H.cap_chip[i], t, ok ? UI_CHIP_OK : UI_CHIP_NEUTRAL);
        }
    }
    rebuild_profiles(app);
    home_set_battery(app, app->battery_percent, app->charging);
}

/* ---------------- build ---------------- */

static GtkWidget *build_hero(App *app)
{
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_add_css_class(hero, "aj-hero");

    /* the connected model's photo (swapped in by refresh_home) */
    H.hero_pic = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(H.hero_pic), FALSE);
    gtk_widget_set_valign(H.hero_pic, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(H.hero_pic, GTK_ALIGN_START);
    gtk_widget_add_css_class(H.hero_pic, "aj-photo");
    gtk_box_append(GTK_BOX(hero), H.hero_pic);

    GtkWidget *id = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(id, TRUE);
    gtk_widget_set_valign(id, GTK_ALIGN_CENTER);
    H.name = gtk_label_new("");
    gtk_widget_add_css_class(H.name, "aj-hero-name");
    gtk_widget_set_halign(H.name, GTK_ALIGN_START);
    H.meta = gtk_label_new("");
    gtk_widget_add_css_class(H.meta, "aj-hero-meta");
    gtk_widget_set_halign(H.meta, GTK_ALIGN_START);
    GtkWidget *chips = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    H.mode_chip = ui_chip_new("USB", UI_CHIP_OK);
    H.tested_chip = ui_chip_new("", UI_CHIP_NEUTRAL);
    H.charge_chip = ui_chip_new(_("Charging"), UI_CHIP_ACCENT);
    gtk_widget_set_visible(H.charge_chip, FALSE);
    gtk_box_append(GTK_BOX(chips), H.mode_chip);
    gtk_box_append(GTK_BOX(chips), H.tested_chip);
    gtk_box_append(GTK_BOX(chips), H.charge_chip);
    gtk_box_append(GTK_BOX(id), H.name);
    gtk_box_append(GTK_BOX(id), H.meta);
    gtk_box_append(GTK_BOX(id), chips);
    gtk_box_append(GTK_BOX(hero), id);

    GtkWidget *ov = gtk_overlay_new();
    gtk_widget_set_valign(ov, GTK_ALIGN_CENTER);
    H.ring = gtk_drawing_area_new();
    gtk_widget_set_size_request(H.ring, 92, 92);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(H.ring), draw_ring, app, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(ov), H.ring);
    GtkWidget *mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(mid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(mid, GTK_ALIGN_CENTER);
    H.pct = gtk_label_new("\xE2\x80\x94");
    gtk_widget_add_css_class(H.pct, "title-3");
    H.pct_sub = gtk_label_new("");
    gtk_widget_add_css_class(H.pct_sub, "caption");
    gtk_widget_add_css_class(H.pct_sub, "dim-label");
    gtk_box_append(GTK_BOX(mid), H.pct);
    gtk_box_append(GTK_BOX(mid), H.pct_sub);
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), mid);
    gtk_box_append(GTK_BOX(hero), ov);
    return hero;
}

GtkWidget *build_home_page(App *app)
{
    g_app = app;
    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Overview"), NULL, NULL, &body);

    gtk_box_append(GTK_BOX(body), build_hero(app));

    GtkWidget *tiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_set_homogeneous(GTK_BOX(tiles), TRUE);
    gtk_box_append(GTK_BOX(tiles), tile(app, _("DPI"), "performance", &H.dpi_val, &H.dpi_sub));
    gtk_box_append(GTK_BOX(tiles), tile(app, _("Report rate"), "performance", &H.rate_val, &H.rate_sub));
    gtk_box_append(GTK_BOX(tiles), tile(app, _("Lighting"), "lighting", &H.light_val, &H.light_sub));
    gtk_box_append(GTK_BOX(tiles), tile(app, _("Battery saver"), "performance", &H.saver_val, &H.saver_sub));
    gtk_box_append(GTK_BOX(body), tiles);

    /* profiles strip */
    GtkWidget *cb;
    H.profiles_card = ui_card_new(_("Profiles"), _("Switch the whole mouse with one click."), &cb);
    H.profiles_box = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(H.profiles_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(H.profiles_box), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(H.profiles_box), 8);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(H.profiles_box), 8);
    gtk_box_append(GTK_BOX(cb), ui_card_pad(H.profiles_box));
    gtk_widget_set_visible(H.profiles_card, FALSE);
    gtk_box_append(GTK_BOX(body), H.profiles_card);

    /* buttons summary */
    GtkWidget *card = ui_card_new(_("Buttons"), NULL, &cb);
    GtkWidget *list = ui_list_new();
    static const char *const names[AJAZZ_KEY_SLOTS] = {
        N_("Button 1"), N_("Button 2"), N_("Button 3"), N_("Button 4"), N_("Button 5"), N_("Button 6 (DPI)"),
    };
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        H.button_row[i] = ui_row_new(gettext(names[i]), "", gtk_image_new_from_icon_name("go-next-symbolic"));
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(H.button_row[i]), TRUE);
        g_signal_connect(H.button_row[i], "activated", G_CALLBACK(on_buttons_row), app);
        gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(H.button_row[i]));
    }
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    /* device */
    card = ui_card_new(_("This mouse"), NULL, &cb);
    list = ui_list_new();
    H.r_model = ui_row_new(_("Model"), "\xE2\x80\x94", NULL);
    H.r_fw = ui_row_new(_("Firmware ID"), "\xE2\x80\x94", NULL);
    H.r_vidpid = ui_row_new(_("USB ID"), "\xE2\x80\x94", NULL);
    H.r_iface = ui_row_new(_("Connection"), "\xE2\x80\x94", NULL);
    H.r_node = ui_row_new(_("Device node"), "\xE2\x80\x94", NULL);
    AdwActionRow *rows[] = {H.r_model, H.r_fw, H.r_vidpid, H.r_iface, H.r_node};
    for (guint i = 0; i < G_N_ELEMENTS(rows); i++)
        gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(rows[i]));
    gtk_box_append(GTK_BOX(cb), list);

    GtkWidget *caps = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(caps), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(caps), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(caps), 6);
    for (guint i = 0; i < G_N_ELEMENTS(CAPS); i++) {
        H.cap_chip[i] = ui_chip_new(gettext(CAPS[i].label), UI_CHIP_NEUTRAL);
        gtk_flow_box_append(GTK_FLOW_BOX(caps), H.cap_chip[i]);
    }
    GtkWidget *capnote = gtk_label_new(_("\xE2\x9C\x93 means this feature has been verified on this exact model."));
    gtk_widget_add_css_class(capnote, "aj-card-desc");
    gtk_widget_set_halign(capnote, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(capnote, 4);
    GtkWidget *capbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(capbox), caps);
    gtk_box_append(GTK_BOX(capbox), capnote);
    gtk_box_append(GTK_BOX(cb), ui_card_pad(capbox));
    gtk_box_append(GTK_BOX(body), card);

    /* maintenance */
    card = ui_card_new(_("Maintenance"), NULL, &cb);
    list = ui_list_new();
    GtkWidget *reset = ui_button_new(_("Restore\xE2\x80\xA6"), "edit-undo-symbolic", "danger");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_clicked), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Restore factory settings"),
        _("Puts everything on the mouse back to how it shipped."), reset)));
    gtk_box_append(GTK_BOX(cb), list);
    gtk_box_append(GTK_BOX(body), card);

    refresh_home(app);
    return page;
}
