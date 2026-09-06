/*
 * Buttons page: remap the 5 physical buttons (consumer keys, keyboard
 * keys, or rapid-fire), reset to factory defaults, and the mouse
 * diagram that shows which physical button is which slot.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "../devices/device.h"

enum { KF_SKIP, KF_RESET, KF_CONSUMER, KF_KEYBOARD, KF_FIRE };

typedef struct {
    char label[48];
    int kind;
    uint16_t code;
} key_function_t;

#define MAX_KEY_FUNCTIONS 64
static key_function_t key_functions[MAX_KEY_FUNCTIONS];
static size_t n_key_functions;
void init_key_functions(void)
{
    size_t i = 0;

    g_strlcpy(key_functions[i].label, _("Don't change"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_SKIP;
    i++;

    g_strlcpy(key_functions[i].label, _("Default (built-in click)"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_RESET;
    i++;

    g_strlcpy(key_functions[i].label, _("Rapid-fire (auto-click)"), sizeof(key_functions[i].label));
    key_functions[i].kind = KF_FIRE;
    i++;

    struct { const char *label; uint16_t usage; } consumer[] = {
        {N_("Volume up"),     AJAZZ_USAGE_VOLUME_UP},
        {N_("Volume down"),   AJAZZ_USAGE_VOLUME_DOWN},
        {N_("Mute"),          AJAZZ_USAGE_MUTE},
        {N_("Play / Pause"),  AJAZZ_USAGE_PLAY_PAUSE},
        {N_("Next track"),    AJAZZ_USAGE_NEXT_TRACK},
        {N_("Previous track"),AJAZZ_USAGE_PREV_TRACK},
        {N_("Stop"),          AJAZZ_USAGE_STOP},
        {N_("Browser home"),  AJAZZ_USAGE_AC_HOME},
        {N_("Browser back"),  AJAZZ_USAGE_AC_BACK},
        {N_("Browser forward"),AJAZZ_USAGE_AC_FORWARD},
        {N_("Refresh page"),  AJAZZ_USAGE_AC_REFRESH},
        {N_("Bookmarks"),     AJAZZ_USAGE_AC_BOOKMARKS},
    };
    for (size_t c = 0; c < G_N_ELEMENTS(consumer); c++, i++) {
        g_strlcpy(key_functions[i].label, gettext(consumer[c].label), sizeof(key_functions[i].label));
        key_functions[i].kind = KF_CONSUMER;
        key_functions[i].code = consumer[c].usage;
    }

    for (char c = 'A'; c <= 'Z'; c++, i++) {
        snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), c);
        key_functions[i].kind = KF_KEYBOARD;
        key_functions[i].code = (uint8_t)(0x04 + (c - 'A'));
    }
    for (char c = '1'; c <= '9'; c++, i++) {
        snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), c);
        key_functions[i].kind = KF_KEYBOARD;
        key_functions[i].code = (uint8_t)(0x1E + (c - '1'));
    }
    snprintf(key_functions[i].label, sizeof(key_functions[i].label), _("Key %c"), '0');
    key_functions[i].kind = KF_KEYBOARD;
    key_functions[i].code = 0x27;
    i++;

    n_key_functions = i;
}
#define N_KEY_FUNCTIONS n_key_functions
static void on_keys_reset_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    App *app = user_data;
    if (strcmp(response, "reset") != 0)
        return;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t req;
    ajazz_build_reset_keys(&req);
    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0) {
        toast(app, _("Button mapping reset to default."));
        refresh_buttons_delayed(app);
    } else {
        toast(app, _("Failed to reset buttons."));
    }
}

static void on_keys_reset_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Reset all buttons?"),
        _("This clears every button remap and restores the factory defaults.")));
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "reset", _("Reset"));
    adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_keys_reset_response), app);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(app->win));
}

static void on_keys_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        toast(app, _("Could not read the current button table."));
        return;
    }

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);

    gboolean changed = FALSE;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        if (!app->key_dropdown[i]) continue; /* left/right click, not remappable */
        guint sel = adw_combo_row_get_selected(app->key_dropdown[i]);
        if (sel >= N_KEY_FUNCTIONS) continue;
        const key_function_t *kf = &key_functions[sel];
        if (kf->kind == KF_SKIP) continue;
        changed = TRUE;
        if (kf->kind == KF_RESET) {
            ajazz_key_slot_reset_default(&req, i);
        } else if (kf->kind == KF_CONSUMER) {
            ajazz_key_slot_set_consumer(&req, i, kf->code);
        } else if (kf->kind == KF_KEYBOARD) {
            ajazz_key_slot_set_keyboard(&req, i, 0, (uint8_t)kf->code);
        } else if (kf->kind == KF_FIRE) {
            uint8_t interval = app->key_fire_interval[i]
                ? (uint8_t)adw_spin_row_get_value(app->key_fire_interval[i]) : 5;
            uint8_t number = app->key_fire_number[i]
                ? (uint8_t)adw_spin_row_get_value(app->key_fire_number[i]) : 1;
            ajazz_key_slot_set_fire(&req, i, interval, number);
        }
    }

    if (!changed) { toast(app, _("Nothing selected to change.")); return; }

    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0) {
        toast(app, _("Buttons remapped."));
        refresh_buttons_delayed(app);
    } else {
        toast(app, _("Failed to apply the remap."));
    }
}

