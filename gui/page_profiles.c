/*
 * Profiles page: local (PC-side) configuration snapshots, saved as JSON
 * files under ~/.config/ajazzy/profiles/. This is NOT a mouse feature --
 * the protocol has no on-device profile storage (there's no "profile
 * slot" concept anywhere in the wire format, see protocol/hid.h), so a
 * "profile" here just means "everything this app already keeps cached
 * about the mouse, written to a file and reapplied later". Applying a
 * profile re-sends every field it captured through the same *_mt()
 * helpers/apply logic every other page already uses -- see
 * mouse_page_apply_profile_dpi()/_rate(), lighting_apply_profile(), and
 * buttons_apply_profile_table() in their respective files.
 *
 * Macros are intentionally NOT captured by a profile -- see the comment
 * on PROFILE_MACROS_NOTE below for why.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"
#include "../devices/device.h"

/* Mirrors the RGB_* enum in page_lighting.c -- duplicated as plain
 * integers here (not shared) since a profile just needs to round-trip
 * whatever number was selected, not interpret it. */
#define PROFILE_RGB_OFF 0
#define PROFILE_RGB_RAINBOW 1
#define PROFILE_RGB_CONSTANT 2
#define PROFILE_RGB_SOFT_BREATHING 3
#define PROFILE_RGB_SOFT_COLORCYCLE 4

typedef struct {
    char name[64];
    uint16_t dpi[AJAZZ_DPI_STAGES];
    uint8_t dpi_color[AJAZZ_DPI_STAGES][3];
    int dpi_selected;
    int rate_hz;
    int lighting_effect;
    int lighting_brightness;
    int lighting_speed;
    uint8_t lighting_soft_color[3];
    uint8_t buttons_raw[AJAZZ_KEY_SLOTS][3];
} Profile;

static char *profile_path_for(const char *name)
{
    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return NULL;
    char *safe = g_strdup(name);
    for (char *p = safe; *p; p++)
        if (*p == '/' || *p == '\\') *p = '_';
    char *filename = g_strdup_printf("%s.json", safe);
    char *path = g_build_filename(dir, filename, NULL);
    g_free(dir); g_free(safe); g_free(filename);
    return path;
}

static gboolean profile_save(const Profile *p)
{
    char *path = profile_path_for(p->name);
    if (!path) return FALSE;

    GString *out = g_string_new("{\n  ");
    json_put_string(out, "name", p->name); g_string_append(out, ",\n  ");

    int dpi_ints[AJAZZ_DPI_STAGES];
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) dpi_ints[i] = p->dpi[i];
    json_put_int_array(out, "dpi", dpi_ints, AJAZZ_DPI_STAGES); g_string_append(out, ",\n  ");

    int color_ints[AJAZZ_DPI_STAGES * 3];
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
        for (int c = 0; c < 3; c++) color_ints[i * 3 + c] = p->dpi_color[i][c];
    json_put_int_array(out, "dpi_color", color_ints, AJAZZ_DPI_STAGES * 3); g_string_append(out, ",\n  ");

    json_put_int(out, "dpi_selected", p->dpi_selected); g_string_append(out, ",\n  ");
    json_put_int(out, "rate_hz", p->rate_hz); g_string_append(out, ",\n  ");
    json_put_int(out, "lighting_effect", p->lighting_effect); g_string_append(out, ",\n  ");
    json_put_int(out, "lighting_brightness", p->lighting_brightness); g_string_append(out, ",\n  ");
    json_put_int(out, "lighting_speed", p->lighting_speed); g_string_append(out, ",\n  ");

    int soft_color_ints[3] = {p->lighting_soft_color[0], p->lighting_soft_color[1], p->lighting_soft_color[2]};
    json_put_int_array(out, "lighting_soft_color", soft_color_ints, 3); g_string_append(out, ",\n  ");

    int button_ints[AJAZZ_KEY_SLOTS * 3];
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        for (int c = 0; c < 3; c++) button_ints[i * 3 + c] = p->buttons_raw[i][c];
    json_put_int_array(out, "buttons_raw", button_ints, AJAZZ_KEY_SLOTS * 3);

    g_string_append(out, "\n}\n");
    gboolean ok = json_write_file(path, out->str);
    g_string_free(out, TRUE);
    g_free(path);
    return ok;
}

