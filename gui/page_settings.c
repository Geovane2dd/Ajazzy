/*
 * Settings: how Ajazzy itself behaves on this PC. Nothing here is sent to
 * the mouse. Persisted to ~/.config/ajazzy/settings.json.
 */

#define _GNU_SOURCE
#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"

static char *settings_file_path(void)
{
    char *dir = ajazzy_config_subdir(".");
    if (!dir) return NULL;
    char *path = g_build_filename(dir, "settings.json", NULL);
    g_free(dir);
    return path;
}

static char *autostart_desktop_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "autostart", "io.github.ajazzy.Gui.desktop", NULL);
}

void settings_save(App *app)
{
    char *path = settings_file_path();
    if (!path) return;

    GString *out = g_string_new("{\n  ");
    json_put_int(out, "theme", app->settings_theme); g_string_append(out, ",\n  ");
    json_put_int(out, "start_minimized", app->settings_start_minimized); g_string_append(out, ",\n  ");
    json_put_int(out, "close_to_tray", app->settings_close_to_tray); g_string_append(out, ",\n  ");
    json_put_int(out, "low_battery_warning", app->settings_low_battery_warning); g_string_append(out, ",\n  ");
    json_put_int(out, "low_battery_threshold", app->settings_low_battery_threshold); g_string_append(out, ",\n  ");
    json_put_int(out, "profile_on_start", app->settings_profile_on_start); g_string_append(out, ",\n  ");
    json_put_int(out, "auto_saver", app->settings_auto_saver); g_string_append(out, ",\n  ");
    json_put_string(out, "last_profile", app->settings_last_profile);
    g_string_append(out, "\n}\n");
    json_write_file(path, out->str);
    g_string_free(out, TRUE);
    g_free(path);
}

static void apply_theme(int theme)
{
    AdwStyleManager *mgr = adw_style_manager_get_default();
    switch (theme) {
        case 1: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_FORCE_LIGHT); break;
        case 2: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_FORCE_DARK); break;
        default: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_DEFAULT); break;
    }
}

/* The launcher's Exec line points at whatever binary is running right now
 * (/proc/self/exe), so autostart also works from a build directory. */
static gboolean set_autostart(gboolean enabled)
{
    char *path = autostart_desktop_path();
    gboolean ok = TRUE;

    if (!enabled) {
        if (g_file_test(path, G_FILE_TEST_EXISTS))
            ok = (g_unlink(path) == 0);
        g_free(path);
        return ok;
    }

    char exe_path[4096];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    const char *exec = "ajazzy-gui";
    if (n > 0) { exe_path[n] = '\0'; exec = exe_path; }

    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    char *contents = g_strdup_printf(
        "[Desktop Entry]\nType=Application\nName=Ajazzy\n"
        "Comment=Configure AJAZZ gaming mice on Linux\n"
        "Exec=%s\nIcon=io.github.ajazzy.Gui\nTerminal=false\n"
        "X-GNOME-Autostart-enabled=true\nNoDisplay=false\n", exec);
    ok = g_file_set_contents(path, contents, -1, NULL);
    g_free(contents);
    g_free(path);
    return ok;
}

void settings_load_and_apply(App *app)
{
    app->settings_theme = 2;
    app->settings_start_minimized = FALSE;
    app->settings_close_to_tray = TRUE;
    app->settings_low_battery_warning = TRUE;
    app->settings_low_battery_threshold = 20;
    app->settings_profile_on_start = FALSE;
    app->settings_low_battery_was_ok = TRUE;
    app->settings_last_profile[0] = '\0';

    char *path = settings_file_path();
    if (path) {
        char *text = json_read_file(path);
        if (text) {
            app->settings_theme = json_get_int(text, "theme", 2);
            app->settings_start_minimized = json_get_int(text, "start_minimized", 0) != 0;
            app->settings_close_to_tray = json_get_int(text, "close_to_tray", 1) != 0;
            app->settings_low_battery_warning = json_get_int(text, "low_battery_warning", 1) != 0;
            app->settings_low_battery_threshold = json_get_int(text, "low_battery_threshold", 20);
            app->settings_profile_on_start = json_get_int(text, "profile_on_start", 0) != 0;
            app->settings_auto_saver = json_get_int(text, "auto_saver", 0) != 0;
            json_get_string(text, "last_profile", app->settings_last_profile, sizeof(app->settings_last_profile));
            g_free(text);
        }
        g_free(path);
    }
    if (app->settings_low_battery_threshold < 5 || app->settings_low_battery_threshold > 50)
        app->settings_low_battery_threshold = 20;

    /* the autostart file itself is the source of truth */
    char *auto_path = autostart_desktop_path();
    app->settings_autostart = g_file_test(auto_path, G_FILE_TEST_EXISTS);
    g_free(auto_path);

    apply_theme(app->settings_theme);
}

