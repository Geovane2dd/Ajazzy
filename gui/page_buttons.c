/*
 * Buttons: pick a button on the mouse (or in the diagram), pick what it
 * does. Every choice is written to the mouse straight away and then read
 * back, so what you see is what the mouse really has.
 *
 * The button table is nine 3-byte slots (see "The button/key mapping table"
 * in docs/reverse-engineering.md); slots 0-5 are the five buttons plus the
 * DPI button. All encodings below come from the official driver's own
 * button-table writer -- entries marked beta have not been pressed on real
 * hardware yet.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "keymap.h"
#include "../devices/registry.h"
#include "../devices/device.h"

enum { CAT_MOUSE, CAT_MEDIA, CAT_SYSTEM, CAT_KEYBOARD, CAT_MACRO, CAT_FIRE, CAT_COUNT };

typedef struct {
    const char *label;
    int cat;
    uint8_t b[3];
    gboolean beta;
} BtnFn;

static const BtnFn FUNCS[] = {
    /* mouse */
    {N_("Left click"),      CAT_MOUSE, {0x10, 0x01, 0x00}, FALSE},
    {N_("Right click"),     CAT_MOUSE, {0x10, 0x02, 0x00}, FALSE},
    {N_("Middle click"),    CAT_MOUSE, {0x10, 0x04, 0x00}, FALSE},
    {N_("Back"),            CAT_MOUSE, {0x10, 0x08, 0x00}, FALSE},
    {N_("Forward"),         CAT_MOUSE, {0x10, 0x10, 0x00}, FALSE},
    {N_("Double click"),    CAT_MOUSE, {0x30, 0x32, 0x03}, TRUE},
    {N_("Scroll up"),       CAT_MOUSE, {0x20, 0x01, 0x00}, TRUE},
    {N_("Scroll down"),     CAT_MOUSE, {0x20, 0xff, 0x00}, TRUE},
    {N_("Scroll left"),     CAT_MOUSE, {0x10, 0x00, 0xf5}, TRUE},
    {N_("Scroll right"),    CAT_MOUSE, {0x10, 0x00, 0xf6}, TRUE},
    {N_("DPI cycle"),       CAT_MOUSE, {0x40, 0x01, 0x00}, FALSE},
    {N_("DPI up"),          CAT_MOUSE, {0x40, 0x02, 0x00}, TRUE},
    {N_("DPI down"),        CAT_MOUSE, {0x40, 0x03, 0x00}, TRUE},
    {N_("Disabled"),        CAT_MOUSE, {0x60, 0x00, 0x00}, FALSE},
    /* media */
    {N_("Play / Pause"),    CAT_MEDIA, {0x80, 0xcd, 0x00}, FALSE},
    {N_("Stop"),            CAT_MEDIA, {0x80, 0xb7, 0x00}, FALSE},
    {N_("Previous track"),  CAT_MEDIA, {0x80, 0xb6, 0x00}, FALSE},
    {N_("Next track"),      CAT_MEDIA, {0x80, 0xb5, 0x00}, FALSE},
    {N_("Volume up"),       CAT_MEDIA, {0x80, 0xe9, 0x00}, FALSE},
    {N_("Volume down"),     CAT_MEDIA, {0x80, 0xea, 0x00}, FALSE},
    {N_("Mute"),            CAT_MEDIA, {0x80, 0xe2, 0x00}, FALSE},
    {N_("Media player"),    CAT_MEDIA, {0x80, 0x83, 0x01}, TRUE},
    /* system */
    {N_("Screen brightness +"), CAT_SYSTEM, {0x80, 0x6f, 0x00}, TRUE},
    {N_("Screen brightness \xE2\x88\x92"), CAT_SYSTEM, {0x80, 0x70, 0x00}, TRUE},
    {N_("Calculator"),      CAT_SYSTEM, {0x80, 0x92, 0x01}, TRUE},
    {N_("My computer"),     CAT_SYSTEM, {0x80, 0x94, 0x01}, TRUE},
    {N_("E-mail"),          CAT_SYSTEM, {0x80, 0x8a, 0x01}, TRUE},
    {N_("Browser home"),    CAT_SYSTEM, {0x80, 0x23, 0x02}, FALSE},
    {N_("Browser back"),    CAT_SYSTEM, {0x80, 0x24, 0x02}, FALSE},
    {N_("Browser forward"), CAT_SYSTEM, {0x80, 0x25, 0x02}, FALSE},
    {N_("Refresh page"),    CAT_SYSTEM, {0x80, 0x27, 0x02}, FALSE},
    {N_("Bookmarks"),       CAT_SYSTEM, {0x80, 0x2a, 0x02}, FALSE},
    {N_("Show desktop"),    CAT_SYSTEM, {0x70, 0x08, 0x07}, TRUE},
    {N_("Switch application"), CAT_SYSTEM, {0x70, 0x08, 0x2b}, TRUE},
    {N_("Copy"),            CAT_SYSTEM, {0x70, 0x01, 0x06}, TRUE},
    {N_("Paste"),           CAT_SYSTEM, {0x70, 0x01, 0x19}, TRUE},
    {N_("Cut"),             CAT_SYSTEM, {0x70, 0x01, 0x1b}, TRUE},
};
#define N_FUNCS G_N_ELEMENTS(FUNCS)

