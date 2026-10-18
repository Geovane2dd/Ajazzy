#define _DEFAULT_SOURCE
/*
 * Ajazzy -- app shell: window, sidebar navigation, connection manager.
 *
 * The shell owns everything that is not a page: it finds the mouse (and
 * notices when it goes away or comes back), decides whether to show a page
 * or the "mouse not found" screen, and tells pages to refresh. Pages never
 * open or close the device themselves.
 *
 * Connection handling runs on a worker thread so a slow or half-dead hidraw
 * node can never freeze the window.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <malloc.h>

#include "app.h"
#include "../devices/device.h"
#include <glib/gstdio.h>
#include "../protocol/udev_rule.h"
#define AJAZZ_UDEV_RULE_TEXT AJAZZ_UDEV_RULE

#ifndef LOCALEDIR
#define LOCALEDIR "locale"
#endif
#ifndef ICONDIR
#define ICONDIR "gui/icons"
#endif

/* ---------------- small shared helpers ---------------- */

void free_closure_data(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

void toast(App *app, const char *msg)
{
    if (!app->toast_overlay)
        return;
    AdwToast *t = adw_toast_new(msg);
    adw_toast_set_timeout(t, 3);
    adw_toast_overlay_add_toast(app->toast_overlay, t);
}

void toastf(App *app, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    toast(app, msg);
    g_free(msg);
}

/* ---------------- async helper ---------------- */

typedef struct {
    App *app;
    AppWorkFn work;
    AppDoneFn done;
    gpointer data;
    GDestroyNotify free_data;
} AsyncJob;

static void async_thread(GTask *task, gpointer src, gpointer task_data, GCancellable *c)
{
    (void)src; (void)c;
    AsyncJob *j = task_data;
    if (j->work)
        j->work(j->app, j->data);
    g_task_return_boolean(task, TRUE);
}

static void async_finished(GObject *src, GAsyncResult *res, gpointer user_data)
{
    (void)src; (void)res;
    AsyncJob *j = user_data;
    if (j->done)
        j->done(j->app, j->data);
    if (j->free_data)
        j->free_data(j->data);
    g_free(j);
}

void app_async(App *app, AppWorkFn work, AppDoneFn done, gpointer data, GDestroyNotify free_data)
{
    AsyncJob *j = g_new0(AsyncJob, 1);
    j->app = app;
    j->work = work;
    j->done = done;
    j->data = data;
    j->free_data = free_data;
    GTask *task = g_task_new(NULL, NULL, async_finished, j);
    g_task_set_task_data(task, j, NULL);
    g_task_run_in_thread(task, async_thread);
    g_object_unref(task);
}

/* ---------------- pages ---------------- */

typedef struct {
    const char *id;
    const char *title;
    const char *icon;
    const char *group;   /* sidebar group label, shown above the first item of a group */
    gboolean needs_device;
    GtkWidget *(*build)(App *);
    void (*refresh)(App *);
} PageDef;

static const PageDef PAGES[] = {
    {"home",        N_("Overview"),    "go-home-symbolic",                  N_("Device"),    TRUE,  build_home_page,        refresh_home},
    {"buttons",     N_("Buttons"),     "input-mouse-symbolic",              N_("Customise"), TRUE,  build_buttons_page,     refresh_buttons},
    {"performance", N_("Performance"), "applications-engineering-symbolic", NULL,            TRUE,  build_performance_page, refresh_performance},
    {"lighting",    N_("Lighting"),    "weather-clear-symbolic",            NULL,            TRUE,  build_lighting_page,    refresh_lighting},
    {"macros",      N_("Macros"),      "input-keyboard-symbolic",           NULL,            TRUE,  build_macros_page,      refresh_macros},
    {"profiles",    N_("Profiles"),    "document-save-symbolic",            N_("App"),       FALSE, build_profiles_page,    refresh_profiles},
    {"settings",    N_("Settings"),    "emblem-system-symbolic",            NULL,            FALSE, build_settings_page,    NULL},
};
#define N_PAGES G_N_ELEMENTS(PAGES)

static const PageDef *find_page(const char *id)
{
    for (guint i = 0; i < N_PAGES; i++)
        if (strcmp(PAGES[i].id, id) == 0)
            return &PAGES[i];
    return NULL;
}

static void update_content_state(App *app)
{
    const PageDef *p = app->current_page ? find_page(app->current_page) : NULL;
    gboolean offline = (p && p->needs_device && !app->connected);
    gtk_stack_set_visible_child_name(app->content_stack, offline ? "offline" : "pages");
}

static void refresh_page(App *app, const char *id)
{
    const PageDef *p = find_page(id);
    if (!p || !p->refresh)
        return;
    if (app->connected || !p->needs_device)
        p->refresh(app);
}

void app_refresh_all(App *app)
{
    for (guint i = 0; i < N_PAGES; i++)
        refresh_page(app, PAGES[i].id);
}

void switch_to_page(App *app, const char *page_name)
{
    for (GtkWidget *c = gtk_widget_get_first_child(GTK_WIDGET(app->nav)); c; c = gtk_widget_get_next_sibling(c)) {
        const char *rid = g_object_get_data(G_OBJECT(c), "page");
        if (rid && strcmp(rid, page_name) == 0) {
            gtk_list_box_select_row(app->nav, GTK_LIST_BOX_ROW(c));
            return;
        }
    }
}

static void on_nav_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row)
        return;
    const char *id = g_object_get_data(G_OBJECT(row), "page");
    if (!id)
        return;
    const PageDef *p = find_page(id);
    if (!p)
        return;
    app->current_page = p->id;
    adw_view_stack_set_visible_child_name(app->stack, p->id);
    gtk_label_set_text(GTK_LABEL(app->header_title), gettext(p->title));
    update_content_state(app);
    refresh_page(app, p->id);
}