void settings_check_low_battery(App *app, int percent, gboolean charging)
{
    powersaver_battery_update(app, percent, charging);
    if (!app->settings_low_battery_warning || percent < 0) {
        app->settings_low_battery_was_ok = TRUE;
        return;
    }
    gboolean low_now = (percent < app->settings_low_battery_threshold && !charging);
    if (low_now && app->settings_low_battery_was_ok && !app->settings_auto_saver)
        toastf(app, _("Mouse battery low: %d%%"), percent);
    app->settings_low_battery_was_ok = !low_now;
}

/* ---------------- handlers ---------------- */

static void on_theme_selected(int index, gpointer user_data)
{
    App *app = user_data;
    app->settings_theme = index;
    apply_theme(index);
    settings_save(app);
}

static void on_autostart_changed(GObject *sw, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    gboolean enabled = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));
    if (set_autostart(enabled)) {
        app->settings_autostart = enabled;
    } else {
        toast(app, _("Couldn't update the startup entry."));
        g_signal_handlers_block_by_func(sw, on_autostart_changed, user_data);
        adw_switch_row_set_active(ADW_SWITCH_ROW(sw), app->settings_autostart);
        g_signal_handlers_unblock_by_func(sw, on_autostart_changed, user_data);
    }
}

#define SWITCH_HANDLER(name, field)                                              \
    static void name(GObject *sw, GParamSpec *pspec, gpointer user_data)          \
    {                                                                             \
        (void)pspec;                                                              \
        App *app = user_data;                                                     \
        app->field = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));               \
        settings_save(app);                                                       \
    }
SWITCH_HANDLER(on_minimized_changed, settings_start_minimized)
SWITCH_HANDLER(on_close_tray_changed, settings_close_to_tray)
SWITCH_HANDLER(on_low_batt_changed, settings_low_battery_warning)
SWITCH_HANDLER(on_profile_start_changed, settings_profile_on_start)

static void on_threshold_changed(GObject *sp, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    app->settings_low_battery_threshold = (int)adw_spin_row_get_value(ADW_SPIN_ROW(sp));
    settings_save(app);
}

static void on_reset_data_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    App *app = user_data;
    if (strcmp(response, "reset") != 0)
        return;
    char *dir = ajazzy_config_subdir(".");
    if (dir) {
        /* the directory is created and owned by this app */
        GDir *d = g_dir_open(dir, 0, NULL);
        if (d) {
            const char *n;
            while ((n = g_dir_read_name(d))) {
                char *p = g_build_filename(dir, n, NULL);
                if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
                    GDir *sub = g_dir_open(p, 0, NULL);
                    const char *sn;
                    while (sub && (sn = g_dir_read_name(sub))) {
                        char *sp = g_build_filename(p, sn, NULL);
                        g_remove(sp);
                        g_free(sp);
                    }
                    if (sub) g_dir_close(sub);
                }
                g_remove(p);
                g_free(p);
            }
            g_dir_close(d);
        }
        g_free(dir);
    }
    if (app->macro_lib)
        g_ptr_array_set_size(app->macro_lib, 0);
    memset(app->macro_slot_name, 0, sizeof(app->macro_slot_name));
    app->settings_last_profile[0] = '\0';
    refresh_profiles(app);
    toast(app, _("Local data cleared."));
}

static void on_reset_data_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Delete all local data?"),
        _("Removes every saved profile, macro and lighting preset, and resets these settings. "
          "The mouse itself is not touched.")));
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "reset", _("Delete everything"));
    adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_reset_data_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

static void on_open_url(GtkListBoxRow *row, gpointer user_data)
{
    (void)row;
    App *app = user_data;
    GtkUriLauncher *launcher = gtk_uri_launcher_new(AJAZZY_GITHUB_URL);
    gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->win), NULL, NULL, NULL);
    g_object_unref(launcher);
}

static void on_copy_diagnostics(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "Ajazzy %s\n", AJAZZY_VERSION);
    if (app->connected) {
        g_string_append_printf(s, "Device: %s\nVID:PID: %04X:%04X\ndev_id: %s\nInterface: %s (%s)\n",
            app->dev.name, app->dev.vid, app->dev.pid, app->dev.dev_id[0] ? app->dev.dev_id : "?",
            app->dev.mode[0] ? app->dev.mode : "?", app->dev.path);
        g_string_append_printf(s, "Battery: %d%%%s\n", app->battery_percent, app->charging ? " (charging)" : "");
    } else {
        g_string_append(s, "Device: not connected\n");
    }
    gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()), s->str);
    g_string_free(s, TRUE);
    toast(app, _("Diagnostics copied to the clipboard."));
}