/* Finds the dropdown entry that matches what's actually on the mouse
 * right now, if there is one -- KF_RESET for the native-default marker,
 * a KF_CONSUMER/KF_KEYBOARD entry for a recognised code, or -1 for
 * anything we don't have a dropdown entry for (a macro trigger, or a raw
 * code we don't recognise). */
static int find_key_function_for_slot(const uint8_t *slot)
{
    uint8_t marker = slot[0];

    if (marker == 0x10) {
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_RESET)
                return (int)i;
        return -1;
    }
    if (marker == 0x80) {
        uint16_t code = (uint16_t)(slot[1] | (slot[2] << 8));
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_CONSUMER && key_functions[i].code == code)
                return (int)i;
        return -1;
    }
    if (marker == 0x70) {
        uint8_t keycode = slot[2];
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_KEYBOARD && key_functions[i].code == keycode)
                return (int)i;
        return -1;
    }
    if (marker == 0x30) {
        for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
            if (key_functions[i].kind == KF_FIRE)
                return (int)i;
        return -1;
    }
    return -1;
}

/* Text shown under each button's dropdown for what's actually on the
 * mouse right now -- the dropdown's own selected-item text alone isn't
 * a strong enough signal at a glance. */
static void describe_key_slot(const uint8_t *slot, char *out, size_t outlen)
{
    int idx = find_key_function_for_slot(slot);
    if (idx >= 0) {
        snprintf(out, outlen, _("Currently: %s"), key_functions[idx].label);
        return;
    }
    if (slot[0] == 0x90) {
        snprintf(out, outlen, _("Currently: plays a macro"));
        return;
    }
    snprintf(out, outlen, _("Currently: custom code (0x%02x, not in this list)"), slot[0]);
}

/* What each slot does out of the box. Slot 5 (Forward) is the only one
 * independently confirmed -- resetting it brought back "Forward" in the
 * original driver. The rest follow the standard Windows mouse button
 * order (Left, Right, Middle, Back=XButton1, Forward=XButton2), which
 * this lines up with exactly, but 1-4 haven't been checked one by one. */
static const char *default_button_names[AJAZZ_KEY_SLOTS] = {
    N_("Left click"), N_("Right click"), N_("Middle click"), N_("Back"), N_("Forward"),
};

/* Left/right click can't be remapped in the original driver, so we
 * don't offer it here either -- better to match a limit we know is
 * real than to let someone remap a button and wonder why nothing
 * changes. */
static gboolean key_slot_is_lockable(int slot)
{
    return slot == 0 || slot == 1;
}

/* Shows the interval/number fields only for the row currently set to
 * rapid-fire -- everyone else doesn't need to see them. */
static void update_fire_box_visibility(App *app, int slot)
{
    if (!app->key_dropdown[slot] || !app->key_fire_box[slot])
        return;
    guint sel = adw_combo_row_get_selected(app->key_dropdown[slot]);
    gboolean show = (sel < N_KEY_FUNCTIONS && key_functions[sel].kind == KF_FIRE);
    gtk_widget_set_visible(app->key_fire_box[slot], show);
}

