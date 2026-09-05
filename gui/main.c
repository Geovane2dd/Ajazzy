/*
 * Ajazzy GUI, a GTK4/libadwaita front-end for ajazzyctl.
 *
 * Built from native libadwaita widgets (AdwNavigationSplitView,
 * AdwPreferencesGroup/Row) rather than a hand-painted theme, so it
 * follows the user's system light/dark preference and accent colour
 * instead of forcing a fixed palette -- the only custom CSS left is for
 * things libadwaita has no row type for (DPI stage swatches, the button
 * marker overlay on the mouse diagram).
 *
 * Every user-facing string goes through _() (gettext). English is the
 * source language; po/pt_BR.po has the Portuguese translation.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "../devices/device.h"

#ifndef LOCALEDIR
#define LOCALEDIR "locale"
#endif
#ifndef ICONDIR
#define ICONDIR "gui/icons"
#endif

#define AJAZZ_APP_ICON "io.github.ajazzy.Gui"

/*
 * Ajazzy has its own brand identity (matching the app icon's red/orange
 * mouse mark on near-black, see gui/icons/io.github.ajazzy.Gui.svg)
 * rather than just inheriting whatever accent colour the user's system
 * theme happens to be set to. Redefining accent_color/accent_bg_color/
 * accent_fg_color at APPLICATION priority re-themes every stock
 * libadwaita "suggested-action" button, switch, and selection highlight
 * to match -- this is the single highest-leverage thing for making the
 * app feel like a distinct product instead of a generic GNOME settings
 * panel, since those widgets are used throughout every page.
 */
