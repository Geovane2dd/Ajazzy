/*
 * Macro page: assign a short press+release sequence (keys and/or mouse
 * buttons, with a configurable delay) to one of the 5 physical buttons.
 * See docs/reverse-engineering.md's Macros section for the wire format,
 * including the chunked multi-packet write used past 3 actions.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>

#include "app.h"
#include "../devices/device.h"


/* macro action list: keyboard keys + mouse buttons, flat, for the macro
 * editor's per-step dropdowns. Index 0 is always "(none)". */
enum { MA_NONE, MA_KEY, MA_BUTTON };
typedef struct {
    char label[48];
    int kind;
    uint16_t code; /* HID keycode, or mouse button bit (1,2,4,8,16) */
} macro_action_t;

#define MAX_MACRO_ACTIONS 48
static macro_action_t macro_actions[MAX_MACRO_ACTIONS];
static size_t n_macro_actions;
void init_macro_actions(void)
{
    size_t i = 0;

    g_strlcpy(macro_actions[i].label, _("(none)"), sizeof(macro_actions[i].label));
    macro_actions[i].kind = MA_NONE;
    i++;

    struct { const char *label; uint16_t bit; } buttons[] = {
        {N_("Left click"),   1},
        {N_("Right click"),  2},
        {N_("Middle click"), 4},
        {N_("Button 4"),     8},
        {N_("Button 5"),     16},
    };
    for (size_t b = 0; b < G_N_ELEMENTS(buttons); b++, i++) {
        g_strlcpy(macro_actions[i].label, gettext(buttons[b].label), sizeof(macro_actions[i].label));
        macro_actions[i].kind = MA_BUTTON;
        macro_actions[i].code = buttons[b].bit;
    }

    for (char c = 'A'; c <= 'Z'; c++, i++) {
        snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), c);
        macro_actions[i].kind = MA_KEY;
        macro_actions[i].code = (uint8_t)(0x04 + (c - 'A'));
    }
    for (char c = '1'; c <= '9'; c++, i++) {
        snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), c);
        macro_actions[i].kind = MA_KEY;
        macro_actions[i].code = (uint8_t)(0x1E + (c - '1'));
    }
    snprintf(macro_actions[i].label, sizeof(macro_actions[i].label), _("Key %c"), '0');
    macro_actions[i].kind = MA_KEY;
    macro_actions[i].code = 0x27;
    i++;

    n_macro_actions = i;
}
#define N_MACRO_ACTIONS n_macro_actions
static void macro_select_slot(App *app, int slot)
{
    app->macro_slot_selected = slot;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        gtk_toggle_button_set_active(app->macro_slot_btn[i], i == slot);
}

static void on_macro_slot_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    macro_select_slot((App *)data[0], GPOINTER_TO_INT(data[1]));
}

/* Heap-allocated result of the fetch thread below, handed to
 * macro_status_fetch_apply() on the main thread via g_idle_add(). */
typedef struct {
    App *app;
    gboolean ok;
    ajazz_report_t reply;
} MacroStatusFetchResult;

static gboolean macro_status_fetch_apply(gpointer user_data)
{
    MacroStatusFetchResult *res = user_data;
    App *app = res->app;
    app->macro_fetch_pending = FALSE;

    if (!app->connected || !res->ok) {
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        const uint8_t *slot = &res->reply.payload[i * 3];
        gboolean is_macro = (slot[0] == 0x90);
        if (app->macro_status_lbl[i])
            gtk_label_set_text(app->macro_status_lbl[i],
                is_macro ? _("plays a macro") : _("default"));
    }

    g_free(res);
    return G_SOURCE_REMOVE;
}

/* Runs off the main thread -- MUST NOT touch any GTK widget, only
 * app->dev via the *_mt() wrapper (see App::hid_lock) and the heap
 * result handed to macro_status_fetch_apply(). */
static gpointer macro_status_fetch_thread(gpointer user_data)
{
    App *app = user_data;
    MacroStatusFetchResult *res = g_new0(MacroStatusFetchResult, 1);
    res->app = app;
    res->ok = (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &res->reply, 1000) == 0);
    g_idle_add(macro_status_fetch_apply, res);
    return NULL;
}