static gboolean profile_load(const char *filename, Profile *out)
{
    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return FALSE;
    char *path = g_build_filename(dir, filename, NULL);
    g_free(dir);
    char *text = json_read_file(path);
    g_free(path);
    if (!text) return FALSE;

    memset(out, 0, sizeof(*out));
    json_get_string(text, "name", out->name, sizeof(out->name));

    int dpi_ints[AJAZZ_DPI_STAGES] = {0};
    json_get_int_array(text, "dpi", dpi_ints, AJAZZ_DPI_STAGES);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) out->dpi[i] = (uint16_t)dpi_ints[i];

    int color_ints[AJAZZ_DPI_STAGES * 3] = {0};
    json_get_int_array(text, "dpi_color", color_ints, AJAZZ_DPI_STAGES * 3);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
        for (int c = 0; c < 3; c++) out->dpi_color[i][c] = (uint8_t)color_ints[i * 3 + c];

    out->dpi_selected = json_get_int(text, "dpi_selected", 0);
    out->rate_hz = json_get_int(text, "rate_hz", 1000);
    out->lighting_effect = json_get_int(text, "lighting_effect", PROFILE_RGB_RAINBOW);
    out->lighting_brightness = json_get_int(text, "lighting_brightness", 4);
    out->lighting_speed = json_get_int(text, "lighting_speed", 2);

    int soft_color_ints[3] = {0xe8, 0x39, 0x4a};
    json_get_int_array(text, "lighting_soft_color", soft_color_ints, 3);
    for (int c = 0; c < 3; c++) out->lighting_soft_color[c] = (uint8_t)soft_color_ints[c];

    int button_ints[AJAZZ_KEY_SLOTS * 3] = {0};
    json_get_int_array(text, "buttons_raw", button_ints, AJAZZ_KEY_SLOTS * 3);
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        for (int c = 0; c < 3; c++) out->buttons_raw[i][c] = (uint8_t)button_ints[i * 3 + c];

    g_free(text);
    return TRUE;
}

/* Snapshot everything this app already keeps cached about the mouse --
 * plus a fresh query for the button table, since that's the one field no
 * page keeps a full always-current raw copy of (page_buttons.c only
 * keeps dropdown selections, not the raw wire bytes). */
static gboolean profile_capture_current(App *app, const char *name, Profile *out)
{
    if (!ensure_connected(app))
        return FALSE;

    memset(out, 0, sizeof(*out));
    g_strlcpy(out->name, name, sizeof(out->name));
    memcpy(out->dpi, app->dpi_cache, sizeof(out->dpi));
    memcpy(out->dpi_color, app->dpi_color_cache, sizeof(out->dpi_color));
    out->dpi_selected = app->dpi_selected;
    out->rate_hz = app->rate_hz > 0 ? app->rate_hz : 1000;
    out->lighting_effect = app->rgb_effect ? (int)adw_combo_row_get_selected(app->rgb_effect) : PROFILE_RGB_RAINBOW;
    out->lighting_brightness = app->rgb_brightness ? (int)adw_spin_row_get_value(app->rgb_brightness) : 4;
    out->lighting_speed = app->rgb_speed ? (int)adw_spin_row_get_value(app->rgb_speed) : 2;
    memcpy(out->lighting_soft_color, app->rgb_soft_color, sizeof(out->lighting_soft_color));

    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) == 0) {
        for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
            memcpy(out->buttons_raw[i], &reply.payload[i * 3], 3);
    }
    return TRUE;
}

/* Re-sends everything a profile captured, page by page, reusing each
 * page's own apply logic (see the file comment at the top). Macros are
 * skipped -- see PROFILE_MACROS_NOTE. */