static const char *CSS =
    "@define-color accent_color #f0564a;"
    "@define-color accent_bg_color #f0564a;"
    "@define-color accent_fg_color #ffffff;"
    "@define-color ajazz_bg_deep #0c0e10;"
    "@define-color ajazz_panel #15181b;"

    "window.background, .background { background-color: @ajazz_bg_deep; }"

    ".ajazz-dpi-btn { border-radius: 10px; padding: 12px; font-weight: 800; min-width: 76px;"
    "  box-shadow: 0 1px 3px rgba(0,0,0,0.4); transition: box-shadow 150ms ease; }"
    ".ajazz-dpi-btn.selected { outline: 2px solid #fff; outline-offset: 2px;"
    "  box-shadow: 0 0 0 3px alpha(@accent_color, 0.55), 0 0 18px 2px alpha(@accent_color, 0.55); }"
    /* matching the mouse's own confirmed per-DPI-stage indicator LED
     * colours (query 0x14, see docs/reverse-engineering.md): red, green,
     * blue, cyan, yellow, magenta -- not arbitrary decoration, this is
     * literally what the hardware lights up for each stage. */
    ".ajazz-dpi-1 { background-color: #ff3b3b; color: white; }"
    ".ajazz-dpi-2 { background-color: #2ecc40; color: black; }"
    ".ajazz-dpi-3 { background-color: #3b6bff; color: white; }"
    ".ajazz-dpi-4 { background-color: #00d1d1; color: black; }"
    ".ajazz-dpi-5 { background-color: #ffd400; color: black; }"
    ".ajazz-dpi-6 { background-color: #e838e8; color: white; }"

    ".ajazz-marker { min-width: 27px; min-height: 27px; border-radius: 14px;"
    "  font-size: 11px; font-weight: 800; padding: 0;"
    "  background-color: alpha(#000, 0.55); color: #fff; border: 1.5px solid alpha(#fff, 0.4); }"
    ".ajazz-marker.selected { background-color: @accent_bg_color; color: @accent_fg_color;"
    "  border-color: #fff; box-shadow: 0 0 0 3px alpha(@accent_bg_color, 0.4), 0 0 14px alpha(@accent_bg_color, 0.6); }"

    ".ajazz-sidebar-panel { background-color: @ajazz_panel;"
    "  background-image: linear-gradient(180deg, alpha(@accent_color, 0.10), transparent 220px);"
    "  border-right: 1px solid alpha(#fff, 0.06); }"
    ".ajazz-sidebar-header { padding: 18px 16px 18px 16px;"
    "  border-bottom: 1px solid alpha(@accent_color, 0.35); }"
    ".ajazz-sidebar-title { font-size: 18px; font-weight: 900; letter-spacing: 3px; color: #fff; }"
    ".ajazz-sidebar-subtitle { font-size: 10px; font-weight: 700; letter-spacing: 1px;"
    "  text-transform: uppercase; opacity: 0.5; }"
    ".navigation-sidebar row { border-left: 3px solid transparent; margin: 1px 6px; border-radius: 8px; }"
    ".navigation-sidebar row:selected { background-color: alpha(@accent_color, 0.16);"
    "  border-left: 3px solid @accent_color; }"
    ".navigation-sidebar row:selected label { font-weight: 700; }"

    ".ajazz-status-connected { color: #3fe07a; font-weight: 700; }"
    ".ajazz-status-disconnected { color: #ff5c5c; font-weight: 700; }"
    ".ajazz-status-dot { min-width: 8px; min-height: 8px; border-radius: 5px; margin: 0 2px; }"
    ".ajazz-status-dot.connected { background-color: #3fe07a; box-shadow: 0 0 6px #3fe07a; }"
    ".ajazz-status-dot.disconnected { background-color: #ff5c5c; }"

    "preferencesgroup > box > label.title { letter-spacing: 0.5px; font-weight: 800;"
    "  text-transform: uppercase; font-size: 12px; opacity: 0.7; }"
    ".ajazz-cta { font-weight: 800; padding-left: 24px; padding-right: 24px; }"
    "button.suggested-action.pill { box-shadow: 0 2px 14px alpha(@accent_color, 0.4); }"

    /* Home page hero */
    ".ajazz-hero { border-radius: 20px; padding: 22px 26px;"
    "  background-image: linear-gradient(135deg, alpha(@accent_color, 0.22), alpha(@accent_color, 0.03) 70%);"
    "  border: 1px solid alpha(@accent_color, 0.3); }"
    ".ajazz-hero-name { font-size: 24px; font-weight: 900; color: #fff; }"
    ".ajazz-hero-sub { font-size: 12px; font-weight: 600; letter-spacing: 1px; text-transform: uppercase; opacity: 0.55; }"
    ".ajazz-stat-label { font-size: 10px; font-weight: 700; letter-spacing: 0.8px; text-transform: uppercase; opacity: 0.5; }"
    ".ajazz-stat-value { font-size: 15px; font-weight: 800; }"

    /* battery bars */
    ".ajazz-batt-seg { min-width: 9px; min-height: 20px; border-radius: 2px;"
    "  background-color: alpha(#fff, 0.12); }"
    ".ajazz-batt-seg.lit { background-color: @accent_color; box-shadow: 0 0 8px alpha(@accent_color, 0.65); }"

    /* tested/untested badges */
    ".ajazz-badge { border-radius: 999px; padding: 3px 11px; font-size: 11px; font-weight: 800; letter-spacing: 0.3px; }"
    ".ajazz-badge-green { background-color: alpha(#3fe07a, 0.16); color: #3fe07a; border: 1px solid alpha(#3fe07a, 0.45); }"
    ".ajazz-badge-yellow { background-color: alpha(#ffc93f, 0.16); color: #ffc93f; border: 1px solid alpha(#ffc93f, 0.45); }";

GtkWidget *make_logo_widget(int size)
{
    GtkWidget *img = gtk_image_new_from_icon_name(AJAZZ_APP_ICON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), size);
    return img;
}
void free_closure_data(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}
static gboolean refresh_buttons_delayed_cb(gpointer user_data)
{
    refresh_buttons((App *)user_data);
    return G_SOURCE_REMOVE;
}

void refresh_buttons_delayed(App *app)
{
    g_timeout_add(250, refresh_buttons_delayed_cb, app);
}

static gboolean refresh_macro_status_delayed_cb(gpointer user_data)
{
    refresh_macro_status((App *)user_data);
    return G_SOURCE_REMOVE;
}

void refresh_macro_status_delayed(App *app)
{
    g_timeout_add(250, refresh_macro_status_delayed_cb, app);
}

void toast(App *app, const char *msg)
{
    AdwToast *t = adw_toast_new(msg);
    adw_toast_set_timeout(t, 3);
    adw_toast_overlay_add_toast(app->toast_overlay, t);
}

/* Sets both a status-dot widget's colour and (if given) an accompanying
 * label's text/colour in one place, since the sidebar footer and the Home
 * hero both show the same connection state and should never disagree. */