static void update_offline_page(App *app);

/* ---------------- connection manager ---------------- */

typedef struct {
    gboolean was_connected;
    gboolean now_connected;
    gboolean changed;   /* device appeared, disappeared or switched interface */
    int battery;        /* -1 = unknown / not read */
    gboolean charging;
    gboolean read_battery;
    gboolean asleep;
    int access;     /* ajazz_access_t, filled in while not connected */
    /* A freshly opened device is built privately by the worker and swapped into
     * app->dev on the main thread (under hid_lock), so the UI never reads fields
     * that a worker is in the middle of writing. */
    gboolean opened;
    gboolean close_old;
    ajazz_dev_t nd;
} ConnResult;

/* Runs on a worker thread. Reads app->dev only through the locked wrappers (plus
 * dev.path, which only this serialized job and conn_done ever change) and never
 * touches GTK. */
static void conn_work(App *app, gpointer data)
{
    ConnResult *r = data;
    r->battery = -1;
    r->nd.fd = -1;

    if (g_getenv("AJAZZY_NO_DEVICE")) { /* dev aid: review the "no mouse" screens */
        r->access = g_getenv("AJAZZY_NO_DEVICE")[0] == 'p' ? AJAZZ_ACCESS_DENIED : AJAZZ_ACCESS_NO_DEVICE;
        return;
    }

    gboolean use_new = FALSE;
    if (r->was_connected) {
        gboolean still_here = (access(app->dev.path, F_OK) == 0);
        gboolean better = FALSE;
        char best[280];
        int charging = 0;
        if (still_here && ajazz_best_path(best, sizeof(best), &charging)) {
            if (strcmp(best, app->dev.path) != 0)
                better = TRUE;
            else
                r->charging = charging;
        }
        if (still_here && !better) {
            r->now_connected = TRUE;
            goto battery;
        }
        r->close_old = TRUE;
        r->changed = TRUE;
    }

    if (ajazz_open(&r->nd) == 0) {
        r->opened = TRUE;
        use_new = TRUE;
        r->now_connected = TRUE;
        r->changed = TRUE;
        r->charging = r->nd.charging;
    } else {
        r->access = ajazz_probe_access();
    }

battery:
    /* The dongle answers the identity query even while the mouse sleeps, but every
     * setting query then goes unanswered -- so probe with one of those first, and
     * only read the battery (a second over-the-air round trip) if the mouse answers. */
    if (r->now_connected && r->read_battery) {
        ajazz_report_t probe, reply;
        int rc;
        /* a device nobody else can reach yet is queried directly; the live one under the lock */
        rc = use_new ? ajazz_query(&r->nd, AJAZZ_QUERY_REPORT_RATE, &probe, 500)
                     : ajazz_query_mt(app, AJAZZ_QUERY_REPORT_RATE, &probe, 500);
        r->asleep = rc != 0;
        if (!r->asleep) {
            rc = use_new ? ajazz_query(&r->nd, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000)
                         : ajazz_query_mt(app, AJAZZ_QUERY_DEVICE_INFO, &reply, 1000);
            if (rc == 0)
                r->battery = reply.payload[AJAZZ_BATTERY_PAYLOAD_OFFSET];
        }
    }
}