static void profile_apply(App *app, const Profile *p)
{
    gboolean ok_dpi = mouse_page_apply_profile_dpi(app, p->dpi, p->dpi_color, p->dpi_selected);
    gboolean ok_rate = mouse_page_apply_profile_rate(app, p->rate_hz);
    lighting_apply_profile(app, p->lighting_effect, p->lighting_brightness, p->lighting_speed, p->lighting_soft_color);
    gboolean ok_buttons = buttons_apply_profile_table(app, p->buttons_raw);

    if (ok_dpi && ok_rate && ok_buttons)
        toast(app, _("Profile applied."));
    else
        toast(app, _("Profile applied, but some settings failed to send -- check the connection."));

    refresh_home(app);
}

/* PROFILE_MACROS_NOTE: macros are per-button variable-length action
 * lists (up to 50 press/release steps each, see gui/page_macro.c), built
 * up interactively through AdwComboRow widgets rather than kept as a
 * single always-current raw buffer anywhere in App. Capturing and
 * replaying that faithfully would mean either re-deriving it from 5
 * live device queries with a bespoke serializer for a format that's
 * still partly reverse-engineered (see the multi-packet write caveat in
 * gui/page_home.c's "Known limitations" card), or reaching into
 * page_macro.c's private widget arrays from here. Given the risk of a
 * subtly wrong reserialization silently writing a corrupted macro to
 * real hardware, this profile format leaves macros out entirely rather
 * than guess -- re-record macros by hand after applying a profile. */

static GtkWidget *g_profiles_list; /* the tile row, rebuilt by refresh_profiles() */

static void populate_profiles(App *app);

static void on_apply_profile_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    char *filename = data[1];
    Profile p;
    if (!profile_load(filename, &p)) {
        toast(app, _("Failed to read that profile."));
        return;
    }
    profile_apply(app, &p);
}

static void on_delete_profile_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    char *filename = data[1];
    char *dir = ajazzy_config_subdir("profiles");
    if (dir) {
        char *path = g_build_filename(dir, filename, NULL);
        g_remove(path);
        g_free(path);
        g_free(dir);
    }
    toast(app, _("Profile deleted."));
    populate_profiles(app);
}

static void free_gpointer_pair(gpointer data, GClosure *closure)
{
    (void)closure;
    gpointer *pair = data;
    g_free(pair[1]);
    g_free(pair);
}

/* One tile per saved profile: name, a one-line summary (DPI/Hz/effect),
 * and Apply/Delete. Not "the active one highlighted" -- there's no
 * concept of a currently-applied profile once you've hand-edited a
 * setting afterward, so this doesn't pretend to track one. */
static void populate_profiles(App *app)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(g_profiles_list)))
        gtk_box_remove(GTK_BOX(g_profiles_list), child);

    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return;

    GDir *gdir = g_dir_open(dir, 0, NULL);
    gboolean any = FALSE;
    if (gdir) {
        const char *entry;
        while ((entry = g_dir_read_name(gdir))) {
            if (!g_str_has_suffix(entry, ".json"))
                continue;
            Profile p;
            if (!profile_load(entry, &p))
                continue;
            any = TRUE;

            GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_widget_add_css_class(tile, "ajazz-card");
            gtk_widget_set_size_request(tile, 200, -1);

            GtkWidget *name_lbl = gtk_label_new(p.name[0] ? p.name : entry);
            gtk_widget_add_css_class(name_lbl, "heading");
            gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(tile), name_lbl);

            static const char *effect_names[] = {
                N_("Off"), N_("Rainbow"), N_("Constant"), N_("Breathing"), N_("Colour cycle"),
            };
            const char *effect_name = (p.lighting_effect >= 0 && p.lighting_effect < (int)G_N_ELEMENTS(effect_names))
                ? gettext(effect_names[p.lighting_effect]) : "?";
            char summary[96];
            snprintf(summary, sizeof(summary), _("%u DPI \xC2\xB7 %d Hz \xC2\xB7 %s"),
                p.dpi[p.dpi_selected < AJAZZ_DPI_STAGES ? p.dpi_selected : 0], p.rate_hz, effect_name);
            GtkWidget *sum_lbl = gtk_label_new(summary);
            gtk_widget_add_css_class(sum_lbl, "dim-label");
            gtk_widget_add_css_class(sum_lbl, "caption");
            gtk_widget_set_halign(sum_lbl, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(tile), sum_lbl);

            GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_margin_top(btn_row, 4);

            GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply"));
            gtk_widget_add_css_class(apply_btn, "suggested-action");
            gtk_widget_add_css_class(apply_btn, "pill");
            gtk_widget_set_hexpand(apply_btn, TRUE);
            gpointer *apply_data = g_new(gpointer, 2);
            apply_data[0] = app;
            apply_data[1] = g_strdup(entry);
            g_signal_connect_data(apply_btn, "clicked", G_CALLBACK(on_apply_profile_clicked), apply_data, free_gpointer_pair, 0);
            gtk_box_append(GTK_BOX(btn_row), apply_btn);

            GtkWidget *delete_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(delete_btn, "destructive-action");
            gtk_widget_add_css_class(delete_btn, "circular");
            gpointer *delete_data = g_new(gpointer, 2);
            delete_data[0] = app;
            delete_data[1] = g_strdup(entry);
            g_signal_connect_data(delete_btn, "clicked", G_CALLBACK(on_delete_profile_clicked), delete_data, free_gpointer_pair, 0);
            gtk_box_append(GTK_BOX(btn_row), delete_btn);

            gtk_box_append(GTK_BOX(tile), btn_row);
            gtk_box_append(GTK_BOX(g_profiles_list), tile);
        }
        g_dir_close(gdir);
    }
    g_free(dir);

    if (!any) {
        GtkWidget *empty = gtk_label_new(_("No saved profiles yet."));
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_box_append(GTK_BOX(g_profiles_list), empty);
    }
}