void set_status_widgets(GtkWidget *dot, GtkLabel *lbl, gboolean connected)
{
    if (dot) {
        gtk_widget_remove_css_class(dot, connected ? "disconnected" : "connected");
        gtk_widget_add_css_class(dot, connected ? "connected" : "disconnected");
    }
    if (lbl) {
        gtk_label_set_text(lbl, connected ? _("Connected") : _("Disconnected"));
        gtk_widget_remove_css_class(GTK_WIDGET(lbl), connected ? "ajazz-status-disconnected" : "ajazz-status-connected");
        gtk_widget_add_css_class(GTK_WIDGET(lbl), connected ? "ajazz-status-connected" : "ajazz-status-disconnected");
    }
}

/* Always called on the main thread (never from a page's fetch GThread --
 * those only ever check app->connected, they don't reconnect). Takes
 * hid_lock around the close/reopen so it can't reassign app->dev.fd out
 * from under a fetch thread that's mid-query on the old fd -- see the
 * comment on App::hid_lock. */
gboolean ensure_connected(App *app)
{
    if (app->connected) {
        /* app->connected only ever means "we successfully opened it at
         * some point" -- it doesn't notice an unplug in between, since
         * there's no hotplug/udev watch. A dead hidraw node (unplugged,
         * or replugged under a new /dev/hidrawN number) fails this
         * check, so we close the stale handle and fall through to
         * ajazz_open() to find the device again instead of getting
         * stuck reporting "connected" forever. */
        gboolean still_here = (access(app->dev.path, F_OK) == 0);

        /* Separately: even if our current node is still there, a
         * BETTER one may have appeared since we opened it -- plugging
         * in the mouse's USB cable while an existing "2.4G" connection
         * is open doesn't make the dongle's node disappear, so the
         * access() check above alone would keep us talking to the
         * dongle forever and never notice the cable (including for
         * dev.charging, which would then also never flip to true).
         * ajazz_best_path() is a cheap directory scan, safe to run on
         * every call. */
        char best_path[280];
        int charging = 0;
        gboolean better_available = FALSE;
        if (still_here && ajazz_best_path(best_path, sizeof(best_path), &charging)) {
            if (strcmp(best_path, app->dev.path) != 0)
                better_available = TRUE;
            else
                app->dev.charging = charging; /* same interface, charging state may have changed */
        }

        if (still_here && !better_available)
            return TRUE;

        g_mutex_lock(&app->hid_lock);
        ajazz_close(&app->dev);
        g_mutex_unlock(&app->hid_lock);
        app->connected = FALSE;
        set_status_widgets(app->status_dot, app->status_lbl, FALSE);
        set_status_widgets(app->hero_status_dot, app->hero_status_lbl, FALSE);
    }
    g_mutex_lock(&app->hid_lock);
    int rc = ajazz_open(&app->dev);
    g_mutex_unlock(&app->hid_lock);
    if (rc == 0) {
        app->connected = TRUE;
        set_status_widgets(app->status_dot, app->status_lbl, TRUE);
        return TRUE;
    }
    toast(app, _("No AJAZZ mouse found. Check the connection and permissions."));
    return FALSE;
}

/* ---------------- navigation / window chrome ---------------- */

typedef struct {
    const char *label;
    const char *icon;
    const char *page;
} nav_item_t;

/* Re-reads whichever page is named, from the mouse, right now -- shared by
 * both the "switched to a different page" and "clicked the page that was
 * already open" paths below, so every page is always showing live state
 * rather than whatever was on screen when you last looked at it. */
static void refresh_page(App *app, const char *page_name)
{
    if (strcmp(page_name, "home") == 0)
        refresh_home(app);
    else if (strcmp(page_name, "config") == 0)
        refresh_config(app);
    else if (strcmp(page_name, "buttons") == 0)
        refresh_buttons(app);
    else if (strcmp(page_name, "macro") == 0)
        refresh_macro_status(app);
    else if (strcmp(page_name, "lighting") == 0)
        refresh_lighting(app);
}

static void on_sidebar_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row)
        return;

    const char *page_name = g_object_get_data(G_OBJECT(row), "page");
    adw_view_stack_set_visible_child_name(app->stack, page_name);
    refresh_page(app, page_name);
}