static void update_sidebar_status(App *app)
{
    ui_set_class(app->side_dot, "aj-dot-on", app->connected);
    ui_set_class(app->side_dot, "aj-dot-off", !app->connected);
    if (app->connected) {
        gtk_label_set_text(app->side_name, app->dev.name);
        gtk_label_set_text(app->side_state, app->dev.mode[0] ? app->dev.mode : "USB");
    } else {
        gtk_label_set_text(app->side_name, _("No mouse"));
        gtk_label_set_text(app->side_state, _("Not connected"));
    }
    if (app->battery_percent >= 0 && app->connected) {
        char b[24];
        snprintf(b, sizeof(b), "%d%%", app->battery_percent);
        ui_chip_set(app->side_battery, b,
            app->charging ? UI_CHIP_ACCENT : (app->battery_percent < 20 ? UI_CHIP_ERROR : UI_CHIP_NEUTRAL));
        gtk_widget_set_visible(app->side_battery, TRUE);
    } else {
        gtk_widget_set_visible(app->side_battery, FALSE);
    }
}

void app_connection_changed(App *app)
{
    update_sidebar_status(app);
    update_content_state(app);
}

static void conn_done(App *app, gpointer data)
{
    ConnResult *r = data;
    app->connect_busy = FALSE;

    /* swap the device handle on the main thread, waiting for any in-flight query */
    if (r->close_old || r->opened) {
        g_mutex_lock(&app->hid_lock);
        if (app->dev.fd >= 0)
            ajazz_close(&app->dev);
        if (r->opened) {
            app->dev = r->nd;
            r->nd.fd = -1; /* ownership moved */
        }
        g_mutex_unlock(&app->hid_lock);
    }
    if (r->nd.fd >= 0) /* opened but not adopted (shouldn't happen) */
        ajazz_close(&r->nd);

    gboolean was = app->connected;
    app->connected = r->now_connected;
    app->charging = r->charging;
    if (r->battery >= 0)
        app->battery_percent = r->battery;
    if (!app->connected)
        app->battery_percent = -1;

    gboolean was_asleep = app->asleep;
    app->asleep = app->connected && r->asleep;
    if (r->read_battery && app->asleep != was_asleep)
        adw_banner_set_revealed(app->asleep_banner, app->asleep);

    if (!was && app->connected)
        toastf(app, _("Connected to %s"), app->dev.name);
    else if (was && !app->connected)
        toast(app, _("Mouse disconnected"));

    app->access_state = app->connected ? AJAZZ_ACCESS_OK : r->access;
    update_offline_page(app);
    app_connection_changed(app);
    home_set_battery(app, app->battery_percent, app->charging);
    settings_check_low_battery(app, app->battery_percent, app->charging);

    if (r->changed || was != app->connected || (was_asleep && !app->asleep)) {
        if (app->connected && !app->asleep)
            app_refresh_all(app);
        else if (!app->connected)
            lighting_shutdown(app);
    }

    /* the mouse just (re)connected: put the user's last profile back */
    if (!was && app->connected && app->settings_profile_on_start && app->settings_last_profile[0]) {
        char *name = g_strdup(app->settings_last_profile);
        profiles_apply_by_name(app, name);
        g_free(name);
    }
}

/* Presence is checked every tick with plain filesystem calls (no traffic to
 * the mouse). Anything that talks to the mouse over the air -- the battery
 * read -- is rationed, because every query wakes its radio and costs
 * battery: every 30 s while the window is open, every 2 min while it is
 * hidden in the tray, plus whenever the connection (re)starts. */
#define BATTERY_POLL_TICKS_VISIBLE 15  /* x 2 s */
#define BATTERY_POLL_TICKS_HIDDEN  60