static void on_key_dropdown_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd; (void)pspec;
    gpointer *data = user_data;
    update_fire_box_visibility(data[0], GPOINTER_TO_INT(data[1]));
}

/* Heap-allocated result of the fetch thread below, handed to
 * buttons_fetch_apply() on the main thread via g_idle_add(). Just a
 * copy of the reply -- everything in the original refresh_buttons()
 * body past the query only touches GTK widgets, so it all belongs in
 * the apply phase unchanged. */
typedef struct {
    App *app;
    gboolean ok;
    ajazz_report_t reply;
} ButtonsFetchResult;

static gboolean buttons_fetch_apply(gpointer user_data)
{
    ButtonsFetchResult *res = user_data;
    App *app = res->app;
    app->buttons_fetch_pending = FALSE;

    if (!app->connected || !res->ok) {
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        const uint8_t *slot = &res->reply.payload[i * 3];
        if (!app->key_dropdown[i])
            continue; /* left/right click, not remappable */

        int idx = find_key_function_for_slot(slot);
        adw_combo_row_set_selected(app->key_dropdown[i], idx >= 0 ? (guint)idx : 0);

        char desc[64];
        describe_key_slot(slot, desc, sizeof(desc));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(app->key_dropdown[i]), desc);

        /* Home dashboard's compact readout -- see App::home_key_lbl. */
        if (app->home_key_lbl[i])
            gtk_label_set_text(app->home_key_lbl[i], idx >= 0 ? key_functions[idx].label : desc);

        if (slot[0] == 0x30 && app->key_fire_interval[i] && app->key_fire_number[i]) {
            adw_spin_row_set_value(app->key_fire_interval[i], slot[1]);
            adw_spin_row_set_value(app->key_fire_number[i], slot[2]);
        }
        update_fire_box_visibility(app, i);
    }

    g_free(res);
    return G_SOURCE_REMOVE;
}

/* Runs off the main thread -- MUST NOT touch any GTK widget, only
 * app->dev via the *_mt() wrapper (see App::hid_lock) and the heap
 * result handed to buttons_fetch_apply(). */
static gpointer buttons_fetch_thread(gpointer user_data)
{
    App *app = user_data;
    ButtonsFetchResult *res = g_new0(ButtonsFetchResult, 1);
    res->app = app;
    res->ok = (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &res->reply, 1000) == 0);
    g_idle_add(buttons_fetch_apply, res);
    return NULL;
}

/* Called every time the Buttons page becomes visible, so what's on
 * screen is always what the mouse actually has right now. Reads on a
 * background thread so a slow/unplugged mouse can't freeze the window
 * -- see buttons_fetch_thread()/buttons_fetch_apply() and the comment
 * on App::hid_lock. */
void refresh_buttons(App *app)
{
    if (!app->connected)
        return;
    if (app->buttons_fetch_pending)
        return;
    app->buttons_fetch_pending = TRUE;
    g_thread_unref(g_thread_new("ajazzy-buttons-fetch", buttons_fetch_thread, app));
}

/* Public wrapper for gui/page_profiles.c -- see the declaration in
 * app.h. `raw` is exactly the 3-byte-per-slot wire format
 * AJAZZ_QUERY_KEY_TABLE's payload uses (marker/param1/param2), which is
 * also exactly what a profile save captures (see page_profiles.c) -- so
 * applying it is just: read the current table (to get a valid base
 * report), overwrite its payload with the saved bytes, and reuse
 * ajazz_build_key_table_from_query() to turn that into a SET command,
 * the same helper on_keys_apply() above uses. Re-reads the mouse
 * afterward so the dropdowns/Home readout reflect what's actually
 * there. */
gboolean buttons_apply_profile_table(App *app, const uint8_t raw[AJAZZ_KEY_SLOTS][3])
{
    if (!ensure_connected(app))
        return FALSE;

    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0)
        return FALSE;

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        memcpy(&reply.payload[i * 3], raw[i], 3);

    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    gboolean ok = (ajazz_send_and_wait_mt(app, &req, 1000) == 0);
    if (ok)
        refresh_buttons_delayed(app);
    return ok;
}

