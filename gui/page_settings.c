/*
 * App-level settings: things about how Ajazzy itself behaves on this
 * PC, not the mouse. Persisted to ~/.config/ajazzy/settings.json via
 * jsonutil.h. Kept to a single flat page (no tabs) -- there are only a
 * handful of real, working options here; a multi-tab "General / Device
 * / Updates / Backup" layout would just be empty chrome around them.
 */

#define _GNU_SOURCE
#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"

#define AJAZZY_VERSION "0.1.4"
#define AJAZZY_GITHUB_URL "https://github.com/Geovane2dd/Ajazzy"

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
    const char *home = g_get_home_dir();
    return g_build_filename(home, ".config", "autostart", "io.github.ajazzy.Gui.desktop", NULL);
}

static void settings_save(App *app)
{
    char *path = settings_file_path();
    if (!path) return;

    GString *out = g_string_new("{\n  ");
    json_put_int(out, "theme", app->settings_theme); g_string_append(out, ",\n  ");
    json_put_int(out, "autostart", app->settings_autostart ? 1 : 0); g_string_append(out, ",\n  ");
    json_put_int(out, "low_battery_warning", app->settings_low_battery_warning ? 1 : 0);
    g_string_append(out, "\n}\n");
    json_write_file(path, out->str);
    g_string_free(out, TRUE);
    g_free(path);
}

/* Applies the theme choice to the whole app immediately -- AdwStyleManager
 * is process-global, so this re-themes every open window, not just the
 * settings page. */
static void apply_theme(int theme)
{
    /* Ajazzy's own CSS (see gui/main.c's CSS constant) paints every one
     * of its own widgets from a fixed dark/red palette regardless of
     * this setting -- it's a branded look, not an adaptive one. This
     * still has a real, visible effect on the handful of stock
     * GTK/libadwaita chrome our CSS doesn't touch (colour picker
     * dialogs, alert dialogs' default styling), so it's a genuine
     * option, just a narrower one than "Theme" usually implies. Default
     * ("Follow system") keeps the dark-forced look this app always had
     * before this setting existed, rather than actually following the
     * desktop's light/dark preference. */
    AdwStyleManager *mgr = adw_style_manager_get_default();
    switch (theme) {
        case 1: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_FORCE_LIGHT); break;
        case 2: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_FORCE_DARK); break;
        default: adw_style_manager_set_color_scheme(mgr, ADW_COLOR_SCHEME_PREFER_DARK); break;
    }
}

/* Writes (or removes) ~/.config/autostart/io.github.ajazzy.Gui.desktop.
 * The launcher's Exec line points at whatever binary is currently
 * running (via /proc/self/exe), not a hardcoded install path -- this
 * app is often run straight from a build directory during development,
 * and hardcoding e.g. /usr/bin/ajazzy-gui would silently do nothing for
 * that case. */
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
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Ajazzy\n"
        "Comment=Configure AJAZZ gaming mice on Linux\n"
        "Exec=%s\n"
        "Icon=io.github.ajazzy.Gui\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "NoDisplay=false\n",
        exec);
    ok = g_file_set_contents(path, contents, -1, NULL);
    g_free(contents);
    g_free(path);
    return ok;
}

void settings_load_and_apply(App *app)
{
    app->settings_theme = 0;
    app->settings_autostart = FALSE;
    app->settings_low_battery_warning = TRUE;
    app->settings_low_battery_was_ok = TRUE;

    char *path = settings_file_path();
    if (path) {
        char *text = json_read_file(path);
        if (text) {
            app->settings_theme = json_get_int(text, "theme", 0);
            app->settings_low_battery_warning = json_get_int(text, "low_battery_warning", 1) != 0;
            g_free(text);
        }
        g_free(path);
    }

    /* autostart's source of truth is the desktop file itself, not the
     * saved json -- if the user (or a stale install) removed/added it
     * by hand outside the app, this reflects reality rather than a
     * possibly-stale cached flag. */
    char *auto_path = autostart_desktop_path();
    app->settings_autostart = g_file_test(auto_path, G_FILE_TEST_EXISTS);
    g_free(auto_path);

    apply_theme(app->settings_theme);
}

void settings_check_low_battery(App *app, int percent, gboolean charging)
{
    if (!app->settings_low_battery_warning || percent < 0) {
        app->settings_low_battery_was_ok = TRUE;
        return;
    }
    gboolean low_now = (percent < 20 && !charging);
    if (low_now && app->settings_low_battery_was_ok) {
        char msg[128];
        snprintf(msg, sizeof(msg), _("Mouse battery low: %d%%"), percent);
        toast(app, msg);
    }
    app->settings_low_battery_was_ok = !low_now;
}