static void connection_poll_ex(App *app, gboolean force_battery)
{
    if (app->connect_busy)
        return;
    app->connect_busy = TRUE;
    ConnResult *r = g_new0(ConnResult, 1);
    r->was_connected = app->connected;
    int period = gtk_widget_get_visible(GTK_WIDGET(app->win)) ? BATTERY_POLL_TICKS_VISIBLE : BATTERY_POLL_TICKS_HIDDEN;
    /* While the mouse is asleep nothing is rationed: a probe to a sleeping mouse
     * costs it nothing (it isn't listening) and the banner must go away the moment
     * it wakes, not up to 30 s later. */
    r->read_battery = force_battery || app->asleep || !app->connected || app->battery_percent < 0
        || (app->tick % period == 0);
    app_async(app, conn_work, conn_done, r, g_free);
}

static void connection_poll(App *app)
{
    connection_poll_ex(app, TRUE);
}

static gboolean connection_tick(gpointer user_data)
{
    App *app = user_data;
    app->tick++;
    connection_poll_ex(app, FALSE);
    return G_SOURCE_CONTINUE;
}

/* ---------------- window chrome ---------------- */

static void on_retry_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    connection_poll(user_data);
}

/* "plugged in but not allowed" needs the udev rule; "not plugged in" needs a
 * cable. The offline screen says which one it is and, for the first, offers
 * the exact command, a copy button and a one-click fix. */
static AdwStatusPage *g_offline;
static GtkWidget *g_perm_box, *g_cmd_label, *g_fix_btn;

static void update_offline_page(App *app)
{
    if (!g_offline)
        return;
    gboolean denied = !app->connected && app->access_state == AJAZZ_ACCESS_DENIED;
    adw_status_page_set_icon_name(g_offline, denied ? "changes-prevent-symbolic" : "input-mouse-symbolic");
    adw_status_page_set_title(g_offline, denied ? _("Permission needed") : _("No AJAZZ mouse found"));
    adw_status_page_set_description(g_offline, denied
        ? _("The mouse is plugged in, but your user isn't allowed to talk to it yet. "
            "This is a one-time setup: Ajazzy installs a small udev rule that gives the logged-in user access.")
        : _("Plug in the mouse (or its 2.4G dongle) and turn it on."));
    gtk_widget_set_visible(g_perm_box, denied);
}

typedef struct { App *app; char *tmp; } FixCtx;

static void on_fix_finished(GObject *src, GAsyncResult *res, gpointer user_data)
{
    FixCtx *c = user_data;
    GError *err = NULL;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(src), res, &err) == TRUE;
    if (c->tmp) { g_remove(c->tmp); g_free(c->tmp); }
    gtk_widget_set_sensitive(g_fix_btn, TRUE);
    if (ok) {
        toast(c->app, _("Access granted \xE2\x80\x94 looking for the mouse\xE2\x80\xA6"));
        connection_poll(c->app);
    } else {
        toast(c->app, _("Couldn't apply the fix automatically. Copy the command and run it in a terminal."));
    }
    g_clear_error(&err);
    g_free(c);
}

static void on_fix_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    char *tmp = NULL;
    int fd = g_file_open_tmp("ajazzy-udev-XXXXXX.rules", &tmp, NULL);
    if (fd < 0) { toast(app, _("Couldn't prepare the fix.")); return; }
    close(fd);
    g_file_set_contents(tmp, AJAZZ_UDEV_RULE_TEXT, -1, NULL);

    GError *err = NULL;
    /* pkexec shows the desktop's own password prompt; the script is fixed text
     * plus our temp file path, nothing user-controlled */
    const char *script = "install -m644 \"$1\" /etc/udev/rules.d/71-ajazzy.rules && "
        "udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw --action=change";
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &err,
        "pkexec", "sh", "-c", script, "sh", tmp, NULL);
    if (!p) {
        toast(app, _("pkexec isn't available. Copy the command and run it in a terminal."));
        g_clear_error(&err);
        g_remove(tmp);
        g_free(tmp);
        return;
    }
    gtk_widget_set_sensitive(g_fix_btn, FALSE);
    FixCtx *c = g_new0(FixCtx, 1);
    c->app = app;
    c->tmp = tmp;
    g_subprocess_wait_check_async(p, NULL, on_fix_finished, c);
    g_object_unref(p);
}