/* Traces the mouse-body silhouette into `cr`'s current path, as a
 * rounded-shoulders / tapered-bottom top-down shape -- the same curve
 * family as the app icon's own mouse mark (gui/icons/io.github.ajazzy.Gui.svg),
 * just re-parameterized from that SVG's 128x128 space to an arbitrary
 * (x, y, w, h) box, for visual consistency between the icon and this
 * diagram. Not a drawing of any specific AJAZZ model. */
static void trace_mouse_body(cairo_t *cr, double x, double y, double w, double h)
{
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.5 * w, y);
    cairo_curve_to(cr, x + 0.8125 * w, y, x + w, y + 0.15 * h, x + w, y + 0.40 * h);
    cairo_line_to(cr, x + w, y + 0.65 * h);
    cairo_curve_to(cr, x + w, y + 0.875 * h, x + 0.78125 * w, y + h, x + 0.5 * w, y + h);
    cairo_curve_to(cr, x + 0.21875 * w, y + h, x, y + 0.875 * h, x, y + 0.65 * h);
    cairo_line_to(cr, x, y + 0.40 * h);
    cairo_curve_to(cr, x, y + 0.15 * h, x + 0.1875 * w, y, x + 0.5 * w, y);
    cairo_close_path(cr);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

/* Brand accent (#f0564a) as 0-1 floats, matching the CSS override at the
 * top of this file -- cairo draws in raw RGBA, it can't read GTK theme
 * colours, so this is kept in sync by hand rather than looked up. */
#define AJAZZ_ACCENT_R 0.941
#define AJAZZ_ACCENT_G 0.337
#define AJAZZ_ACCENT_B 0.290

static void draw_mouse_outline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void)area; (void)data;
    double x = width * 0.14, y = 10, w = width * 0.72, h = height - 22;

    /* soft shadow: same silhouette, offset down and blurred by stacking
     * a few progressively larger/fainter copies (cairo has no blur op) */
    for (int i = 6; i >= 1; i--) {
        trace_mouse_body(cr, x - i * 0.6, y - i * 0.3 + 5, w + i * 1.2, h + i * 1.2);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.035);
        cairo_fill(cr);
    }

    trace_mouse_body(cr, x, y, w, h);
    cairo_pattern_t *body = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba(body, 0.0, 1, 1, 1, 0.09);
    cairo_pattern_add_color_stop_rgba(body, 0.45, 1, 1, 1, 0.04);
    cairo_pattern_add_color_stop_rgba(body, 1.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.10);
    cairo_set_source(cr, body);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(body);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    /* a soft highlight along the top edge, like light catching the shell */
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.14 * w, y + 0.10 * h);
    cairo_curve_to(cr, x + 0.3 * w, y + 0.01 * h, x + 0.7 * w, y + 0.01 * h, x + 0.86 * w, y + 0.10 * h);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* left/right click seam, following the same curve as the icon's stem */
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.5 * w, y + 0.03 * h);
    cairo_line_to(cr, x + 0.5 * w, y + 0.30 * h);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    /* scroll wheel: body + a couple of grooves for texture */
    double ww = w * 0.14, wh = h * 0.19, wx = x + w / 2 - ww / 2, wy = y + h * 0.03;
    rounded_rect(cr, wx, wy, ww, wh, ww * 0.42);
    cairo_pattern_t *wheel = cairo_pattern_create_linear(wx, wy, wx + ww, wy);
    cairo_pattern_add_color_stop_rgba(wheel, 0.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.55);
    cairo_pattern_add_color_stop_rgba(wheel, 0.5, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.85);
    cairo_pattern_add_color_stop_rgba(wheel, 1.0, AJAZZ_ACCENT_R, AJAZZ_ACCENT_G, AJAZZ_ACCENT_B, 0.55);
    cairo_set_source(cr, wheel);
    cairo_fill(cr);
    cairo_pattern_destroy(wheel);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 2; i++) {
        double gy = wy + wh * (0.32 * i);
        cairo_move_to(cr, wx + ww * 0.22, gy);
        cairo_line_to(cr, wx + ww * 0.78, gy);
    }
    cairo_stroke(cr);
}