/* Called every time the Macro page becomes visible, so the "plays a
 * macro"/"default" hint under each slot is always current. Reads on a
 * background thread so a slow/unplugged mouse can't freeze the window
 * -- see macro_status_fetch_thread()/macro_status_fetch_apply() and the
 * comment on App::hid_lock. */
void refresh_macro_status(App *app)
{
    if (!app->connected)
        return;
    if (app->macro_fetch_pending)
        return;
    app->macro_fetch_pending = TRUE;
    g_thread_unref(g_thread_new("ajazzy-macro-fetch", macro_status_fetch_thread, app));
}

static void update_macro_add_button(App *app)
{
    gboolean can_add = app->macro_action_rows->len < AJAZZ_MACRO_MAX_CHUNKED_ACTIONS;
    gtk_widget_set_sensitive(app->macro_add_btn, can_add);
    gtk_widget_set_visible(app->macro_experimental_hint,
        app->macro_action_rows->len > AJAZZ_MACRO_MAX_STEPS / 2);
}

static void renumber_macro_action_rows(App *app)
{
    for (guint i = 0; i < app->macro_action_rows->len; i++) {
        AdwComboRow *row = g_ptr_array_index(app->macro_action_rows, i);
        char title[32];
        snprintf(title, sizeof(title), _("Action %u"), i + 1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    }
}

static void on_macro_action_remove_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gpointer *data = user_data;
    App *app = data[0];
    AdwComboRow *row = data[1];

    /* keep at least one row so there's always something to pick from */
    if (app->macro_action_rows->len <= 1)
        return;

    adw_preferences_group_remove(app->macro_actions_group, GTK_WIDGET(row));
    g_ptr_array_remove(app->macro_action_rows, row);
    renumber_macro_action_rows(app);
    update_macro_add_button(app);
}

/* Icon shown at the start of each action row, matching the action's kind
 * -- purely decorative (a visual "type" column), the actual action is
 * still whatever's selected in the row's dropdown. */
static const char *macro_action_icon_name(int kind)
{
    switch (kind) {
        case MA_BUTTON: return "input-mouse-symbolic";
        case MA_KEY:     return "input-keyboard-symbolic";
        default:         return "action-unavailable-symbolic";
    }
}

static void on_macro_action_kind_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    GtkWidget *icon = user_data;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(dd));
    int kind = (sel < N_MACRO_ACTIONS) ? macro_actions[sel].kind : MA_NONE;
    gtk_image_set_from_icon_name(GTK_IMAGE(icon), macro_action_icon_name(kind));
}

static AdwComboRow *add_macro_action_row(App *app)
{
    const char *action_names[MAX_MACRO_ACTIONS + 1];
    for (size_t i = 0; i < N_MACRO_ACTIONS; i++)
        action_names[i] = macro_actions[i].label;
    action_names[N_MACRO_ACTIONS] = NULL;

    AdwComboRow *dd = ADW_COMBO_ROW(adw_combo_row_new());
    adw_combo_row_set_model(dd, G_LIST_MODEL(gtk_string_list_new(action_names)));
    adw_combo_row_set_selected(dd, 0);

    /* fixed-width "type" icon column, so every row lines up like a table
     * column even though the underlying widget is still a preferences row */
    GtkWidget *icon = gtk_image_new_from_icon_name(macro_action_icon_name(MA_NONE));
    gtk_widget_set_size_request(icon, 24, -1);
    gtk_widget_add_css_class(icon, "dim-label");
    adw_action_row_add_prefix(ADW_ACTION_ROW(dd), icon);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_macro_action_kind_changed), icon);

    GtkWidget *remove_btn = gtk_button_new_from_icon_name("list-remove-symbolic");
    gtk_widget_add_css_class(remove_btn, "flat");
    gtk_widget_add_css_class(remove_btn, "circular");
    gtk_widget_set_valign(remove_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(remove_btn, _("Remove this action"));
    gpointer *data = g_new(gpointer, 2);
    data[0] = app;
    data[1] = dd;
    g_signal_connect_data(remove_btn, "clicked", G_CALLBACK(on_macro_action_remove_clicked), data, free_closure_data, 0);
    adw_action_row_add_suffix(ADW_ACTION_ROW(dd), remove_btn);

    g_ptr_array_add(app->macro_action_rows, dd);
    adw_preferences_group_add(app->macro_actions_group, GTK_WIDGET(dd));
    renumber_macro_action_rows(app);
    update_macro_add_button(app);
    return dd;
}