static const char *const CAT_NAMES[CAT_COUNT] = {
    N_("Mouse"), N_("Media"), N_("System"), N_("Keyboard"), N_("Macro"), N_("Rapid-fire"),
};

static const char *const DEFAULT_NAMES[AJAZZ_KEY_SLOTS] = {
    N_("Left click"), N_("Right click"), N_("Middle click"), N_("Back"), N_("Forward"), N_("DPI button"),
};

typedef struct {
    GtkToggleButton *marker[AJAZZ_KEY_SLOTS];
    GtkWidget *title, *subtitle, *chip;
    GtkWidget *cat_seg, *cat_stack;
    GtkWidget *fn_btn[N_FUNCS];
    GtkWidget *kb_label, *kb_btn;
    GtkWidget *macro_combo_box;
    AdwComboRow *macro_combo;
    GtkWidget *macro_status;
    GtkWidget *fire_btn;
    AdwSpinRow *fire_interval, *fire_count;
    GtkWidget *panel, *locked_note, *reset_btn;
    GtkWidget *diagram;
    UiDebounce *fire_deb;
    GtkWidget *home_rows[AJAZZ_KEY_SLOTS];
} BtnUI;

static BtnUI B;
static int g_sel;
static gboolean loading;
static ajazz_report_t g_raw;
static gboolean g_have;
static gboolean g_warned_right;

/* ---------------- decoding ---------------- */

static const BtnFn *find_fn(const uint8_t *s)
{
    for (size_t i = 0; i < N_FUNCS; i++)
        if (memcmp(FUNCS[i].b, s, 3) == 0)
            return &FUNCS[i];
    return NULL;
}

static void key_name(uint8_t mods, uint8_t key, char *out, size_t n)
{
    keymap_name(mods, key, out, n);
}

static void describe(int slot, const uint8_t *s, char *out, size_t n, int *cat)
{
    (void)slot;
    *cat = CAT_MOUSE;
    const BtnFn *f = find_fn(s);
    if (f) {
        g_strlcpy(out, gettext(f->label), n);
        *cat = f->cat;
        return;
    }
    switch (s[0]) {
        case 0x30:
            snprintf(out, n, _("Rapid-fire (every %d ms)"), s[1]);
            *cat = CAT_FIRE;
            return;
        case 0x70: {
            char k[64];
            key_name(s[1], s[2], k, sizeof(k));
            snprintf(out, n, _("Key: %s"), k);
            *cat = CAT_KEYBOARD;
            return;
        }
        case 0x90:
            snprintf(out, n, _("Macro"));
            *cat = CAT_MACRO;
            return;
    }
    snprintf(out, n, _("Custom (0x%02X)"), s[0]);
}

/* ---------------- writing ---------------- */

typedef struct {
    ajazz_report_t req;
    gboolean ok;
    ajazz_report_t reply;
    gboolean reply_ok;
} ApplyJob;

static void apply_work(App *app, gpointer d)
{
    ApplyJob *j = d;
    j->ok = ajazz_send_and_wait_mt(app, &j->req, 1000) == 0;
    if (!j->ok)
        return;
    g_usleep(250 * 1000); /* give the firmware a moment before reading back */
    j->reply_ok = ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &j->reply, 1000) == 0;
}

static void update_ui(App *app);

static void apply_done(App *app, gpointer d)
{
    ApplyJob *j = d;
    if (!j->ok)
        toast(app, _("Couldn't change the button."));
    if (j->reply_ok) {
        g_raw = j->reply;
        g_have = TRUE;
        for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
            memcpy(app->key_table[i], &g_raw.payload[i * 3], 3);
        app->key_table_valid = TRUE;
    }
    update_ui(app);
}

static void write_slot(App *app, int slot, const uint8_t b[3])
{
    if (!g_have || !app->connected) {
        toast(app, _("Waiting for the mouse\xE2\x80\xA6"));
        return;
    }
    ApplyJob *j = g_new0(ApplyJob, 1);
    ajazz_build_key_table_from_query(&j->req, &g_raw);
    memcpy(&j->req.payload[slot * 3], b, 3);
    ajazz_finalize(&j->req);
    /* optimistic: reflect it now, the read-back confirms or corrects it */
    memcpy(&g_raw.payload[slot * 3], b, 3);
    memcpy(app->key_table[slot], b, 3);
    update_ui(app);
    app_async(app, apply_work, apply_done, j, g_free);
}