/* Per-slot detail panel wrappers, and the left-column selector list --
 * both file-static since they're only ever built once (build_buttons_page()
 * runs exactly once per process) and only referenced from within this
 * file, unlike the widgets in App that other pages/callbacks reach into. */
static GtkWidget *g_key_detail_box[AJAZZ_KEY_SLOTS];
static GtkListBox *g_key_listbox;
static int g_key_selected_slot;

static void select_key_marker(App *app, int slot)
{
    g_key_selected_slot = slot;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        gboolean sel = (i == slot);
        if (app->key_marker[i]) {
            if (sel) gtk_widget_add_css_class(app->key_marker[i], "selected");
            else gtk_widget_remove_css_class(app->key_marker[i], "selected");
        }
        if (app->key_row[i]) {
            if (sel) gtk_widget_add_css_class(app->key_row[i], "accent");
            else gtk_widget_remove_css_class(app->key_row[i], "accent");
        }
        if (g_key_detail_box[i])
            gtk_widget_set_visible(g_key_detail_box[i], sel);
    }
    if (g_key_listbox) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(g_key_listbox, slot);
        if (row)
            gtk_list_box_select_row(g_key_listbox, row);
    }
    if (app->key_dropdown[slot])
        gtk_widget_grab_focus(GTK_WIDGET(app->key_dropdown[slot]));
}

static void on_marker_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    int slot = GPOINTER_TO_INT(data[1]);
    select_key_marker(app, slot);
}

static void on_key_listbox_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row)
        return;
    select_key_marker(app, gtk_list_box_row_get_index(row));
}

/* Roughly where each button sits on a typical AJAZZ mouse: left/right
 * click up front, wheel click between them, two side buttons on the
 * thumb rest. Fractions of the drawing area's width/height. */
static const double marker_pos[AJAZZ_KEY_SLOTS][2] = {
    {0.34, 0.13}, /* 1: left click   */
    {0.66, 0.13}, /* 2: right click  */
    {0.50, 0.08}, /* 3: wheel click  */
    {0.06, 0.58}, /* 4: side button, Back (rear)     */
    {0.06, 0.42}, /* 5: side button, Forward (front) */
};

static GtkWidget *build_mouse_diagram(App *app)
{
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 200, 280);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_mouse_outline, NULL, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        GtkWidget *marker = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(marker, "ajazz-marker");
        gtk_widget_add_css_class(marker, "circular");
        gtk_widget_set_halign(marker, GTK_ALIGN_START);
        gtk_widget_set_valign(marker, GTK_ALIGN_START);
        gtk_widget_set_margin_start(marker, (int)(200 * marker_pos[i][0]));
        gtk_widget_set_margin_top(marker, (int)(280 * marker_pos[i][1]));
        app->key_marker[i] = marker;

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(marker, "clicked", G_CALLBACK(on_marker_clicked), data, free_closure_data, 0);

        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), marker);
    }

    return overlay;
}

/* Popular-function tile: selects the dropdown entry whose label matches
 * `label` for the currently-selected button slot, then immediately
 * applies -- see g_key_selected_slot (kept in sync by select_key_marker())
 * and on_keys_apply() above. If the selected slot is left/right click
 * (not remappable) or has no matching entry, this is a no-op with a toast
 * rather than silently doing nothing. */
static void on_popular_function_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = ((gpointer *)user_data)[0];
    const char *label = ((gpointer *)user_data)[1];
    int slot = g_key_selected_slot;

    if (!app->key_dropdown[slot]) {
        toast(app, _("This button can't be remapped."));
        return;
    }
    for (size_t i = 0; i < N_KEY_FUNCTIONS; i++) {
        if (strcmp(key_functions[i].label, label) == 0) {
            adw_combo_row_set_selected(app->key_dropdown[slot], (guint)i);
            on_keys_apply(NULL, app);
            return;
        }
    }
    toast(app, _("That function isn't available on this button."));
}