static void on_copy_cmd_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()), ajazz_udev_fix_command());
    toast(user_data, _("Command copied \xE2\x80\x94 paste it in a terminal."));
}

static GtkWidget *build_offline_page(App *app)
{
    AdwStatusPage *sp = ADW_STATUS_PAGE(adw_status_page_new());
    g_offline = sp;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(box, 560, -1);

    g_perm_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
    g_fix_btn = ui_button_new(_("Fix it automatically"), "object-select-symbolic", "primary");
    g_signal_connect(g_fix_btn, "clicked", G_CALLBACK(on_fix_clicked), app);
    GtkWidget *copy = ui_button_new(_("Copy the command"), "edit-copy-symbolic", "secondary");
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_cmd_clicked), app);
    gtk_box_append(GTK_BOX(row), g_fix_btn);
    gtk_box_append(GTK_BOX(row), copy);
    gtk_box_append(GTK_BOX(g_perm_box), row);

    GtkWidget *hint = gtk_label_new(_("Or run this in a terminal, then Ajazzy will find the mouse on its own:"));
    gtk_widget_add_css_class(hint, "aj-page-sub");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(g_perm_box), hint);
    g_cmd_label = gtk_label_new(ajazz_udev_fix_command());
    gtk_label_set_selectable(GTK_LABEL(g_cmd_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(g_cmd_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(g_cmd_label), 0);
    gtk_widget_add_css_class(g_cmd_label, "monospace");
    gtk_widget_add_css_class(g_cmd_label, "aj-callout");
    gtk_widget_add_css_class(g_cmd_label, "aj-callout-info");
    gtk_box_append(GTK_BOX(g_perm_box), g_cmd_label);
    gtk_box_append(GTK_BOX(box), g_perm_box);

    GtkWidget *retry = ui_button_new(_("Search again"), "view-refresh-symbolic", "secondary");
    gtk_widget_set_halign(retry, GTK_ALIGN_CENTER);
    g_signal_connect(retry, "clicked", G_CALLBACK(on_retry_clicked), app);
    gtk_box_append(GTK_BOX(box), retry);

    adw_status_page_set_child(sp, box);
    app->offline_status = GTK_WIDGET(sp);
    update_offline_page(app);
    return GTK_WIDGET(sp);
}

static GtkWidget *build_sidebar(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "aj-sidebar");

    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(brand, "aj-brand");
    GtkWidget *logo = gtk_image_new_from_icon_name(AJAZZ_APP_ID);
    gtk_image_set_pixel_size(GTK_IMAGE(logo), 36);
    gtk_box_append(GTK_BOX(brand), logo);
    GtkWidget *bt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(bt, GTK_ALIGN_CENTER);
    GtkWidget *bn = gtk_label_new("AJAZZY");
    gtk_widget_add_css_class(bn, "aj-brand-name");
    gtk_widget_set_halign(bn, GTK_ALIGN_START);
    GtkWidget *bs = gtk_label_new(_("Mouse configurator"));
    gtk_widget_add_css_class(bs, "aj-brand-sub");
    gtk_widget_set_halign(bs, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bt), bn);
    gtk_box_append(GTK_BOX(bt), bs);
    gtk_box_append(GTK_BOX(brand), bt);
    gtk_box_append(GTK_BOX(box), brand);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "aj-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    app->nav = GTK_LIST_BOX(list);

    for (guint i = 0; i < N_PAGES; i++) {
        if (PAGES[i].group) {
            GtkWidget *grow = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(grow), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(grow), FALSE);
            GtkWidget *gl = gtk_label_new(gettext(PAGES[i].group));
            gtk_widget_add_css_class(gl, "aj-nav-group");
            gtk_widget_set_halign(gl, GTK_ALIGN_START);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(grow), gl);
            gtk_list_box_append(GTK_LIST_BOX(list), grow);
        }
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *rb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(rb, 12);
        gtk_widget_set_margin_end(rb, 12);
        gtk_box_append(GTK_BOX(rb), gtk_image_new_from_icon_name(PAGES[i].icon));
        GtkWidget *lbl = gtk_label_new(gettext(PAGES[i].title));
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(rb), lbl);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rb);
        g_object_set_data(G_OBJECT(row), "page", (gpointer)PAGES[i].id);

        char tip[64];
        snprintf(tip, sizeof(tip), "%s  (Ctrl+%u)", gettext(PAGES[i].title), i + 1);
        gtk_widget_set_tooltip_text(row, tip);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    g_signal_connect(list, "row-selected", G_CALLBACK(on_nav_row_selected), app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);

    /* footer: connection state + battery */
    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(foot, "aj-side-foot");
    app->side_dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(app->side_dot, "aj-dot");
    gtk_widget_add_css_class(app->side_dot, "aj-dot-off");
    gtk_widget_set_valign(app->side_dot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(foot), app->side_dot);

    GtkWidget *ft = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(ft, TRUE);
    app->side_name = GTK_LABEL(gtk_label_new(_("No mouse")));
    gtk_widget_set_halign(GTK_WIDGET(app->side_name), GTK_ALIGN_START);
    gtk_label_set_xalign(app->side_name, 0);
    gtk_label_set_ellipsize(app->side_name, PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(GTK_WIDGET(app->side_name), "heading");
    app->side_state = GTK_LABEL(gtk_label_new(_("Not connected")));
    gtk_widget_set_halign(GTK_WIDGET(app->side_state), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(app->side_state), "caption");
    gtk_widget_add_css_class(GTK_WIDGET(app->side_state), "dim-label");
    gtk_box_append(GTK_BOX(ft), GTK_WIDGET(app->side_name));
    gtk_box_append(GTK_BOX(ft), GTK_WIDGET(app->side_state));
    gtk_box_append(GTK_BOX(foot), ft);

    app->side_battery = ui_chip_new("--", UI_CHIP_NEUTRAL);
    gtk_widget_set_visible(app->side_battery, FALSE);
    gtk_box_append(GTK_BOX(foot), app->side_battery);
    gtk_box_append(GTK_BOX(box), foot);
    return box;
}