static void on_theme_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    app->settings_theme = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(dd));
    apply_theme(app->settings_theme);
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
        toast(app, _("Failed to update the autostart entry."));
        g_signal_handlers_block_by_func(sw, on_autostart_changed, user_data);
        adw_switch_row_set_active(ADW_SWITCH_ROW(sw), app->settings_autostart);
        g_signal_handlers_unblock_by_func(sw, on_autostart_changed, user_data);
    }
}

static void on_low_battery_changed(GObject *sw, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    App *app = user_data;
    app->settings_low_battery_warning = adw_switch_row_get_active(ADW_SWITCH_ROW(sw));
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
        char *cmd_dir = g_shell_quote(dir);
        char *cmd = g_strdup_printf("rm -rf %s", cmd_dir);
        /* Local config directory this app itself created and fully
         * owns (~/.config/ajazzy) -- safe to wipe outright, same as
         * "Reset Application Data" in any other app's settings. */
        g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
        g_free(cmd);
        g_free(cmd_dir);
        g_free(dir);
    }
    toast(app, _("Local app data cleared. Restart Ajazzy to see the effect."));
}

static void on_github_row_activated(AdwActionRow *row, gpointer user_data)
{
    (void)row;
    App *app = user_data;
    GtkUriLauncher *launcher = gtk_uri_launcher_new(AJAZZY_GITHUB_URL);
    gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->win), NULL, NULL, NULL);
    g_object_unref(launcher);
}

static void on_reset_data_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Reset application data?"),
        _("Deletes every saved profile and lighting preset, and this settings page's choices. Does not touch the mouse itself.")));
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "reset", _("Reset"));
    adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_reset_data_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

GtkWidget *build_settings_page(App *app)
{
    GtkWidget *page = adw_preferences_page_new();

    AdwPreferencesGroup *appearance = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(appearance, _("Appearance"));

    AdwComboRow *theme_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme_row), _("Theme"));
    const char *theme_names[] = {N_("Follow system"), N_("Light"), N_("Dark"), NULL};
    const char *theme_names_tr[4] = {gettext(theme_names[0]), gettext(theme_names[1]), gettext(theme_names[2]), NULL};
    adw_combo_row_set_model(theme_row, G_LIST_MODEL(gtk_string_list_new(theme_names_tr)));
    adw_combo_row_set_selected(theme_row, (guint)app->settings_theme);
    g_signal_connect(theme_row, "notify::selected", G_CALLBACK(on_theme_changed), app);
    adw_preferences_group_add(appearance, GTK_WIDGET(theme_row));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), appearance);

    AdwPreferencesGroup *behavior = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(behavior, _("Behaviour"));

    GtkWidget *autostart_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(autostart_row), _("Start on login"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(autostart_row), _("Launches Ajazzy minimised to the tray when you log in."));
    adw_switch_row_set_active(ADW_SWITCH_ROW(autostart_row), app->settings_autostart);
    g_signal_connect(autostart_row, "notify::active", G_CALLBACK(on_autostart_changed), app);
    adw_preferences_group_add(behavior, autostart_row);

    GtkWidget *low_batt_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(low_batt_row), _("Low battery warning"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(low_batt_row), _("Shows a one-time notification when the mouse drops below 20% and isn't charging."));
    adw_switch_row_set_active(ADW_SWITCH_ROW(low_batt_row), app->settings_low_battery_warning);
    g_signal_connect(low_batt_row, "notify::active", G_CALLBACK(on_low_battery_changed), app);
    adw_preferences_group_add(behavior, low_batt_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), behavior);

    AdwPreferencesGroup *data_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(data_group, _("Local data"));
    adw_preferences_group_set_description(data_group,
        _("Profiles and lighting presets are stored on this PC (~/.config/ajazzy), not on the mouse."));

    GtkWidget *reset_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(reset_row), _("Reset application data"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(reset_row), _("Deletes all saved profiles and lighting presets."));
    GtkWidget *reset_btn = gtk_button_new_with_label(_("Reset"));
    gtk_widget_add_css_class(reset_btn, "destructive-action");
    gtk_widget_set_valign(reset_btn, GTK_ALIGN_CENTER);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_data_clicked), app);
    adw_action_row_add_suffix(ADW_ACTION_ROW(reset_row), reset_btn);
    adw_preferences_group_add(data_group, reset_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), data_group);

    AdwPreferencesGroup *about = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(about, _("About"));

    GtkWidget *version_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(version_row), _("Version"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(version_row), AJAZZY_VERSION);
    adw_preferences_group_add(about, version_row);

    GtkWidget *github_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(github_row), _("Source code and releases"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(github_row), AJAZZY_GITHUB_URL);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(github_row), TRUE);
    GtkWidget *link_icon = gtk_image_new_from_icon_name("adw-external-link-symbolic");
    adw_action_row_add_suffix(ADW_ACTION_ROW(github_row), link_icon);
    g_signal_connect(github_row, "activated", G_CALLBACK(on_github_row_activated), app);
    adw_preferences_group_add(about, github_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), about);

    return page;
}