void buttons_describe(App *app, int slot, char *out, size_t n)
{
    if (!app->key_table_valid || slot < 0 || slot >= AJAZZ_KEY_SLOTS) {
        g_strlcpy(out, gettext(DEFAULT_NAMES[slot < 0 || slot >= AJAZZ_KEY_SLOTS ? 0 : slot]), n);
        return;
    }
    int cat;
    describe(slot, app->key_table[slot], out, n, &cat);
    if (app->key_table[slot][0] == 0x90 && app->macro_slot_name[slot][0])
        snprintf(out, n, _("Macro: %s"), app->macro_slot_name[slot]);
}

gboolean buttons_apply_table(App *app, const uint8_t raw[AJAZZ_KEY_SLOTS][3])
{
    if (!g_have || !app->connected)
        return FALSE;
    ApplyJob *j = g_new0(ApplyJob, 1);
    ajazz_build_key_table_from_query(&j->req, &g_raw);
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        memcpy(&j->req.payload[i * 3], raw[i], 3);
    ajazz_finalize(&j->req);
    app_async(app, apply_work, apply_done, j, g_free);
    return TRUE;
}

/* ---------------- UI state ---------------- */

static void update_ui(App *app)
{
    loading = TRUE;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        gtk_toggle_button_set_active(B.marker[i], i == g_sel);

    const uint8_t *cur = g_have ? &g_raw.payload[g_sel * 3] : NULL;
    char t[64], desc[96] = "";
    snprintf(t, sizeof(t), _("Button %d"), g_sel + 1);
    gtk_label_set_text(GTK_LABEL(B.title), t);

    int cat = CAT_MOUSE;
    gboolean is_default = FALSE;
    if (cur) {
        describe(g_sel, cur, desc, sizeof(desc), &cat);
        if (cur[0] == 0x90) {
            const char *mn = app->macro_slot_name[g_sel];
            snprintf(desc, sizeof(desc), mn[0] ? _("Macro: %s") : _("Macro"), mn);
        }
        uint8_t def[3] = {0x10, (uint8_t)(1u << g_sel), 0};
        if (g_sel == 5) { def[0] = 0x40; def[1] = 0x01; }
        is_default = memcmp(def, cur, 3) == 0;
    }
    gtk_label_set_text(GTK_LABEL(B.subtitle), cur ? desc : _("Reading from the mouse\xE2\x80\xA6"));
    ui_chip_set(B.chip, is_default ? _("Default") : _("Custom"), is_default ? UI_CHIP_NEUTRAL : UI_CHIP_ACCENT);
    gtk_widget_set_visible(B.chip, cur != NULL);

    /* function buttons */
    for (size_t i = 0; i < N_FUNCS; i++)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(B.fn_btn[i]), cur && memcmp(FUNCS[i].b, cur, 3) == 0);

    /* keyboard */
    if (cur && cur[0] == 0x70 && !find_fn(cur)) {
        char k[64];
        key_name(cur[1], cur[2], k, sizeof(k));
        gtk_label_set_text(GTK_LABEL(B.kb_label), k);
    } else {
        gtk_label_set_text(GTK_LABEL(B.kb_label), _("No key assigned"));
    }

    /* rapid-fire */
    gboolean fire = cur && cur[0] == 0x30 && !find_fn(cur);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(B.fire_btn), fire);
    if (fire) {
        adw_spin_row_set_value(B.fire_interval, cur[1]);
        adw_spin_row_set_value(B.fire_count, cur[2]);
    }

    /* macro */
    gtk_label_set_text(GTK_LABEL(B.macro_status),
        (cur && cur[0] == 0x90) ? _("This button plays a macro.") : _("This button has no macro."));

    /* left click can't be changed */
    gboolean locked = (g_sel == 0);
    gtk_widget_set_sensitive(B.cat_seg, !locked);
    gtk_widget_set_sensitive(B.cat_stack, !locked);
    gtk_widget_set_sensitive(B.reset_btn, !locked && !is_default);
    gtk_widget_set_visible(B.locked_note, locked);

    if (cur)
        ui_segmented_set(B.cat_seg, cat);
    gtk_stack_set_visible_child_name(GTK_STACK(B.cat_stack), (char[]){(char)('0' + cat), 0});
    loading = FALSE;
}

/* ---------------- handlers ---------------- */

static void select_slot(App *app, int slot)
{
    g_sel = slot;
    update_ui(app);
}

static void on_marker(GtkToggleButton *b, gpointer user_data)
{
    gpointer *d = user_data;
    App *app = d[0];
    int slot = GPOINTER_TO_INT(d[1]);
    if (loading) return;
    if (!gtk_toggle_button_get_active(b)) {
        gtk_toggle_button_set_active(b, TRUE);
        return;
    }
    select_slot(app, slot);
}

