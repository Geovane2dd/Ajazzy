#ifndef AJAZZY_TRAY_H
#define AJAZZY_TRAY_H

#include <gio/gio.h>

/*
 * A minimal system tray / status icon, implemented by hand against the
 * freedesktop StatusNotifierItem + com.canonical.dbusmenu D-Bus
 * protocols (the ones KDE, and GNOME via the "AppIndicator and
 * KStatusNotifierItem Support" shell extension, actually watch for --
 * GTK4 dropped the old GtkStatusIcon API and libadwaita has no
 * replacement for it).
 *
 * Implemented from scratch rather than linking libayatana-appindicator3
 * because: (a) that library isn't installed on this system and isn't a
 * normal dependency for a GTK4 app, and (b) it's built against GTK3 (it
 * hands you a GtkMenu), and mixing GTK3 and GTK4 in one process is
 * fragile. GDBus (part of GIO, already linked for GTK/libadwaita) is
 * enough to speak both protocols directly with no extra dependency.
 *
 * The menu is intentionally flat (no submenus) -- just enough for a
 * Steam-tray-icon-style quick menu: open the window, see battery at a
 * glance, refresh, quit.
 */

typedef struct AjazzTray AjazzTray;

typedef struct {
    void (*on_activate)(gpointer user_data); /* left-click / double-click */
    void (*on_refresh)(gpointer user_data);  /* "Refresh now" menu item   */
    void (*on_quit)(gpointer user_data);     /* "Quit" menu item          */
    gpointer user_data;
} AjazzTrayCallbacks;

/* Connects to the session bus, registers the tray item, and returns a
 * handle -- or NULL if the session bus isn't reachable at all (e.g.
 * running in an environment with no D-Bus, such as some CI/sandboxes).
 * A missing StatusNotifierWatcher (no compatible tray host running) is
 * NOT treated as failure -- registration just quietly does nothing
 * visible until/unless a compatible host appears, matching how every
 * other StatusNotifierItem app behaves. */
AjazzTray *ajazz_tray_new(const char *icon_name, const char *title, const AjazzTrayCallbacks *cb);

/* Updates the tooltip and the (disabled, label-only) battery line in the
 * menu, and notifies any listening host via the relevant D-Bus signals. */
void ajazz_tray_set_status(AjazzTray *tray, const char *device_line, const char *battery_line);

/* Swaps the tray icon itself, e.g. to a battery-NNN-symbolic name so the
 * charge level is visible without hovering for the tooltip. */
void ajazz_tray_set_icon(AjazzTray *tray, const char *icon_name);

void ajazz_tray_free(AjazzTray *tray);

#endif