static void on_macro_add_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    add_macro_action_row((App *)user_data);
}

static void on_macro_apply(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    App *app = user_data;
    if (!ensure_connected(app)) { toast(app, _("Connect the mouse first.")); return; }

    ajazz_macro_step_t steps[AJAZZ_MACRO_MAX_CHUNKED_STEPS];
    int n_steps = 0;
    uint16_t delay_ms = (uint16_t)adw_spin_row_get_value(app->macro_delay);

    for (guint i = 0; i < app->macro_action_rows->len; i++) {
        AdwComboRow *row = g_ptr_array_index(app->macro_action_rows, i);
        guint sel = adw_combo_row_get_selected(row);
        if (sel >= N_MACRO_ACTIONS || macro_actions[sel].kind == MA_NONE)
            continue;
        if (macro_actions[sel].kind == MA_BUTTON) {
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_DOWN, delay_ms, (uint8_t)macro_actions[sel].code};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_MOUSE_UP,   delay_ms, (uint8_t)macro_actions[sel].code};
        } else {
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_DOWN, delay_ms, (uint8_t)macro_actions[sel].code};
            steps[n_steps++] = (ajazz_macro_step_t){AJAZZ_MACRO_KEY_UP,   delay_ms, (uint8_t)macro_actions[sel].code};
        }
    }

    if (n_steps == 0) { toast(app, _("Pick at least one action.")); return; }

    ajazz_report_t chunks[(AJAZZ_MACRO_MAX_CHUNKED_STEPS * 4 + AJAZZ_PAYLOAD_LEN) / AJAZZ_PAYLOAD_LEN + 1];
    int n_chunks = ajazz_build_macro_content_chunked(chunks, (int)(sizeof(chunks) / sizeof(chunks[0])), steps, n_steps);
    if (n_chunks < 0) { toast(app, _("Macro is too long to encode.")); return; }
    for (int i = 0; i < n_chunks; i++) {
        if (ajazz_send_and_wait_mt(app, &chunks[i], 1000) != 0) {
            toast(app, _("Failed to write macro content."));
            return;
        }
    }

    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) {
        toast(app, _("Could not read the current button table."));
        return;
    }
    ajazz_report_t assign;
    ajazz_build_key_table_from_query(&assign, &reply);
    ajazz_key_slot_set_macro(&assign, app->macro_slot_selected);

    if (ajazz_send_and_wait_mt(app, &assign, 1000) == 0) {
        toast(app, _("Macro saved to button."));
        refresh_macro_status_delayed(app);
    } else {
        toast(app, _("Failed to assign the macro to the button."));
    }
}

static void on_macro_clear(GtkButton *btn, gpointer user_data)
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
    ajazz_key_slot_reset_default(&req, app->macro_slot_selected);

    if (ajazz_send_and_wait_mt(app, &req, 1000) == 0) {
        toast(app, _("Button reset to default."));
        refresh_macro_status_delayed(app);
    } else {
        toast(app, _("Failed to reset the button."));
    }
}