static void on_cat(int index, gpointer user_data)
{
    (void)user_data;
    if (loading) return;
    gtk_stack_set_visible_child_name(GTK_STACK(B.cat_stack), (char[]){(char)('0' + index), 0});
}

static void do_pick_fn(App *app, const BtnFn *f)
{
    write_slot(app, g_sel, f->b);
}

static App *g_app_for_buttons;

static void on_right_click_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    const BtnFn *f = user_data;
    if (strcmp(resp, "go") == 0) {
        g_warned_right = TRUE;
        do_pick_fn(g_app_for_buttons, f);
    }
}

static void on_fn_toggled(GtkToggleButton *b, gpointer user_data)
{
    gpointer *d = user_data;
    App *app = d[0];
    const BtnFn *f = &FUNCS[GPOINTER_TO_INT(d[1])];
    if (loading) return;
    if (!gtk_toggle_button_get_active(b)) {
        update_ui(app); /* a function button can't be "unselected" -- pick another instead */
        return;
    }
    /* removing the right-click from its own button is the classic way to
     * lock yourself out of a UI, so ask once */
    if (g_sel == 1 && !g_warned_right && f->b[0] != 0x10) {
        AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Change the right button?"),
            _("You'll lose the right click on this mouse until you reset the button.")));
        adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
        adw_alert_dialog_add_response(dlg, "go", _("Change it"));
        adw_alert_dialog_set_response_appearance(dlg, "go", ADW_RESPONSE_DESTRUCTIVE);
        adw_alert_dialog_set_close_response(dlg, "cancel");
        g_signal_connect(dlg, "response", G_CALLBACK(on_right_click_response), (gpointer)f);
        adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
        update_ui(app);
        return;
    }
    do_pick_fn(app, f);
}

static void on_reset_slot(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    uint8_t def[3] = {0x10, (uint8_t)(1u << g_sel), 0};
    if (g_sel == 5) { def[0] = 0x40; def[1] = 0x01; }
    write_slot(app, g_sel, def);
}

static void on_reset_all_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    App *app = user_data;
    if (strcmp(resp, "reset") != 0) return;
    ApplyJob *j = g_new0(ApplyJob, 1);
    ajazz_build_reset_keys(&j->req);
    app_async(app, apply_work, apply_done, j, g_free);
}

static void on_reset_all(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Reset every button?"),
        _("All buttons go back to their factory function.")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "reset", _("Reset all"));
    adw_alert_dialog_set_response_appearance(dlg, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_reset_all_response), app);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
}

/* ---------------- keyboard shortcut recorder ---------------- */

typedef struct {
    App *app;
    GtkWidget *label;
    AdwAlertDialog *dlg;
    uint8_t mods, key;
    gboolean have;
} Recorder;

static uint8_t mods_from_state(GdkModifierType st)
{
    uint8_t m = 0;
    if (st & GDK_CONTROL_MASK) m |= 0x01;
    if (st & GDK_SHIFT_MASK) m |= 0x02;
    if (st & GDK_ALT_MASK) m |= 0x04;
    if (st & GDK_SUPER_MASK) m |= 0x08;
    return m;
}

static gboolean on_rec_key(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType st, gpointer user_data)
{
    (void)c; (void)keyval;
    Recorder *r = user_data;
    if (keymap_modifier_bit(keycode))
        return TRUE; /* wait for the real key; modifiers come from the state mask */
    uint8_t hid = keymap_hid_from_gdk(keycode);
    if (!hid)
        return TRUE;
    r->mods = mods_from_state(st);
    r->key = hid;
    r->have = TRUE;
    char name[64];
    key_name(r->mods, r->key, name, sizeof(name));
    gtk_label_set_text(GTK_LABEL(r->label), name);
    adw_alert_dialog_set_response_enabled(r->dlg, "save", TRUE);
    return TRUE;
}

static void on_rec_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    Recorder *r = user_data;
    if (strcmp(resp, "save") == 0 && r->have) {
        uint8_t b[3] = {0x70, r->mods, r->key};
        write_slot(r->app, g_sel, b);
    }
    g_free(r);
}

static void on_record_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    Recorder *r = g_new0(Recorder, 1);
    r->app = app;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Press the shortcut"),
        _("Press the key (with any Ctrl / Shift / Alt / Super) you want this button to send.")));
    r->dlg = dlg;
    r->label = gtk_label_new(_("Waiting for a key\xE2\x80\xA6"));
    gtk_widget_add_css_class(r->label, "title-1");
    gtk_widget_set_margin_top(r->label, 8);
    gtk_widget_set_margin_bottom(r->label, 8);
    gtk_widget_set_focusable(r->label, TRUE);
    adw_alert_dialog_set_extra_child(dlg, r->label);
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "save", _("Use this key"));
    adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_response_enabled(dlg, "save", FALSE);
    adw_alert_dialog_set_close_response(dlg, "cancel");

    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_rec_key), r);
    gtk_widget_add_controller(GTK_WIDGET(dlg), kc);

    g_signal_connect(dlg, "response", G_CALLBACK(on_rec_response), r);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
    gtk_widget_grab_focus(r->label);
}