/* ---------------- shortcuts ---------------- */

static gboolean on_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
    (void)c; (void)keycode;
    App *app = user_data;
    if (state & GDK_CONTROL_MASK) {
        if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_7) {
            guint i = keyval - GDK_KEY_1;
            if (i < N_PAGES) {
                switch_to_page(app, PAGES[i].id);
                return TRUE;
            }
        }
        if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
            g_application_quit(G_APPLICATION(app->gtk_app));
            return TRUE;
        }
    }
    if (keyval == GDK_KEY_F5) {
        connection_poll(app);
        app_refresh_all(app);
        return TRUE;
    }
    return FALSE;
}

/* ---------------- tray ---------------- */

static void on_tray_activate(gpointer user_data)
{
    App *app = user_data;
    gtk_widget_set_visible(GTK_WIDGET(app->win), TRUE);
    gtk_window_present(GTK_WINDOW(app->win));
}

static void on_tray_refresh(gpointer user_data)
{
    App *app = user_data;
    connection_poll(app);
    app_refresh_all(app);
}

static void on_tray_quit(gpointer user_data)
{
    App *app = user_data;
    g_application_quit(G_APPLICATION(app->gtk_app));
}

static void on_refresh_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    on_tray_refresh(user_data);
}

/* glibc keeps freed memory around; after the big allocations of startup, or when the
 * window goes to the tray, hand it back so a background app stays small. */
static gboolean trim_heap_cb(gpointer unused)
{
    (void)unused;
    malloc_trim(0);
    return G_SOURCE_REMOVE;
}

static gboolean on_window_close_request(GtkWindow *win, gpointer user_data)
{
    App *app = user_data;
    if (app->settings_close_to_tray && app->tray) {
        gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
        g_timeout_add_seconds(1, trim_heap_cb, NULL);
        return TRUE;
    }
    g_application_quit(G_APPLICATION(app->gtk_app));
    return FALSE;
}

/* ---------------- dev aid: render every page to a PNG ---------------- */

typedef struct { App *app; const char *dir; guint idx; } snap_ctx_t;