void refresh_profiles(App *app)
{
    populate_profiles(app);
}

/* AdwEntryRow inside a plain AdwAlertDialog is the simplest "ask for one
 * string" prompt libadwaita has without pulling in a bespoke dialog --
 * same pattern as the factory-reset confirmation elsewhere in this app,
 * just with a text entry added as the dialog's extra child. */
static GtkWidget *g_save_name_entry;
static void on_save_profile_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    App *app = user_data;
    if (strcmp(response, "save") != 0)
        return;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(g_save_name_entry));
    if (!name || !*name) {
        toast(app, _("Enter a profile name."));
        return;
    }
    Profile p;
    if (!profile_capture_current(app, name, &p)) {
        toast(app, _("Connect the mouse first."));
        return;
    }
    if (profile_save(&p)) {
        toast(app, _("Profile saved."));
        populate_profiles(app);
    } else {
        toast(app, _("Failed to save the profile."));
    }
    (void)dialog;
}

static void on_save_profile_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Save current settings as a profile"),
        _("Captures DPI, DPI colours, report rate, the current lighting effect and button mapping. Macros aren't included -- see the Macro page.")));
    g_save_name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_save_name_entry), _("Profile name"));
    adw_alert_dialog_set_extra_child(dialog, g_save_name_entry);
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "save", _("Save"));
    adw_alert_dialog_set_response_appearance(dialog, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "save");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_save_profile_dialog_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

GtkWidget *build_profiles_page(App *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 16);
    gtk_widget_set_margin_start(outer, 16);
    gtk_widget_set_margin_end(outer, 16);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(_("Profiles"));
    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *subtitle = gtk_label_new(_("Saved on this computer."));
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header_text), title);
    gtk_box_append(GTK_BOX(header_text), subtitle);
    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header), header_text);

    GtkWidget *save_btn = gtk_button_new_with_label(_("Save current as profile"));
    gtk_widget_add_css_class(save_btn, "suggested-action");
    gtk_widget_add_css_class(save_btn, "pill");
    gtk_widget_set_valign(save_btn, GTK_ALIGN_START);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_profile_clicked), app);
    gtk_box_append(GTK_BOX(header), save_btn);
    gtk_box_append(GTK_BOX(outer), header);

    /* A plain horizontal box in a horizontally-scrolling window -- fine
     * for a handful of profiles; wrapping onto multiple rows (GtkFlowBox)
     * would be nicer for many, but isn't worth the extra layout code for
     * what's expected to be a short list. */
    g_profiles_list = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(g_profiles_list, TRUE);
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), g_profiles_list);
    gtk_box_append(GTK_BOX(outer), scroller);

    populate_profiles(app);
    return outer;
}