/* ---------------- rapid-fire ---------------- */

static gboolean fire_fire(gpointer user_data)
{
    App *app = user_data;
    uint8_t b[3] = {0x30, (uint8_t)adw_spin_row_get_value(B.fire_interval), (uint8_t)adw_spin_row_get_value(B.fire_count)};
    write_slot(app, g_sel, b);
    return G_SOURCE_REMOVE;
}

static void on_fire_toggled(GtkToggleButton *b, gpointer user_data)
{
    App *app = user_data;
    if (loading) return;
    if (gtk_toggle_button_get_active(b))
        fire_fire(app);
    else
        update_ui(app);
}

static void on_fire_value(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)row; (void)p; (void)user_data;
    if (loading) return;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(B.fire_btn)))
        ui_debounce_poke(B.fire_deb);
}

/* ---------------- macro assignment ---------------- */

static void rebuild_macro_combo(App *app)
{
    GPtrArray *lib = app->macro_lib;
    GtkStringList *sl = gtk_string_list_new(NULL);
    gtk_string_list_append(sl, _("Choose a macro\xE2\x80\xA6"));
    for (guint i = 0; lib && i < lib->len; i++)
        gtk_string_list_append(sl, ((AppMacro *)g_ptr_array_index(lib, i))->name);
    loading = TRUE;
    adw_combo_row_set_model(B.macro_combo, G_LIST_MODEL(sl));
    adw_combo_row_set_selected(B.macro_combo, 0);
    loading = FALSE;
}

static void on_macro_pick(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p;
    App *app = user_data;
    if (loading) return;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    if (sel == 0 || !app->macro_lib || sel - 1 >= app->macro_lib->len)
        return;
    macros_assign(app, g_sel, g_ptr_array_index(app->macro_lib, sel - 1));
    loading = TRUE;
    adw_combo_row_set_selected(B.macro_combo, 0);
    loading = FALSE;
}

static void on_open_macros(GtkButton *b, gpointer user_data)
{
    (void)b;
    switch_to_page(user_data, "macros");
}

/* ---------------- diagram ---------------- */

static void trace_body(cairo_t *cr, double x, double y, double w, double h)
{
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.5 * w, y);
    cairo_curve_to(cr, x + 0.82 * w, y, x + w, y + 0.15 * h, x + w, y + 0.40 * h);
    cairo_line_to(cr, x + w, y + 0.66 * h);
    cairo_curve_to(cr, x + w, y + 0.88 * h, x + 0.78 * w, y + h, x + 0.5 * w, y + h);
    cairo_curve_to(cr, x + 0.22 * w, y + h, x, y + 0.88 * h, x, y + 0.66 * h);
    cairo_line_to(cr, x, y + 0.40 * h);
    cairo_curve_to(cr, x, y + 0.15 * h, x + 0.18 * w, y, x + 0.5 * w, y);
    cairo_close_path(cr);
}

static void draw_diagram(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void)area; (void)data;
    double w = width * 0.62, h = height - 24, x = (width - w) / 2, y = 12;

    trace_body(cr, x, y, w, h);
    cairo_pattern_t *p = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba(p, 0, 1, 1, 1, 0.13);
    cairo_pattern_add_color_stop_rgba(p, 0.6, 1, 1, 1, 0.05);
    cairo_pattern_add_color_stop_rgba(p, 1, 0.91, 0.22, 0.29, 0.16);
    cairo_set_source(cr, p);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(p);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    /* left / right seam */
    cairo_move_to(cr, x + w / 2, y + 0.02 * h);
    cairo_line_to(cr, x + w / 2, y + 0.32 * h);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
    cairo_stroke(cr);

    /* wheel */
    double ww = w * 0.13, wh = h * 0.17, wx = x + w / 2 - ww / 2, wy = y + h * 0.05;
    cairo_set_source_rgba(cr, 0.91, 0.22, 0.29, 0.8);
    cairo_new_sub_path(cr);
    cairo_arc(cr, wx + ww / 2, wy + ww / 2, ww / 2, G_PI, 0);
    cairo_arc(cr, wx + ww / 2, wy + wh - ww / 2, ww / 2, 0, G_PI);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* Where each of the six buttons sits on each product photo, in pixels of the
 * photo itself (slot order: left, right, middle, back, forward, DPI). Read
 * off the pictures in gui/images/; the wheel is the middle button, the DPI button is the
 * small oval/slot below it where the model has one on top. */
typedef struct {
    const char *stem;
    int w, h;
    int pos[AJAZZ_KEY_SLOTS][2];
} PhotoLayout;