static gboolean snapshot_step(gpointer user_data)
{
    snap_ctx_t *c = user_data;
    GtkWidget *w = GTK_WIDGET(c->app->win);
    if (c->idx > 0) {
        GdkPaintable *p = gtk_widget_paintable_new(w);
        int pw = gtk_widget_get_width(w), ph = gtk_widget_get_height(w);
        GtkSnapshot *snap = gtk_snapshot_new();
        gdk_paintable_snapshot(p, snap, pw, ph);
        GskRenderNode *node = gtk_snapshot_free_to_node(snap);
        GskRenderer *r = gtk_native_get_renderer(gtk_widget_get_native(w));
        if (node && r) {
            GdkTexture *tex = gsk_renderer_render_texture(r, node, &GRAPHENE_RECT_INIT(0, 0, pw, ph));
            char *path = g_strdup_printf("%s/%s.png", c->dir, PAGES[c->idx - 1].id);
            gdk_texture_save_to_png(tex, path);
            g_free(path);
            g_object_unref(tex);
        }
        if (node)
            gsk_render_node_unref(node);
        g_object_unref(p);
    }
    if (c->idx >= N_PAGES) {
        g_application_quit(G_APPLICATION(c->app->gtk_app));
        return G_SOURCE_REMOVE;
    }
    switch_to_page(c->app, PAGES[c->idx].id);
    c->idx++;
    return G_SOURCE_CONTINUE;
}

/* ---------------- activation ---------------- */

static void on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    App *app = user_data;
    if (app->win) {
        gtk_widget_set_visible(GTK_WIDGET(app->win), TRUE);
        gtk_window_present(GTK_WINDOW(app->win));
        return;
    }
    app->gtk_app = gtk_app;
    g_application_hold(G_APPLICATION(gtk_app));

    settings_load_and_apply(app);
    macros_load_library(app);

    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), ICONDIR);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, AJAZZ_CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    app->win = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(gtk_app)));
    gtk_window_set_title(GTK_WINDOW(app->win), "Ajazzy");
    gtk_window_set_icon_name(GTK_WINDOW(app->win), AJAZZ_APP_ID);
    gtk_window_set_default_size(GTK_WINDOW(app->win), 1120, g_getenv("AJAZZY_SNAPSHOT_DIR") ? 2100 : 760);
    g_signal_connect(app->win, "close-request", G_CALLBACK(on_window_close_request), app);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(GTK_WIDGET(app->win), keys);

    app->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());

    app->stack = ADW_VIEW_STACK(adw_view_stack_new());
    for (guint i = 0; i < N_PAGES; i++)
        adw_view_stack_add_named(app->stack, PAGES[i].build(app), PAGES[i].id);

    app->content_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(app->content_stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(app->content_stack, GTK_WIDGET(app->stack), "pages");
    gtk_stack_add_named(app->content_stack, build_offline_page(app), "offline");

    GtkWidget *sidebar_content = build_sidebar(app);
    AdwNavigationPage *sidebar_page = adw_navigation_page_new(sidebar_content, "Ajazzy");

    AdwHeaderBar *hb = ADW_HEADER_BAR(adw_header_bar_new());
    app->header_title = gtk_label_new("");
    gtk_widget_add_css_class(app->header_title, "heading");
    adw_header_bar_set_title_widget(hb, app->header_title);
    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh, _("Re-read from the mouse (F5)"));
    gtk_widget_add_css_class(refresh, "flat");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_clicked), app);
    adw_header_bar_pack_end(hb, refresh);

    app->asleep_banner = ADW_BANNER(adw_banner_new(_("The mouse is asleep \xE2\x80\x94 move it or click a button to wake it.")));
    adw_banner_set_button_label(app->asleep_banner, _("Check again"));
    g_signal_connect_swapped(app->asleep_banner, "button-clicked", G_CALLBACK(on_tray_refresh), app);

    AdwToolbarView *tv = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(tv, GTK_WIDGET(hb));
    adw_toolbar_view_add_top_bar(tv, GTK_WIDGET(app->asleep_banner));
    adw_toolbar_view_set_content(tv, GTK_WIDGET(app->content_stack));
    AdwNavigationPage *content_page = adw_navigation_page_new(GTK_WIDGET(tv), "Ajazzy");

    AdwNavigationSplitView *split = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    adw_navigation_split_view_set_sidebar(split, sidebar_page);
    adw_navigation_split_view_set_content(split, content_page);
    adw_navigation_split_view_set_min_sidebar_width(split, 230);
    adw_navigation_split_view_set_max_sidebar_width(split, 270);

    adw_toast_overlay_set_child(app->toast_overlay, GTK_WIDGET(split));
    adw_application_window_set_content(app->win, GTK_WIDGET(app->toast_overlay));

    AjazzTrayCallbacks tray_cb = {
        .on_activate = on_tray_activate,
        .on_refresh = on_tray_refresh,
        .on_quit = on_tray_quit,
        .user_data = app,
    };
    app->tray = ajazz_tray_new("battery-missing-symbolic", "Ajazzy", &tray_cb);

    const char *snap_dir = g_getenv("AJAZZY_SNAPSHOT_DIR");
    switch_to_page(app, "home");
    if (snap_dir || !(app->settings_start_minimized && app->tray))
        gtk_window_present(GTK_WINDOW(app->win));

    update_sidebar_status(app);
    update_content_state(app);
    connection_poll(app);
    g_timeout_add_seconds(2, connection_tick, app);
    g_timeout_add_seconds(8, trim_heap_cb, NULL); /* after the first refresh burst has settled */

    if (snap_dir) {
        /* page transitions need frames to finish; a headless capture may not get them */
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, NULL);
        snap_ctx_t *c = g_new0(snap_ctx_t, 1);
        c->app = app;
        c->dir = snap_dir;
        g_timeout_add(3500, snapshot_step, c);
    }
}