GtkWidget *build_buttons_page(App *app)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_top(page, 12);
    gtk_widget_set_margin_bottom(page, 12);
    gtk_widget_set_margin_start(page, 12);
    gtk_widget_set_margin_end(page, 12);

    GtkWidget *prefs = adw_preferences_page_new();
    gtk_widget_set_hexpand(prefs, TRUE);

    /* header: title + subtitle + Reset all, top-right */
    AdwPreferencesGroup *header_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *header_title = gtk_label_new(_("Buttons"));
    gtk_widget_add_css_class(header_title, "title-2");
    gtk_widget_set_halign(header_title, GTK_ALIGN_START);
    GtkWidget *header_sub = gtk_label_new(_("Pick a button from the list (or click a number on the mouse) to change its function."));
    gtk_widget_add_css_class(header_sub, "dim-label");
    gtk_widget_set_halign(header_sub, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header_text), header_title);
    gtk_box_append(GTK_BOX(header_text), header_sub);
    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header_row), header_text);

    GtkWidget *header_reset = gtk_button_new_with_label(_("Reset all to default"));
    gtk_widget_add_css_class(header_reset, "destructive-action");
    gtk_widget_add_css_class(header_reset, "pill");
    gtk_widget_set_valign(header_reset, GTK_ALIGN_START);
    g_signal_connect(header_reset, "clicked", G_CALLBACK(on_keys_reset_clicked), app);
    gtk_box_append(GTK_BOX(header_row), header_reset);
    adw_preferences_group_add(header_group, header_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), header_group);

    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    const char *names[MAX_KEY_FUNCTIONS + 1];
    for (size_t i = 0; i < N_KEY_FUNCTIONS; i++)
        names[i] = key_functions[i].label;
    names[N_KEY_FUNCTIONS] = NULL;

    GtkWidget *mapping_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(mapping_card, "ajazz-card");

    /* left column: one row per physical button */
    GtkWidget *listbox = gtk_list_box_new();
    gtk_widget_add_css_class(listbox, "boxed-list");
    gtk_widget_set_size_request(listbox, 200, -1);
    g_key_listbox = GTK_LIST_BOX(listbox);
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_top(box, 8);
        gtk_widget_set_margin_bottom(box, 8);
        gtk_widget_set_margin_start(box, 10);
        gtk_widget_set_margin_end(box, 10);
        char title[48];
        snprintf(title, sizeof(title), _("Button %d"), i + 1);
        GtkWidget *title_lbl = gtk_label_new(title);
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
        GtkWidget *sub_lbl = gtk_label_new(gettext(default_button_names[i]));
        gtk_widget_add_css_class(sub_lbl, "dim-label");
        gtk_widget_add_css_class(sub_lbl, "caption");
        gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), title_lbl);
        gtk_box_append(GTK_BOX(box), sub_lbl);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(GTK_LIST_BOX(listbox), row);
    }
    g_signal_connect(listbox, "row-selected", G_CALLBACK(on_key_listbox_row_selected), app);
    gtk_box_append(GTK_BOX(mapping_card), listbox);

    /* right column: detail panel, one stacked box per slot, only the
     * selected one visible (see select_key_marker()) */
    GtkWidget *detail_stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(detail_stack, TRUE);

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_visible(detail_box, FALSE);
        g_key_detail_box[i] = detail_box;

        if (key_slot_is_lockable(i)) {
            AdwPreferencesGroup *slot_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
            AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), gettext(default_button_names[i]));
            GtkWidget *fixed = gtk_label_new(_("Fixed by the manufacturer"));
            gtk_widget_add_css_class(fixed, "dim-label");
            adw_action_row_add_suffix(row, fixed);
            adw_preferences_group_add(slot_group, GTK_WIDGET(row));
            gtk_box_append(GTK_BOX(detail_box), GTK_WIDGET(slot_group));
            app->key_row[i] = GTK_WIDGET(row);
            app->key_dropdown[i] = NULL;
            app->key_fire_box[i] = NULL;
            gtk_box_append(GTK_BOX(detail_stack), detail_box);
            continue;
        }

        AdwPreferencesGroup *slot_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

        AdwComboRow *dd = ADW_COMBO_ROW(adw_combo_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(dd), _("Function"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(dd), gettext(default_button_names[i]));
        adw_combo_row_set_model(dd, G_LIST_MODEL(gtk_string_list_new(names)));
        adw_combo_row_set_selected(dd, 0);
        app->key_dropdown[i] = dd;
        adw_preferences_group_add(slot_group, GTK_WIDGET(dd));
        app->key_row[i] = GTK_WIDGET(dd);

        AdwActionRow *interval_row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(interval_row), _("Rapid-fire interval"));
        AdwSpinRow *interval_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(5, 255, 1));
        adw_spin_row_set_value(interval_spin, 5);
        gtk_widget_set_valign(GTK_WIDGET(interval_spin), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(interval_row, GTK_WIDGET(interval_spin));
        app->key_fire_interval[i] = interval_spin;

        AdwActionRow *number_row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(number_row), _("Rapid-fire repeats"));
        AdwSpinRow *number_spin = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 255, 1));
        adw_spin_row_set_value(number_spin, 1);
        gtk_widget_set_valign(GTK_WIDGET(number_spin), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(number_row, GTK_WIDGET(number_spin));
        app->key_fire_number[i] = number_spin;

        GtkWidget *fire_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(fire_box), GTK_WIDGET(interval_row));
        gtk_box_append(GTK_BOX(fire_box), GTK_WIDGET(number_row));
        gtk_widget_set_visible(fire_box, FALSE);
        adw_preferences_group_add(slot_group, fire_box);
        app->key_fire_box[i] = fire_box;

        gtk_box_append(GTK_BOX(detail_box), GTK_WIDGET(slot_group));
        gtk_box_append(GTK_BOX(detail_stack), detail_box);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(dd, "notify::selected", G_CALLBACK(on_key_dropdown_changed), data, free_closure_data, 0);
    }
    gtk_box_append(GTK_BOX(mapping_card), detail_stack);
    adw_preferences_group_add(group, mapping_card);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), group);

    AdwPreferencesGroup *actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Apply to mouse"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    gtk_widget_set_hexpand(btn_apply, TRUE);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_keys_apply), app);
    adw_preferences_group_add(actions_group, btn_apply);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), actions_group);

    /* Popular functions: one-click shortcuts for the selected button,
     * restricted to function kinds this protocol actually has (consumer
     * codes and rapid-fire -- see key_functions[] above). Each tile picks
     * the matching dropdown entry for whichever slot is currently
     * selected and immediately applies, same as picking it by hand and
     * clicking "Apply to mouse". */
    AdwPreferencesGroup *popular_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(popular_group, _("Popular functions"));
    adw_preferences_group_set_description(popular_group, _("Applies to whichever button is selected above."));
    GtkWidget *popular_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(popular_card, "ajazz-card");
    static const char *popular_labels[] = {
        N_("Mute"), N_("Play / Pause"), N_("Volume up"), N_("Volume down"), N_("Rapid-fire (auto-click)"),
    };
    for (guint i = 0; i < G_N_ELEMENTS(popular_labels); i++) {
        const char *tr_label = gettext(popular_labels[i]);
        GtkWidget *tile = gtk_button_new_with_label(tr_label);
        gtk_widget_add_css_class(tile, "flat");
        gtk_widget_set_hexpand(tile, TRUE);
        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = (gpointer)tr_label;
        g_signal_connect_data(tile, "clicked", G_CALLBACK(on_popular_function_clicked), data, free_closure_data, 0);
        gtk_box_append(GTK_BOX(popular_card), tile);
    }
    adw_preferences_group_add(popular_group, popular_card);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(prefs), popular_group);

    GtkWidget *diagram_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(diagram_frame, "card");
    gtk_widget_set_margin_top(diagram_frame, 20);
    gtk_widget_set_valign(diagram_frame, GTK_ALIGN_START);
    GtkWidget *diagram_pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(diagram_pad, 16);
    gtk_widget_set_margin_bottom(diagram_pad, 16);
    gtk_widget_set_margin_start(diagram_pad, 16);
    gtk_widget_set_margin_end(diagram_pad, 16);
    gtk_box_append(GTK_BOX(diagram_pad), build_mouse_diagram(app));
    gtk_frame_set_child(GTK_FRAME(diagram_frame), diagram_pad);

    gtk_box_append(GTK_BOX(page), prefs);
    gtk_box_append(GTK_BOX(page), diagram_frame);

    select_key_marker(app, 0);
    return page;
}