static const PhotoLayout PHOTOS[] = {
    {"aj179",    260, 500, {{80, 150}, {175, 150}, {130, 125}, {28, 270}, {28, 210}, {130, 187}}},
    {"aj139pro", 260, 500, {{85, 150}, {170, 150}, {128, 125}, {30, 240}, {30, 195}, {128, 193}}},
    {"aj159",    260, 500, {{85, 140}, {170, 140}, {123, 125}, {22, 272}, {22, 215}, {128, 230}}},
    {"generic",  215, 352, {{75, 110}, {150, 110}, {113, 75},  {25, 190}, {25, 140}, {113, 152}}},
};

static const PhotoLayout *photo_layout(const char *stem)
{
    for (guint i = 0; i < G_N_ELEMENTS(PHOTOS); i++)
        if (strcmp(PHOTOS[i].stem, stem) == 0)
            return &PHOTOS[i];
    return &PHOTOS[G_N_ELEMENTS(PHOTOS) - 1];
}

#define DIAGRAM_H 400

static GtkWidget *build_diagram(App *app, const char *stem)
{
    const PhotoLayout *lay = photo_layout(stem);
    double scale = (double)DIAGRAM_H / lay->h;
    int w = (int)(lay->w * scale);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_add_css_class(overlay, "aj-photo");

    GtkWidget *pic = NULL;
    GdkTexture *tex = ui_mouse_texture(lay->stem, w, DIAGRAM_H);
    if (tex) {
        pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(tex));
        gtk_picture_set_can_shrink(GTK_PICTURE(pic), FALSE);
        g_object_unref(tex);
    }
    if (!pic) { /* photo not installed: fall back to the drawn silhouette */
        pic = gtk_drawing_area_new();
        gtk_widget_set_size_request(pic, w, DIAGRAM_H);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(pic), draw_diagram, NULL, NULL);
    }
    gtk_widget_set_halign(pic, GTK_ALIGN_START);
    gtk_widget_set_valign(pic, GTK_ALIGN_START);
    gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(overlay, GTK_ALIGN_START);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), pic);

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char l[4];
        snprintf(l, sizeof(l), "%d", i + 1);
        GtkWidget *m = gtk_toggle_button_new_with_label(l);
        gtk_widget_add_css_class(m, "aj-marker");
        gtk_widget_set_halign(m, GTK_ALIGN_START);
        gtk_widget_set_valign(m, GTK_ALIGN_START);
        gtk_widget_set_margin_start(m, (int)(lay->pos[i][0] * scale) - 15);
        gtk_widget_set_margin_top(m, (int)(lay->pos[i][1] * scale) - 15);
        char tip[48];
        snprintf(tip, sizeof(tip), "%s \xE2\x80\xA2 %s", l, gettext(DEFAULT_NAMES[i]));
        gtk_widget_set_tooltip_text(m, tip);
        B.marker[i] = GTK_TOGGLE_BUTTON(m);
        gpointer *d = g_new(gpointer, 2);
        d[0] = app; d[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(m, "toggled", G_CALLBACK(on_marker), d, free_closure_data, 0);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), m);
    }
    return overlay;
}

/* shows the photo that matches the connected model (and its button layout) */
static char g_diagram_stem[16];

static void rebuild_diagram(App *app)
{
    const char *stem = app->connected ? ajazz_lookup_image(app->dev.name) : "generic";
    if (strcmp(stem, g_diagram_stem) == 0 && B.diagram)
        return;
    g_strlcpy(g_diagram_stem, stem, sizeof(g_diagram_stem));
    GtkWidget *holder = gtk_widget_get_parent(B.diagram);
    GtkWidget *old = B.diagram;
    loading = TRUE;
    B.diagram = build_diagram(app, stem);
    gtk_widget_set_halign(B.diagram, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(B.diagram, 14);
    gtk_widget_set_margin_bottom(B.diagram, 6);
    gtk_box_remove(GTK_BOX(holder), old);
    gtk_box_prepend(GTK_BOX(holder), B.diagram);
    loading = FALSE;
}

/* ---------------- refresh ---------------- */

typedef struct { gboolean ok; ajazz_report_t reply; } BFetch;

static void bfetch_work(App *app, gpointer d)
{
    BFetch *f = d;
    f->ok = ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &f->reply, 1000) == 0;
}

static void bfetch_done(App *app, gpointer d)
{
    BFetch *f = d;
    if (!app->connected || !f->ok)
        return;
    g_raw = f->reply;
    g_have = TRUE;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        memcpy(app->key_table[i], &g_raw.payload[i * 3], 3);
    app->key_table_valid = TRUE;
    rebuild_macro_combo(app);
    rebuild_diagram(app);
    update_ui(app);
    refresh_home(app);
}

void refresh_buttons(App *app)
{
    if (!app->connected)
        return;
    rebuild_diagram(app); /* the photo only depends on the model, not on a reply */
    app_async(app, bfetch_work, bfetch_done, g_new0(BFetch, 1), g_free);
}