/* LOCALEDIR is the path baked in at compile time, which is the build tree --
 * useless for a package built on a CI runner and installed elsewhere. So look
 * for the translation catalogue where an installed copy would put it: next to
 * the binary (tarball), under its ../share (prefix install), or in the usual
 * system locations, and only then fall back to the compiled-in path. */
static const char *pick_localedir(void)
{
    static char found[4096];
    GPtrArray *c = g_ptr_array_new_with_free_func(g_free);

    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *bin = g_path_get_dirname(exe);
        g_ptr_array_add(c, g_build_filename(bin, "locale", NULL));
        g_ptr_array_add(c, g_build_filename(bin, "..", "share", "locale", NULL));
        g_free(bin);
    }
    g_ptr_array_add(c, g_strdup("/usr/local/share/locale"));
    g_ptr_array_add(c, g_strdup("/usr/share/locale"));
    g_ptr_array_add(c, g_strdup(LOCALEDIR));

    const char *result = LOCALEDIR;
    for (guint i = 0; i < c->len; i++) {
        char *probe = g_build_filename(g_ptr_array_index(c, i), "pt_BR", "LC_MESSAGES", "ajazzy.mo", NULL);
        gboolean ok = g_file_test(probe, G_FILE_TEST_EXISTS);
        g_free(probe);
        if (ok) {
            g_strlcpy(found, g_ptr_array_index(c, i), sizeof(found));
            result = found;
            break;
        }
    }
    g_ptr_array_free(c, TRUE);
    return result;
}

int main(int argc, char **argv)
{
    /* a handful of worker threads would otherwise each grow their own malloc arena */
    mallopt(M_ARENA_MAX, 2);
    setlocale(LC_ALL, "");
    bindtextdomain("ajazzy", pick_localedir());
    bind_textdomain_codeset("ajazzy", "UTF-8");
    textdomain("ajazzy");

    App app = {0};
    app.dev.fd = -1;
    app.rate_hz = -1;
    app.battery_percent = -1;
    g_mutex_init(&app.hid_lock);

    AdwApplication *gtk_app = adw_application_new(AJAZZ_APP_ID,
        (g_getenv("AJAZZY_SNAPSHOT_DIR") || g_getenv("AJAZZY_NEW_INSTANCE")) ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS); /* dev runs must not hand off to a running copy */
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &app);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    lighting_shutdown(&app);
    macros_save_library(&app);
    ajazz_tray_free(app.tray);
    g_mutex_lock(&app.hid_lock);
    if (app.dev.fd >= 0)
        ajazz_close(&app.dev);
    g_mutex_unlock(&app.hid_lock);

    g_object_unref(gtk_app);
    return status;
}