static GtkWidget *build_sidebar(App *app, const nav_item_t *items, size_t n_items)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "ajazz-sidebar-panel");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(header, "ajazz-sidebar-header");
    gtk_box_append(GTK_BOX(header), make_logo_widget(34));

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new(_("Ajazzy"));
    gtk_widget_add_css_class(title, "ajazz-sidebar-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *subtitle = gtk_label_new(_("AJAZZ mouse configurator"));
    gtk_widget_add_css_class(subtitle, "ajazz-sidebar-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_box_append(GTK_BOX(header), title_box);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    app->sidebar_list = GTK_LIST_BOX(list);

    for (size_t i = 0; i < n_items; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_top(rowbox, 8);
        gtk_widget_set_margin_bottom(rowbox, 8);
        gtk_widget_set_margin_start(rowbox, 12);
        gtk_widget_set_margin_end(rowbox, 12);
        GtkWidget *icon = gtk_image_new_from_icon_name(items[i].icon);
        GtkWidget *lbl = gtk_label_new(gettext(items[i].label));
        gtk_box_append(GTK_BOX(rowbox), icon);
        gtk_box_append(GTK_BOX(rowbox), lbl);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowbox);
        g_object_set_data(G_OBJECT(row), "page", (gpointer)items[i].page);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    g_signal_connect(list, "row-selected", G_CALLBACK(on_sidebar_row_selected), app);
    /* row-selected only fires when the selection actually changes -- also
     * handle row-activated (fires on every click) so clicking the page
     * that's already open still re-reads the mouse instead of doing nothing. */
    g_signal_connect(list, "row-activated", G_CALLBACK(on_sidebar_row_selected), app);
    gtk_widget_set_vexpand(list, TRUE);
    gtk_box_append(GTK_BOX(box), list);

    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(status_box, 8);
    gtk_widget_set_margin_bottom(status_box, 12);
    gtk_widget_set_margin_start(status_box, 16);
    gtk_widget_set_halign(status_box, GTK_ALIGN_START);
    app->status_dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(app->status_dot, "ajazz-status-dot");
    gtk_widget_add_css_class(app->status_dot, "disconnected");
    gtk_widget_set_valign(app->status_dot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(status_box), app->status_dot);
    app->status_lbl = GTK_LABEL(gtk_label_new(_("Disconnected")));
    gtk_widget_add_css_class(GTK_WIDGET(app->status_lbl), "ajazz-status-disconnected");
    gtk_widget_add_css_class(GTK_WIDGET(app->status_lbl), "caption");
    gtk_box_append(GTK_BOX(status_box), GTK_WIDGET(app->status_lbl));
    gtk_box_append(GTK_BOX(box), status_box);

    gtk_list_box_select_row(GTK_LIST_BOX(list),
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0));

    return box;
}

/* Tray callbacks -- see gui/tray.h for why this exists instead of a
 * library-based status icon. "Quit" is the only way to actually end the
 * app once a tray icon is present; closing the window just hides it
 * (see on_window_close_request()), Steam-style, so the mouse stays
 * configurable/watchable from the tray without a window open. */
static void on_tray_activate(gpointer user_data)
{
    App *app = user_data;
    gtk_widget_set_visible(GTK_WIDGET(app->win), TRUE);
    gtk_window_present(GTK_WINDOW(app->win));
}

static void on_tray_refresh(gpointer user_data)
{
    refresh_home((App *)user_data);
}

/* Runs every few seconds so an unplug/replug (or a dongle drop/re-pair)
 * is picked up on its own -- see ensure_connected()'s access() check --
 * instead of only refreshing when the user happens to click something.
 * Keeps the tray icon/status honest even while the window is hidden. */
static gboolean periodic_refresh_cb(gpointer user_data)
{
    refresh_home((App *)user_data);
    return G_SOURCE_CONTINUE;
}

static void on_tray_quit(gpointer user_data)
{
    App *app = user_data;
    g_application_quit(G_APPLICATION(app->gtk_app));
}