GtkWidget *build_macro_page(App *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 16);
    gtk_widget_set_margin_start(outer, 16);
    gtk_widget_set_margin_end(outer, 16);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(_("Macros"));
    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *subtitle = gtk_label_new(_("Assign a short press-and-release sequence to one of the mouse's buttons."));
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header), title);
    gtk_box_append(GTK_BOX(header), subtitle);
    gtk_box_append(GTK_BOX(outer), header);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(outer), columns);

    /* left column: the action list itself, given most of the width */
    GtkWidget *left_page = adw_preferences_page_new();
    gtk_widget_set_hexpand(left_page, TRUE);
    gtk_box_append(GTK_BOX(columns), left_page);

    /* right column: which button, the shared delay, and apply/reset --
     * fixed width so it reads as a settings sidebar, matching the
     * DPI/Lighting pages' list+detail-panel layout. */
    GtkWidget *right_page = adw_preferences_page_new();
    gtk_widget_set_size_request(right_page, 300, -1);
    gtk_box_append(GTK_BOX(columns), right_page);

    AdwPreferencesGroup *slot_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(slot_group, _("Button"));
    adw_preferences_group_set_description(slot_group, _("Choose which button plays the macro on the left."));

    GtkWidget *slot_row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(slot_row_box, 6);
    gtk_widget_set_margin_bottom(slot_row_box, 6);
    gtk_widget_set_halign(slot_row_box, GTK_ALIGN_CENTER);
    GtkToggleButton *slot_group_btn = NULL;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char label[16];
        snprintf(label, sizeof(label), _("Button %d"), i + 1);
        GtkWidget *b = gtk_toggle_button_new_with_label(label);
        gtk_widget_add_css_class(b, "pill");
        if (slot_group_btn)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), slot_group_btn);
        else
            slot_group_btn = GTK_TOGGLE_BUTTON(b);
        app->macro_slot_btn[i] = GTK_TOGGLE_BUTTON(b);

        gpointer *data = g_new(gpointer, 2);
        data[0] = app;
        data[1] = GINT_TO_POINTER(i);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_macro_slot_clicked), data, free_closure_data, 0);

        GtkWidget *status = gtk_label_new(_("default"));
        gtk_widget_add_css_class(status, "dim-label");
        gtk_widget_add_css_class(status, "caption");
        app->macro_status_lbl[i] = GTK_LABEL(status);

        gtk_box_append(GTK_BOX(box), b);
        gtk_box_append(GTK_BOX(box), status);
        gtk_box_append(GTK_BOX(slot_row_box), box);
    }
    gtk_toggle_button_set_active(app->macro_slot_btn[0], TRUE);
    app->macro_slot_selected = 0;
    adw_preferences_group_add(slot_group, slot_row_box);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(right_page), slot_group);

    app->macro_actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(app->macro_actions_group, _("Macro"));
    adw_preferences_group_set_description(app->macro_actions_group,
        _("Actions are played in order, each as press-then-release, with the same delay between every step. The icon on the left shows whether a step is a keyboard key or a mouse button."));

    app->macro_add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(app->macro_add_btn, "flat");
    gtk_widget_set_valign(app->macro_add_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(app->macro_add_btn, _("Add action"));
    g_signal_connect(app->macro_add_btn, "clicked", G_CALLBACK(on_macro_add_clicked), app);
    adw_preferences_group_set_header_suffix(app->macro_actions_group, app->macro_add_btn);

    /* created before the first add_macro_action_row() call below, since
     * that call runs update_macro_add_button() which references this */
    app->macro_experimental_hint = gtk_label_new(
        _("Long macros (more than 3 actions) are less tested -- try the button after saving to make sure it plays back correctly."));
    gtk_widget_add_css_class(app->macro_experimental_hint, "warning");
    gtk_widget_add_css_class(app->macro_experimental_hint, "caption");
    gtk_label_set_wrap(GTK_LABEL(app->macro_experimental_hint), TRUE);
    gtk_widget_set_halign(app->macro_experimental_hint, GTK_ALIGN_START);
    gtk_widget_set_visible(app->macro_experimental_hint, FALSE);

    app->macro_action_rows = g_ptr_array_new();
    add_macro_action_row(app);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(left_page), app->macro_actions_group);

    AdwPreferencesGroup *delay_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_add(delay_group, app->macro_experimental_hint);

    app->macro_delay = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 65535, 5));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(app->macro_delay), _("Delay between steps"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(app->macro_delay), _("Milliseconds"));
    adw_spin_row_set_value(app->macro_delay, 10);
    adw_preferences_group_add(delay_group, GTK_WIDGET(app->macro_delay));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(right_page), delay_group);

    AdwPreferencesGroup *actions_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *btn_apply = gtk_button_new_with_label(_("Save to button"));
    gtk_widget_add_css_class(btn_apply, "suggested-action");
    gtk_widget_add_css_class(btn_apply, "pill");
    gtk_widget_add_css_class(btn_apply, "ajazz-cta");
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_macro_apply), app);
    GtkWidget *btn_clear = gtk_button_new_with_label(_("Reset button to default"));
    gtk_widget_add_css_class(btn_clear, "destructive-action");
    gtk_widget_add_css_class(btn_clear, "pill");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_macro_clear), app);
    gtk_box_append(GTK_BOX(actions), btn_apply);
    gtk_box_append(GTK_BOX(actions), btn_clear);
    adw_preferences_group_add(actions_group, actions);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(right_page), actions_group);

    return outer;
}