GtkWidget *build_settings_page(App *app)
{
    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Settings"), _("How Ajazzy behaves on this computer."), NULL, &body);

    /* appearance */
    GtkWidget *card_body;
    GtkWidget *card = ui_card_new(_("Appearance"), NULL, &card_body);
    GtkWidget *list = ui_list_new();
    const char *themes[] = {_("System"), _("Light"), _("Dark")};
    GtkWidget *seg = ui_segmented_new(themes, 3, app->settings_theme, on_theme_selected, app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Theme"), _("Follow the system or force light/dark."), seg)));
    gtk_box_append(GTK_BOX(card_body), list);
    gtk_box_append(GTK_BOX(body), card);

    /* behaviour */
    card = ui_card_new(_("Behaviour"), NULL, &card_body);
    list = ui_list_new();
    struct { const char *title, *sub; gboolean value; GCallback cb; } sw[] = {
        {_("Start with the computer"), _("Launch Ajazzy when you log in."), app->settings_autostart, G_CALLBACK(on_autostart_changed)},
        {_("Start minimised to the tray"), _("Open in the background instead of showing the window."), app->settings_start_minimized, G_CALLBACK(on_minimized_changed)},
        {_("Keep running in the tray when closed"), _("Closing the window hides it; the tray icon's Quit exits."), app->settings_close_to_tray, G_CALLBACK(on_close_tray_changed)},
        {_("Re-apply my last profile on connect"), _("Restores the last profile you applied whenever the mouse (re)connects."), app->settings_profile_on_start, G_CALLBACK(on_profile_start_changed)},
    };
    for (guint i = 0; i < G_N_ELEMENTS(sw); i++) {
        GtkWidget *row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), sw[i].title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sw[i].sub);
        adw_switch_row_set_active(ADW_SWITCH_ROW(row), sw[i].value);
        g_signal_connect(row, "notify::active", sw[i].cb, app);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    gtk_box_append(GTK_BOX(card_body), list);
    gtk_box_append(GTK_BOX(body), card);

    /* notifications */
    card = ui_card_new(_("Notifications"), NULL, &card_body);
    list = ui_list_new();
    GtkWidget *lb = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lb), _("Low battery warning"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(lb), _("Notify once when the battery drops below the level below."));
    adw_switch_row_set_active(ADW_SWITCH_ROW(lb), app->settings_low_battery_warning);
    g_signal_connect(lb, "notify::active", G_CALLBACK(on_low_batt_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), lb);
    GtkWidget *th = adw_spin_row_new_with_range(5, 50, 5);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(th), _("Warn below (%)"));
    adw_spin_row_set_value(ADW_SPIN_ROW(th), app->settings_low_battery_threshold);
    g_signal_connect(th, "notify::value", G_CALLBACK(on_threshold_changed), app);
    gtk_list_box_append(GTK_LIST_BOX(list), th);
    gtk_box_append(GTK_BOX(card_body), list);
    gtk_box_append(GTK_BOX(body), card);

    /* support */
    card = ui_card_new(_("Support"), NULL, &card_body);
    list = ui_list_new();
    GtkWidget *copy = ui_button_new(_("Copy"), "edit-copy-symbolic", "secondary");
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_diagnostics), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Copy diagnostics"),
        _("Device, interface and battery details for bug reports."), copy)));
    GtkWidget *reset = ui_button_new(_("Delete\xE2\x80\xA6"), "user-trash-symbolic", "danger");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_data_clicked), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Delete local data"),
        _("Profiles, macros and presets are stored in ~/.config/ajazzy."), reset)));
    gtk_box_append(GTK_BOX(card_body), list);
    gtk_box_append(GTK_BOX(body), card);

    /* about */
    card = ui_card_new(_("About"), NULL, &card_body);
    list = ui_list_new();
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Version"), AJAZZY_VERSION, NULL)));
    AdwActionRow *link = ui_row_new(_("Source code and releases"), AJAZZY_GITHUB_URL,
        gtk_image_new_from_icon_name("adw-external-link-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(link), TRUE);
    g_signal_connect(link, "activated", G_CALLBACK(on_open_url), app);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(link));
    gtk_box_append(GTK_BOX(card_body), list);
    gtk_box_append(GTK_BOX(body), card);

    return page;
}