static gboolean on_window_close_request(GtkWindow *win, gpointer user_data)
{
    (void)win; (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
    return TRUE; /* handled -- don't destroy the window or quit the app */
}

static void on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    App *app = user_data;
    app->gtk_app = gtk_app;
    /* keeps the GApplication alive with no visible windows, since closing
     * to the tray (see on_window_close_request()) would otherwise quit it */
    g_application_hold(G_APPLICATION(gtk_app));

    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_PREFER_DARK);

    /* Lets gtk_image_new_from_icon_name(AJAZZ_APP_ICON) and the window icon
     * resolve even when running straight from the build directory, not just
     * when installed under $(PREFIX)/share/icons/hicolor/... */
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), ICONDIR);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    app->win = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(gtk_app)));
    gtk_window_set_title(GTK_WINDOW(app->win), _("Ajazzy"));
    gtk_window_set_icon_name(GTK_WINDOW(app->win), AJAZZ_APP_ICON);
    gtk_window_set_default_size(GTK_WINDOW(app->win), 960, 720);
    g_signal_connect(app->win, "close-request", G_CALLBACK(on_window_close_request), app);

    /* created before build_sidebar() below, since selecting its initial
     * row synchronously fires a refresh that can call toast() */
    app->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());

    app->stack = ADW_VIEW_STACK(adw_view_stack_new());
    adw_view_stack_add_named(app->stack, build_home_page(app), "home");
    adw_view_stack_add_named(app->stack, build_buttons_page(app), "buttons");
    adw_view_stack_add_named(app->stack, build_mouse_config_page(app), "config");
    adw_view_stack_add_named(app->stack, build_lighting_page(app), "lighting");
    adw_view_stack_add_named(app->stack, build_macro_page(app), "macro");

    static const nav_item_t items[] = {
        {N_("Home"), "go-home-symbolic", "home"},
        {N_("Buttons"), "input-mouse-symbolic", "buttons"},
        {N_("Mouse settings"), "preferences-system-symbolic", "config"},
        {N_("Lighting"), "weather-clear-symbolic", "lighting"},
        {N_("Macro"), "input-keyboard-symbolic", "macro"},
    };
    GtkWidget *sidebar_content = build_sidebar(app, items, G_N_ELEMENTS(items));

    AdwToolbarView *sidebar_toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_set_content(sidebar_toolbar, sidebar_content);
    AdwNavigationPage *sidebar_page = ADW_NAVIGATION_PAGE(
        adw_navigation_page_new(GTK_WIDGET(sidebar_toolbar), _("Ajazzy")));

    AdwHeaderBar *content_header = ADW_HEADER_BAR(adw_header_bar_new());
    adw_header_bar_set_show_title(content_header, TRUE);

    AdwToolbarView *content_toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(content_toolbar, GTK_WIDGET(content_header));
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), GTK_WIDGET(app->stack));
    adw_toolbar_view_set_content(content_toolbar, scroller);
    AdwNavigationPage *content_page = ADW_NAVIGATION_PAGE(
        adw_navigation_page_new(GTK_WIDGET(content_toolbar), _("Ajazzy")));

    AdwNavigationSplitView *split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    adw_navigation_split_view_set_sidebar(split, sidebar_page);
    adw_navigation_split_view_set_content(split, content_page);
    adw_navigation_split_view_set_min_sidebar_width(split, 220);
    adw_navigation_split_view_set_max_sidebar_width(split, 260);

    adw_toast_overlay_set_child(app->toast_overlay, GTK_WIDGET(split));

    adw_application_window_set_content(app->win, GTK_WIDGET(app->toast_overlay));
    gtk_window_present(GTK_WINDOW(app->win));

    AjazzTrayCallbacks tray_cb = {
        .on_activate = on_tray_activate,
        .on_refresh = on_tray_refresh,
        .on_quit = on_tray_quit,
        .user_data = app,
    };
    /* Starts on the "missing" battery icon since we haven't talked to the
     * mouse yet; refresh_home() swaps it for the real charge level as
     * soon as it reads one. Uses a standard battery-*-symbolic name, not
     * our own app icon -- the shell/AppIndicator extension renders this
     * in its own process and won't see our ICONDIR search path unless
     * we're actually installed under a standard icon directory. */
    app->tray = ajazz_tray_new("battery-missing-symbolic", _("Ajazzy"), &tray_cb);

    refresh_home(app);
    g_timeout_add_seconds(2, periodic_refresh_cb, app);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("ajazzy", LOCALEDIR);
    bind_textdomain_codeset("ajazzy", "UTF-8");
    textdomain("ajazzy");

    App app = {0};
    app.dev.fd = -1;
    app.dpi_selected = 0;
    init_key_functions();
    init_macro_actions();

    AdwApplication *gtk_app = adw_application_new("io.github.ajazzy.Gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &app);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    ajazz_tray_free(app.tray);
    if (app.connected)
        ajazz_close(&app.dev);

    g_object_unref(gtk_app);
    return status;
}