/* ---------------- page ---------------- */

static GtkWidget *fn_grid(App *app, int cat)
{
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    for (size_t i = 0; i < N_FUNCS; i++) {
        if (FUNCS[i].cat != cat) continue;
        GtkWidget *b = gtk_toggle_button_new();
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(row, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row), gtk_label_new(gettext(FUNCS[i].label)));
        if (FUNCS[i].beta) {
            GtkWidget *chip = ui_chip_new("beta", UI_CHIP_WARN);
            gtk_widget_set_tooltip_text(chip, _("Taken from the official driver but not yet tested on real hardware."));
            gtk_box_append(GTK_BOX(row), chip);
        }
        gtk_button_set_child(GTK_BUTTON(b), row);
        gtk_widget_add_css_class(b, "aj-func");
        B.fn_btn[i] = b;
        gpointer *d = g_new(gpointer, 2);
        d[0] = app; d[1] = GINT_TO_POINTER((int)i);
        g_signal_connect_data(b, "toggled", G_CALLBACK(on_fn_toggled), d, free_closure_data, 0);
        gtk_flow_box_append(GTK_FLOW_BOX(flow), b);
    }
    return flow;
}

GtkWidget *build_buttons_page(App *app)
{
    g_app_for_buttons = app;
    GtkWidget *reset_all = ui_button_new(_("Reset all"), "edit-undo-symbolic", "secondary");
    g_signal_connect(reset_all, "clicked", G_CALLBACK(on_reset_all), app);

    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Buttons"),
        _("Pick a button, then choose what it does. Changes are written to the mouse immediately."), reset_all, &body);
    B.fire_deb = ui_debounce_new(400, fire_fire, app);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(columns), FALSE);

    /* left: diagram */
    GtkWidget *dcb;
    GtkWidget *dcard = ui_card_new(NULL, NULL, &dcb);
    g_strlcpy(g_diagram_stem, "generic", sizeof(g_diagram_stem));
    B.diagram = build_diagram(app, "generic");
    gtk_widget_set_halign(B.diagram, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(B.diagram, 14);
    gtk_widget_set_margin_bottom(B.diagram, 6);
    gtk_box_append(GTK_BOX(dcb), B.diagram);
    GtkWidget *hint = gtk_label_new(_("Click a number to select that button."));
    gtk_widget_add_css_class(hint, "aj-card-desc");
    gtk_widget_set_margin_bottom(hint, 14);
    gtk_box_append(GTK_BOX(dcb), hint);
    gtk_widget_set_valign(dcard, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(columns), dcard);

    /* right: the editor for the selected button */
    GtkWidget *pcb;
    GtkWidget *pcard = ui_card_new(NULL, NULL, &pcb);
    gtk_widget_set_hexpand(pcard, TRUE);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(head, "aj-card-head");
    GtkWidget *ht = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(ht, TRUE);
    B.title = gtk_label_new("");
    gtk_widget_add_css_class(B.title, "aj-card-title");
    gtk_widget_set_halign(B.title, GTK_ALIGN_START);
    B.subtitle = gtk_label_new("");
    gtk_widget_add_css_class(B.subtitle, "aj-card-desc");
    gtk_widget_set_halign(B.subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(ht), B.title);
    gtk_box_append(GTK_BOX(ht), B.subtitle);
    gtk_box_append(GTK_BOX(head), ht);
    B.chip = ui_chip_new(_("Default"), UI_CHIP_NEUTRAL);
    gtk_box_append(GTK_BOX(head), B.chip);
    gtk_box_append(GTK_BOX(pcb), head);

    B.locked_note = gtk_label_new(_("The left click is locked so you can't lose control of the mouse."));
    gtk_widget_add_css_class(B.locked_note, "aj-callout");
    gtk_widget_add_css_class(B.locked_note, "aj-callout-info");
    gtk_label_set_wrap(GTK_LABEL(B.locked_note), TRUE);
    gtk_widget_set_margin_start(B.locked_note, 18);
    gtk_widget_set_margin_end(B.locked_note, 18);
    gtk_box_append(GTK_BOX(pcb), B.locked_note);

    const char *cats[CAT_COUNT];
    for (int i = 0; i < CAT_COUNT; i++) cats[i] = gettext(CAT_NAMES[i]);
    B.cat_seg = ui_segmented_new(cats, CAT_COUNT, 0, on_cat, NULL);
    gtk_widget_set_margin_start(B.cat_seg, 18);
    gtk_widget_set_margin_end(B.cat_seg, 18);
    gtk_widget_set_margin_top(B.cat_seg, 8);
    gtk_widget_set_halign(B.cat_seg, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(pcb), B.cat_seg);

    B.cat_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(B.cat_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_vhomogeneous(GTK_STACK(B.cat_stack), FALSE);

    for (int c = CAT_MOUSE; c <= CAT_SYSTEM; c++)
        gtk_stack_add_named(GTK_STACK(B.cat_stack), ui_card_pad(fn_grid(app, c)), (char[]){(char)('0' + c), 0});

    /* keyboard */
    GtkWidget *kb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    B.kb_label = gtk_label_new("");
    gtk_widget_add_css_class(B.kb_label, "title-2");
    gtk_widget_set_halign(B.kb_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(kb), B.kb_label);
    B.kb_btn = ui_button_new(_("Record a shortcut\xE2\x80\xA6"), "input-keyboard-symbolic", "primary");
    gtk_widget_set_halign(B.kb_btn, GTK_ALIGN_START);
    g_signal_connect(B.kb_btn, "clicked", G_CALLBACK(on_record_clicked), app);
    gtk_box_append(GTK_BOX(kb), B.kb_btn);
    GtkWidget *kbn = gtk_label_new(_("The button will press this key combination, e.g. Ctrl + Shift + M."));
    gtk_widget_add_css_class(kbn, "aj-card-desc");
    gtk_widget_set_halign(kbn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(kb), kbn);
    gtk_stack_add_named(GTK_STACK(B.cat_stack), ui_card_pad(kb), (char[]){(char)('0' + CAT_KEYBOARD), 0});

    /* macro */
    GtkWidget *mc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    B.macro_status = gtk_label_new("");
    gtk_widget_set_halign(B.macro_status, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(mc), B.macro_status);
    GtkWidget *mlist = ui_list_new();
    B.macro_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(B.macro_combo), _("Play this macro"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(B.macro_combo), _("Pick one from your library to put it on this button."));
    g_signal_connect(B.macro_combo, "notify::selected", G_CALLBACK(on_macro_pick), app);
    gtk_list_box_append(GTK_LIST_BOX(mlist), GTK_WIDGET(B.macro_combo));
    gtk_box_append(GTK_BOX(mc), mlist);
    GtkWidget *om = ui_button_new(_("Open the macro editor"), "input-keyboard-symbolic", "secondary");
    gtk_widget_set_halign(om, GTK_ALIGN_START);
    g_signal_connect(om, "clicked", G_CALLBACK(on_open_macros), app);
    gtk_box_append(GTK_BOX(mc), om);
    gtk_stack_add_named(GTK_STACK(B.cat_stack), ui_card_pad(mc), (char[]){(char)('0' + CAT_MACRO), 0});

    /* rapid-fire */
    GtkWidget *rf = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    B.fire_btn = gtk_toggle_button_new_with_label(_("Rapid-fire on this button"));
    gtk_widget_add_css_class(B.fire_btn, "aj-func");
    gtk_widget_set_halign(B.fire_btn, GTK_ALIGN_START);
    g_signal_connect(B.fire_btn, "toggled", G_CALLBACK(on_fire_toggled), app);
    gtk_box_append(GTK_BOX(rf), B.fire_btn);
    GtkWidget *flist = ui_list_new();
    B.fire_interval = ADW_SPIN_ROW(adw_spin_row_new_with_range(5, 255, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(B.fire_interval), _("Interval (ms)"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(B.fire_interval), _("Smaller is faster."));
    adw_spin_row_set_value(B.fire_interval, 50);
    g_signal_connect(B.fire_interval, "notify::value", G_CALLBACK(on_fire_value), NULL);
    gtk_list_box_append(GTK_LIST_BOX(flist), GTK_WIDGET(B.fire_interval));
    B.fire_count = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 255, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(B.fire_count), _("Number of clicks"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(B.fire_count), _("0 keeps clicking while the button is held."));
    adw_spin_row_set_value(B.fire_count, 0);
    g_signal_connect(B.fire_count, "notify::value", G_CALLBACK(on_fire_value), NULL);
    gtk_list_box_append(GTK_LIST_BOX(flist), GTK_WIDGET(B.fire_count));
    gtk_box_append(GTK_BOX(rf), flist);
    gtk_stack_add_named(GTK_STACK(B.cat_stack), ui_card_pad(rf), (char[]){(char)('0' + CAT_FIRE), 0});

    gtk_box_append(GTK_BOX(pcb), B.cat_stack);

    B.reset_btn = ui_button_new(_("Reset this button"), "edit-undo-symbolic", "ghost");
    gtk_widget_set_halign(B.reset_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_start(B.reset_btn, 12);
    gtk_widget_set_margin_bottom(B.reset_btn, 14);
    g_signal_connect(B.reset_btn, "clicked", G_CALLBACK(on_reset_slot), app);
    gtk_box_append(GTK_BOX(pcb), B.reset_btn);

    gtk_box_append(GTK_BOX(columns), pcard);
    gtk_box_append(GTK_BOX(body), columns);

    update_ui(app);
    return page;
}
