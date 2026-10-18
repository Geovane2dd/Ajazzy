/*
 * Macros: a library of named macros you can record, edit, import/export and
 * put on any button.
 *
 * On the wire a macro is a stream of steps [kind, delay, value] stored in
 * the mouse itself (see "Macros" in docs/reverse-engineering.md). The mouse
 * keeps the content per button, so the library lives on this PC and a macro
 * is written to the button you assign it to. Repeat mode (play once / while
 * held / until click / N times) travels in the button-table entry.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"
#include "keymap.h"
#include "../devices/device.h"

#define MAX_STEPS AJAZZ_MACRO_MAX_CHUNKED_STEPS

typedef struct {
    GtkWidget *list;           /* library list (left) */
    GtkWidget *editor;         /* right column, hidden when nothing selected */
    GtkWidget *empty;
    GtkWidget *name_entry;
    GtkWidget *events_box;
    GtkWidget *count_chip;
    GtkWidget *loop_seg;
    AdwSpinRow *loop_count;
    AdwComboRow *slot_combo;
    GtkWidget *assign_btn;
    GtkWidget *rec_btn;
    GtkWidget *limit_note;
    AdwSpinRow *bulk_delay;
} MacroUI;

static MacroUI M;
static int g_sel = -1;
static gboolean loading;
static App *g_app;

/* ---------------- model ---------------- */

static AppMacro *cur(void)
{
    if (!g_app || !g_app->macro_lib || g_sel < 0 || (guint)g_sel >= g_app->macro_lib->len)
        return NULL;
    return g_ptr_array_index(g_app->macro_lib, g_sel);
}

AppMacro *macros_find(App *app, const char *name)
{
    if (!app->macro_lib || !name)
        return NULL;
    for (guint i = 0; i < app->macro_lib->len; i++) {
        AppMacro *m = g_ptr_array_index(app->macro_lib, i);
        if (strcmp(m->name, name) == 0)
            return m;
    }
    return NULL;
}

static char *macros_dir(void) { return ajazzy_config_subdir("macros"); }

static char *file_for(const char *name)
{
    char *dir = macros_dir();
    if (!dir) return NULL;
    char *safe = g_strdup(name);
    for (char *p = safe; *p; p++)
        if (*p == '/' || *p == '\\') *p = '_';
    char *fn = g_strdup_printf("%s.json", safe);
    char *path = g_build_filename(dir, fn, NULL);
    g_free(dir); g_free(safe); g_free(fn);
    return path;
}

static char *macro_to_json(const AppMacro *m)
{
    GString *o = g_string_new("{\n  ");
    json_put_string(o, "name", m->name); g_string_append(o, ",\n  ");
    json_put_int(o, "loop_mode", m->loop_mode); g_string_append(o, ",\n  ");
    json_put_int(o, "loop_count", m->loop_count); g_string_append(o, ",\n  ");
    int kinds[MAX_STEPS], delays[MAX_STEPS], values[MAX_STEPS];
    int n = m->n_steps;
    for (int i = 0; i < n; i++) {
        kinds[i] = m->steps[i].kind;
        delays[i] = m->steps[i].delay_ms;
        values[i] = m->steps[i].value;
    }
    json_put_int(o, "steps", n); g_string_append(o, ",\n  ");
    json_put_int_array(o, "kinds", kinds, n ? n : 1); g_string_append(o, ",\n  ");
    json_put_int_array(o, "delays", delays, n ? n : 1); g_string_append(o, ",\n  ");
    json_put_int_array(o, "values", values, n ? n : 1);
    g_string_append(o, "\n}\n");
    return g_string_free(o, FALSE);
}

static gboolean macro_from_json(const char *t, AppMacro *m)
{
    memset(m, 0, sizeof(*m));
    if (!json_get_string(t, "name", m->name, sizeof(m->name)) || !m->name[0])
        return FALSE;
    m->loop_mode = json_get_int(t, "loop_mode", 0);
    m->loop_count = json_get_int(t, "loop_count", 1);
    if (m->loop_mode < 0 || m->loop_mode > 3) m->loop_mode = 0;
    int n = json_get_int(t, "steps", 0);
    if (n < 0) n = 0;
    if (n > MAX_STEPS) n = MAX_STEPS;
    int kinds[MAX_STEPS] = {0}, delays[MAX_STEPS] = {0}, values[MAX_STEPS] = {0};
    json_get_int_array(t, "kinds", kinds, n);
    json_get_int_array(t, "delays", delays, n);
    json_get_int_array(t, "values", values, n);
    int kept = 0;
    for (int i = 0; i < n; i++) {
        if (!ajazz_macro_kind_valid((uint8_t)kinds[i]) || delays[i] < 0 || delays[i] > 65535)
            continue; /* a hand-edited or damaged file must not send arbitrary bytes to the mouse */
        m->steps[kept].kind = (uint8_t)kinds[i];
        m->steps[kept].delay_ms = (uint16_t)delays[i];
        m->steps[kept].value = (uint8_t)values[i];
        kept++;
    }
    m->n_steps = kept;
    return TRUE;
}

void macros_save_library(App *app)
{
    if (!app->macro_lib)
        return;
    char *dir = macros_dir();
    if (!dir) return;
    for (guint i = 0; i < app->macro_lib->len; i++) {
        AppMacro *m = g_ptr_array_index(app->macro_lib, i);
        char *path = file_for(m->name);
        char *js = macro_to_json(m);
        if (path) json_write_file(path, js);
        g_free(js);
        g_free(path);
    }
    /* which library macro sits on which button */
    GString *o = g_string_new("{\n  ");
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        char k[8];
        snprintf(k, sizeof(k), "s%d", i);
        json_put_string(o, k, app->macro_slot_name[i]);
        g_string_append(o, i + 1 < AJAZZ_KEY_SLOTS ? ",\n  " : "\n");
    }
    g_string_append(o, "}\n");
    char *sp = g_build_filename(dir, "_slots.json", NULL);
    json_write_file(sp, o->str);
    g_free(sp);
    g_string_free(o, TRUE);
    g_free(dir);
}

void macros_load_library(App *app)
{
    if (!app->macro_lib)
        app->macro_lib = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_set_size(app->macro_lib, 0);
    char *dir = macros_dir();
    if (!dir) return;
    GDir *d = g_dir_open(dir, 0, NULL);
    if (d) {
        const char *e;
        while ((e = g_dir_read_name(d))) {
            if (!g_str_has_suffix(e, ".json") || e[0] == '_')
                continue;
            char *p = g_build_filename(dir, e, NULL);
            char *t = json_read_file(p);
            g_free(p);
            if (!t) continue;
            AppMacro *m = g_new0(AppMacro, 1);
            if (macro_from_json(t, m))
                g_ptr_array_add(app->macro_lib, m);
            else
                g_free(m);
            g_free(t);
        }
        g_dir_close(d);
    }
    char *sp = g_build_filename(dir, "_slots.json", NULL);
    char *st = json_read_file(sp);
    g_free(sp);
    if (st) {
        for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
            char k[8];
            snprintf(k, sizeof(k), "s%d", i);
            json_get_string(st, k, app->macro_slot_name[i], sizeof(app->macro_slot_name[i]));
        }
        g_free(st);
    }
    g_free(dir);
}

/* ---------------- writing to the mouse ---------------- */

typedef struct {
    AppMacro macro;
    int slot;
    int rc; /* 0 ok, 1 content failed, 2 table read failed, 3 assign failed */
} AssignJob;

static void assign_work(App *app, gpointer d)
{
    AssignJob *j = d;
    ajazz_report_t chunks[16];
    int n = ajazz_build_macro_content_chunked(chunks, 16, (uint8_t)j->slot, j->macro.steps, j->macro.n_steps);
    if (n < 0) { j->rc = 1; return; }
    for (int i = 0; i < n; i++)
        if (ajazz_send_and_wait_mt(app, &chunks[i], 1000) != 0) { j->rc = 1; return; }

    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) { j->rc = 2; return; }
    ajazz_report_t assign;
    ajazz_build_key_table_from_query(&assign, &reply);
    ajazz_key_slot_set_macro(&assign, j->slot, (ajazz_macro_loop_t)j->macro.loop_mode, (uint8_t)j->macro.loop_count);
    if (ajazz_send_and_wait_mt(app, &assign, 1000) != 0) { j->rc = 3; return; }
    j->rc = 0;
}

static void assign_done(App *app, gpointer d)
{
    AssignJob *j = d;
    switch (j->rc) {
        case 0:
            g_strlcpy(app->macro_slot_name[j->slot], j->macro.name, sizeof(app->macro_slot_name[j->slot]));
            macros_save_library(app);
            toastf(app, _("\xE2\x80\x9C%s\xE2\x80\x9D is now on button %d."), j->macro.name, j->slot + 1);
            refresh_buttons(app);
            break;
        case 1: toast(app, _("Couldn't write the macro to the mouse.")); break;
        case 2: toast(app, _("Couldn't read the button table.")); break;
        default: toast(app, _("Couldn't assign the macro to the button.")); break;
    }
}

gboolean macros_assign(App *app, int slot, const AppMacro *m)
{
    if (!app->connected || slot < 0 || slot >= AJAZZ_KEY_SLOTS)
        return FALSE;
    if (m->n_steps <= 0) {
        toast(app, _("This macro has no steps yet."));
        return FALSE;
    }
    AssignJob *j = g_new0(AssignJob, 1);
    j->macro = *m;
    j->slot = slot;
    app_async(app, assign_work, assign_done, j, g_free);
    return TRUE;
}

/* ---------------- event descriptions ---------------- */

static const char *mouse_button_name(uint8_t bit)
{
    switch (bit) {
        case 1: return _("Left button");
        case 2: return _("Right button");
        case 4: return _("Middle button");
        case 8: return _("Back button");
        case 16: return _("Forward button");
        default: return _("Mouse button");
    }
}

static void step_text(const ajazz_macro_step_t *s, char *out, size_t n, const char **icon, gboolean *down)
{
    char k[32];
    switch (s->kind) {
        case AJAZZ_MACRO_KEY_DOWN:
        case AJAZZ_MACRO_KEY_UP:
            keymap_key_name(s->value, k, sizeof(k));
            snprintf(out, n, "%s", k);
            *icon = "input-keyboard-symbolic";
            *down = s->kind == AJAZZ_MACRO_KEY_DOWN;
            break;
        case AJAZZ_MACRO_MOD_DOWN:
        case AJAZZ_MACRO_MOD_UP:
            snprintf(out, n, "%s", keymap_modifier_name(s->value));
            *icon = "input-keyboard-symbolic";
            *down = s->kind == AJAZZ_MACRO_MOD_DOWN;
            break;
        default:
            snprintf(out, n, "%s", mouse_button_name(s->value));
            *icon = "input-mouse-symbolic";
            *down = s->kind == AJAZZ_MACRO_MOUSE_DOWN;
            break;
    }
}

/* ---------------- editor rendering ---------------- */

static void rebuild_list(App *app);
static void rebuild_events(void);

static void save_and_refresh_counts(void)
{
    macros_save_library(g_app);
    AppMacro *m = cur();
    if (!m) return;
    char c[48];
    snprintf(c, sizeof(c), _("%d of %d steps"), m->n_steps, MAX_STEPS);
    ui_chip_set(M.count_chip, c, m->n_steps >= MAX_STEPS ? UI_CHIP_WARN : UI_CHIP_NEUTRAL);
    gtk_widget_set_visible(M.limit_note, m->n_steps >= MAX_STEPS);
}

static void on_event_delay(GtkAdjustment *adj, gpointer user_data)
{
    AppMacro *m = cur();
    int i = GPOINTER_TO_INT(user_data);
    if (loading || !m || i >= m->n_steps) return;
    m->steps[i].delay_ms = (uint16_t)gtk_adjustment_get_value(adj);
    save_and_refresh_counts();
}

static void on_event_remove(GtkButton *b, gpointer user_data)
{
    (void)b;
    AppMacro *m = cur();
    int i = GPOINTER_TO_INT(user_data);
    if (!m || i >= m->n_steps) return;
    memmove(&m->steps[i], &m->steps[i + 1], (size_t)(m->n_steps - i - 1) * sizeof(m->steps[0]));
    m->n_steps--;
    rebuild_events();
    rebuild_list(g_app);
    save_and_refresh_counts();
}

static void on_event_move(GtkButton *b, gpointer user_data)
{
    AppMacro *m = cur();
    int i = GPOINTER_TO_INT(user_data);
    int dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "dir"));
    int j = i + dir;
    if (!m || i >= m->n_steps || j < 0 || j >= m->n_steps) return;
    ajazz_macro_step_t t = m->steps[i];
    m->steps[i] = m->steps[j];
    m->steps[j] = t;
    rebuild_events();
    save_and_refresh_counts();
}

static GtkWidget *event_row(int i, const ajazz_macro_step_t *s)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "aj-event");
    gtk_widget_add_css_class(row, "aj-event-down");

    char txt[64];
    const char *icon;
    gboolean down;
    step_text(s, txt, sizeof(txt), &icon, &down);
    if (!down) {
        gtk_widget_remove_css_class(row, "aj-event-down");
        gtk_widget_add_css_class(row, "aj-event-up");
    }

    char idx[8];
    snprintf(idx, sizeof(idx), "%d", i + 1);
    GtkWidget *num = gtk_label_new(idx);
    gtk_widget_add_css_class(num, "dim-label");
    gtk_widget_set_size_request(num, 26, -1);
    gtk_box_append(GTK_BOX(row), num);
    gtk_box_append(GTK_BOX(row), gtk_image_new_from_icon_name(icon));

    GtkWidget *l = gtk_label_new(txt);
    gtk_widget_add_css_class(l, "heading");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), l);
    gtk_box_append(GTK_BOX(row), ui_chip_new(down ? _("press") : _("release"), down ? UI_CHIP_OK : UI_CHIP_WARN));

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(row), spacer);

    GtkWidget *lbl = gtk_label_new(_("then wait"));
    gtk_widget_add_css_class(lbl, "dim-label");
    gtk_box_append(GTK_BOX(row), lbl);
    GtkWidget *spin = gtk_spin_button_new_with_range(0, 65535, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), s->delay_ms);
    gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
    g_signal_connect(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin)), "value-changed",
        G_CALLBACK(on_event_delay), GINT_TO_POINTER(i));
    gtk_box_append(GTK_BOX(row), spin);
    gtk_box_append(GTK_BOX(row), gtk_label_new("ms"));

    const char *icons[3] = {"go-up-symbolic", "go-down-symbolic", "user-trash-symbolic"};
    for (int k = 0; k < 3; k++) {
        GtkWidget *b = gtk_button_new_from_icon_name(icons[k]);
        gtk_widget_add_css_class(b, "flat");
        gtk_widget_add_css_class(b, "circular");
        if (k < 2) {
            g_object_set_data(G_OBJECT(b), "dir", GINT_TO_POINTER(k == 0 ? -1 : 1));
            g_signal_connect(b, "clicked", G_CALLBACK(on_event_move), GINT_TO_POINTER(i));
        } else {
            g_signal_connect(b, "clicked", G_CALLBACK(on_event_remove), GINT_TO_POINTER(i));
        }
        gtk_box_append(GTK_BOX(row), b);
    }
    return row;
}

static void rebuild_events(void)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(M.events_box)))
        gtk_box_remove(GTK_BOX(M.events_box), c);
    AppMacro *m = cur();
    if (!m) return;
    loading = TRUE;
    if (m->n_steps == 0) {
        gtk_box_append(GTK_BOX(M.events_box),
            ui_empty_new("input-keyboard-symbolic", _("Nothing recorded yet"),
                _("Press Record and perform the keys and clicks you want, or add steps by hand.")));
    }
    for (int i = 0; i < m->n_steps; i++)
        gtk_box_append(GTK_BOX(M.events_box), event_row(i, &m->steps[i]));
    loading = FALSE;
}

static void sync_editor(App *app)
{
    AppMacro *m = cur();
    gtk_widget_set_visible(M.editor, m != NULL);
    gtk_widget_set_visible(M.empty, m == NULL);
    if (!m) return;
    loading = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(M.name_entry), m->name);
    ui_segmented_set(M.loop_seg, m->loop_mode);
    adw_spin_row_set_value(M.loop_count, m->loop_count < 1 ? 1 : m->loop_count);
    gtk_widget_set_visible(GTK_WIDGET(M.loop_count), m->loop_mode == AJAZZ_MACRO_N_TIMES);
    loading = FALSE;
    rebuild_events();
    save_and_refresh_counts();
    gtk_widget_set_sensitive(M.assign_btn, app->connected);
}

/* ---------------- library list ---------------- */

static void on_macro_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    App *app = user_data;
    if (!row) { g_sel = -1; sync_editor(app); return; }
    int i = gtk_list_box_row_get_index(row);
    if (i == g_sel) return;
    g_sel = i;
    sync_editor(app);
}

static void rebuild_list(App *app)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(M.list)))
        gtk_list_box_remove(GTK_LIST_BOX(M.list), c);
    for (guint i = 0; app->macro_lib && i < app->macro_lib->len; i++) {
        AppMacro *m = g_ptr_array_index(app->macro_lib, i);
        char sub[64];
        snprintf(sub, sizeof(sub), _("%d steps"), m->n_steps);
        AdwActionRow *row = ui_row_new(m->name, sub, NULL);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        gtk_list_box_append(GTK_LIST_BOX(M.list), GTK_WIDGET(row));
    }
    if (g_sel >= 0 && app->macro_lib && (guint)g_sel < app->macro_lib->len) {
        g_signal_handlers_block_by_func(M.list, on_macro_row_selected, app);
        gtk_list_box_select_row(GTK_LIST_BOX(M.list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(M.list), g_sel));
        g_signal_handlers_unblock_by_func(M.list, on_macro_row_selected, app);
    }
}

static char *unique_name(App *app, const char *base)
{
    char *n = g_strdup(base);
    for (int k = 2; macros_find(app, n); k++) {
        g_free(n);
        n = g_strdup_printf("%s %d", base, k);
    }
    return n;
}

static void select_macro(App *app, int idx)
{
    g_sel = idx;
    rebuild_list(app);
    if (idx >= 0)
        gtk_list_box_select_row(GTK_LIST_BOX(M.list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(M.list), idx));
    sync_editor(app);
}

static void on_new_macro(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AppMacro *m = g_new0(AppMacro, 1);
    char *n = unique_name(app, _("New macro"));
    g_strlcpy(m->name, n, sizeof(m->name));
    g_free(n);
    m->loop_count = 1;
    g_ptr_array_add(app->macro_lib, m);
    macros_save_library(app);
    select_macro(app, (int)app->macro_lib->len - 1);
    gtk_widget_grab_focus(M.name_entry);
}

static void on_duplicate(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AppMacro *src = cur();
    if (!src) return;
    AppMacro *m = g_new0(AppMacro, 1);
    *m = *src;
    char *n = unique_name(app, src->name);
    g_strlcpy(m->name, n, sizeof(m->name));
    g_free(n);
    g_ptr_array_add(app->macro_lib, m);
    macros_save_library(app);
    select_macro(app, (int)app->macro_lib->len - 1);
}

static void on_delete_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    App *app = user_data;
    AppMacro *m = cur();
    if (strcmp(resp, "delete") != 0 || !m) return;
    char *path = file_for(m->name);
    if (path) g_remove(path);
    g_free(path);
    g_ptr_array_remove_index(app->macro_lib, (guint)g_sel);
    macros_save_library(app);
    select_macro(app, -1);
    toast(app, _("Macro deleted."));
}

static void on_delete(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AppMacro *m = cur();
    if (!m) return;
    char *body = g_strdup_printf(_("\xE2\x80\x9C%s\xE2\x80\x9D will be removed from your library. "
        "Buttons that already play it keep working until you change them."), m->name);
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Delete this macro?"), body));
    g_free(body);
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "delete", _("Delete"));
    adw_alert_dialog_set_response_appearance(dlg, "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_delete_response), app);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
}

/* ---------------- editor handlers ---------------- */

static void on_name_changed(GtkEditable *e, gpointer user_data)
{
    App *app = user_data;
    AppMacro *m = cur();
    if (loading || !m) return;
    const char *t = gtk_editable_get_text(e);
    if (!*t) return;
    char *old = file_for(m->name);
    if (old) g_remove(old);
    g_free(old);
    g_strlcpy(m->name, t, sizeof(m->name));
    /* keep the button->macro bookkeeping pointing at the renamed macro */
    save_and_refresh_counts();
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(M.list), g_sel);
    if (row)
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), m->name);
    (void)app;
}

static void on_loop_selected(int idx, gpointer user_data)
{
    (void)user_data;
    AppMacro *m = cur();
    if (loading || !m) return;
    m->loop_mode = idx;
    if (idx == AJAZZ_MACRO_N_TIMES && m->loop_count < 1)
        m->loop_count = 1;
    gtk_widget_set_visible(GTK_WIDGET(M.loop_count), idx == AJAZZ_MACRO_N_TIMES);
    save_and_refresh_counts();
}

static void on_loop_count(GObject *row, GParamSpec *p, gpointer user_data)
{
    (void)p; (void)user_data;
    AppMacro *m = cur();
    if (loading || !m) return;
    m->loop_count = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    save_and_refresh_counts();
}

static void on_bulk_delay(GtkButton *b, gpointer user_data)
{
    (void)b; (void)user_data;
    AppMacro *m = cur();
    if (!m) return;
    uint16_t d = (uint16_t)adw_spin_row_get_value(M.bulk_delay);
    for (int i = 0; i < m->n_steps; i++)
        m->steps[i].delay_ms = d;
    rebuild_events();
    save_and_refresh_counts();
}

static void on_clear_steps(GtkButton *b, gpointer user_data)
{
    (void)b; (void)user_data;
    AppMacro *m = cur();
    if (!m) return;
    m->n_steps = 0;
    rebuild_events();
    rebuild_list(g_app);
    save_and_refresh_counts();
}

static void append_step(AppMacro *m, uint8_t kind, uint16_t delay, uint8_t value)
{
    if (m->n_steps >= MAX_STEPS)
        return;
    m->steps[m->n_steps++] = (ajazz_macro_step_t){kind, delay, value};
}

static void on_add_mouse(GtkButton *b, gpointer user_data)
{
    (void)user_data;
    AppMacro *m = cur();
    if (!m) return;
    uint8_t bit = (uint8_t)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bit"));
    uint16_t d = (uint16_t)adw_spin_row_get_value(M.bulk_delay);
    append_step(m, AJAZZ_MACRO_MOUSE_DOWN, d, bit);
    append_step(m, AJAZZ_MACRO_MOUSE_UP, d, bit);
    rebuild_events();
    rebuild_list(g_app);
    save_and_refresh_counts();
}

/* ---------------- recorder ---------------- */

typedef struct {
    App *app;
    AdwAlertDialog *dlg;
    GtkWidget *status;
    gint64 last_us;
    gboolean any;
    gboolean mod_down[8];
    guint8 key_down[256];
    int added;
} Recorder;

static void rec_push(Recorder *r, uint8_t kind, uint8_t value)
{
    AppMacro *m = cur();
    if (!m || m->n_steps >= MAX_STEPS) {
        adw_alert_dialog_format_body(r->dlg, _("The macro is full (%d steps)."), MAX_STEPS);
        return;
    }
    gint64 now = g_get_monotonic_time();
    if (r->any && m->n_steps > 0) {
        gint64 ms = (now - r->last_us) / 1000;
        if (ms < 1) ms = 1;
        if (ms > 65535) ms = 65535;
        m->steps[m->n_steps - 1].delay_ms = (uint16_t)ms;
    }
    append_step(m, kind, 10, value);
    r->last_us = now;
    r->any = TRUE;
    r->added++;
    char t[64];
    snprintf(t, sizeof(t), _("%d steps recorded"), r->added);
    gtk_label_set_text(GTK_LABEL(r->status), t);
}

static gboolean rec_key_pressed(GtkEventControllerKey *c, guint kv, guint keycode, GdkModifierType st, gpointer ud)
{
    (void)c; (void)kv; (void)st;
    Recorder *r = ud;
    uint8_t mod = keymap_modifier_bit(keycode);
    if (mod) {
        int bit = __builtin_ctz(mod);
        if (!r->mod_down[bit]) { r->mod_down[bit] = TRUE; rec_push(r, AJAZZ_MACRO_MOD_DOWN, mod); }
        return TRUE;
    }
    uint8_t hid = keymap_hid_from_gdk(keycode);
    if (hid && !r->key_down[hid]) {
        r->key_down[hid] = TRUE; /* ignore auto-repeat */
        rec_push(r, AJAZZ_MACRO_KEY_DOWN, hid);
    }
    return TRUE;
}

static void rec_key_released(GtkEventControllerKey *c, guint kv, guint keycode, GdkModifierType st, gpointer ud)
{
    (void)c; (void)kv; (void)st;
    Recorder *r = ud;
    uint8_t mod = keymap_modifier_bit(keycode);
    if (mod) {
        int bit = __builtin_ctz(mod);
        if (r->mod_down[bit]) { r->mod_down[bit] = FALSE; rec_push(r, AJAZZ_MACRO_MOD_UP, mod); }
        return;
    }
    uint8_t hid = keymap_hid_from_gdk(keycode);
    if (hid && r->key_down[hid]) {
        r->key_down[hid] = FALSE;
        rec_push(r, AJAZZ_MACRO_KEY_UP, hid);
    }
}

static uint8_t gdk_button_to_bit(guint b)
{
    switch (b) {
        case 1: return 1;   /* left */
        case 3: return 2;   /* right */
        case 2: return 4;   /* middle */
        case 8: return 8;   /* back */
        case 9: return 16;  /* forward */
        default: return 0;
    }
}

static void rec_mouse_pressed(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)n; (void)x; (void)y;
    uint8_t bit = gdk_button_to_bit(gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g)));
    if (bit) rec_push(ud, AJAZZ_MACRO_MOUSE_DOWN, bit);
}

static void rec_mouse_released(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)n; (void)x; (void)y;
    uint8_t bit = gdk_button_to_bit(gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g)));
    if (bit) rec_push(ud, AJAZZ_MACRO_MOUSE_UP, bit);
}

static void rec_response(AdwAlertDialog *dlg, const char *resp, gpointer ud)
{
    (void)dlg; (void)resp;
    Recorder *r = ud;
    AppMacro *m = cur();
    /* release anything still "held" so the macro can't leave a key stuck */
    for (int i = 0; i < 8; i++)
        if (r->mod_down[i]) rec_push(r, AJAZZ_MACRO_MOD_UP, (uint8_t)(1u << i));
    for (int k = 1; k < 256; k++)
        if (r->key_down[k]) rec_push(r, AJAZZ_MACRO_KEY_UP, (uint8_t)k);
    if (m && m->n_steps > 0 && m->steps[m->n_steps - 1].delay_ms < 10)
        m->steps[m->n_steps - 1].delay_ms = 10;
    rebuild_events();
    rebuild_list(r->app);
    save_and_refresh_counts();
    g_free(r);
}

static void on_record(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AppMacro *m = cur();
    if (!m) return;
    if (m->n_steps >= MAX_STEPS) {
        toast(app, _("The macro is full. Remove some steps first."));
        return;
    }
    Recorder *r = g_new0(Recorder, 1);
    r->app = app;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Recording\xE2\x80\xA6"),
        _("Type keys and click in this window \xE2\x80\x94 your timing is recorded too. Press Stop when you're done.")));
    r->dlg = dlg;
    r->status = gtk_label_new(_("0 steps recorded"));
    gtk_widget_add_css_class(r->status, "aj-rec");
    gtk_widget_set_size_request(r->status, 320, 110);
    GtkWidget *area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(area, "aj-card");
    gtk_box_append(GTK_BOX(area), r->status);
    gtk_widget_set_focusable(area, TRUE);
    adw_alert_dialog_set_extra_child(dlg, area);
    adw_alert_dialog_add_response(dlg, "stop", _("Stop recording"));
    adw_alert_dialog_set_response_appearance(dlg, "stop", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_close_response(dlg, "stop");

    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(rec_key_pressed), r);
    g_signal_connect(kc, "key-released", G_CALLBACK(rec_key_released), r);
    gtk_widget_add_controller(GTK_WIDGET(dlg), kc);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(rec_mouse_pressed), r);
    g_signal_connect(click, "released", G_CALLBACK(rec_mouse_released), r);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    g_signal_connect(dlg, "response", G_CALLBACK(rec_response), r);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
    gtk_widget_grab_focus(area);
}

/* ---------------- assign / import / export ---------------- */

static void on_assign(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    AppMacro *m = cur();
    if (!m) return;
    int slot = (int)adw_combo_row_get_selected(M.slot_combo) + 1; /* skip the locked left click */
    macros_assign(app, slot, m);
}

static void on_import_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    App *app = user_data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f) return;
    char *text = NULL;
    if (g_file_load_contents(f, NULL, &text, NULL, NULL, NULL)) {
        AppMacro *m = g_new0(AppMacro, 1);
        if (macro_from_json(text, m)) {
            char *n = unique_name(app, m->name);
            g_strlcpy(m->name, n, sizeof(m->name));
            g_free(n);
            g_ptr_array_add(app->macro_lib, m);
            macros_save_library(app);
            select_macro(app, (int)app->macro_lib->len - 1);
            toast(app, _("Macro imported."));
        } else {
            g_free(m);
            toast(app, _("That file isn't an Ajazzy macro."));
        }
        g_free(text);
    }
    g_object_unref(f);
}

static void on_import(GtkButton *b, gpointer user_data)
{
    (void)b;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Import a macro"));
    gtk_file_dialog_open(d, GTK_WINDOW(((App *)user_data)->win), NULL, on_import_done, user_data);
    g_object_unref(d);
}

static void on_export_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    App *app = user_data;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    AppMacro *m = cur();
    if (!f || !m) return;
    char *js = macro_to_json(m);
    GError *err = NULL;
    if (g_file_replace_contents(f, js, strlen(js), NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &err))
        toast(app, _("Macro exported."));
    else
        toast(app, _("Couldn't write the file."));
    g_clear_error(&err);
    g_free(js);
    g_object_unref(f);
}

static void on_export(GtkButton *b, gpointer user_data)
{
    (void)b;
    AppMacro *m = cur();
    if (!m) return;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Export this macro"));
    char *fn = g_strdup_printf("%s.json", m->name);
    gtk_file_dialog_set_initial_name(d, fn);
    g_free(fn);
    gtk_file_dialog_save(d, GTK_WINDOW(((App *)user_data)->win), NULL, on_export_done, user_data);
    g_object_unref(d);
}

/* ---------------- refresh / page ---------------- */

void refresh_macros(App *app)
{
    rebuild_list(app);
    sync_editor(app);
}

static GtkWidget *mouse_add_button(const char *label, int bit, App *app)
{
    (void)app;
    GtkWidget *b = ui_button_new(label, NULL, "secondary");
    g_object_set_data(G_OBJECT(b), "bit", GINT_TO_POINTER(bit));
    g_signal_connect(b, "clicked", G_CALLBACK(on_add_mouse), NULL);
    return b;
}

GtkWidget *build_macros_page(App *app)
{
    g_app = app;
    if (!app->macro_lib)
        macros_load_library(app);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *imp = ui_button_new(_("Import"), "document-open-symbolic", "secondary");
    g_signal_connect(imp, "clicked", G_CALLBACK(on_import), app);
    GtkWidget *nw = ui_button_new(_("New macro"), "list-add-symbolic", "primary");
    g_signal_connect(nw, "clicked", G_CALLBACK(on_new_macro), app);
    gtk_box_append(GTK_BOX(actions), imp);
    gtk_box_append(GTK_BOX(actions), nw);

    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Macros"),
        _("Record a sequence of keys and clicks, then put it on any button."), actions, &body);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    /* library */
    GtkWidget *lcb;
    GtkWidget *lcard = ui_card_new(_("Library"), NULL, &lcb);
    gtk_widget_set_size_request(lcard, 250, -1);
    gtk_widget_set_valign(lcard, GTK_ALIGN_START);
    M.list = gtk_list_box_new();
    gtk_widget_add_css_class(M.list, "aj-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(M.list), GTK_SELECTION_SINGLE);
    g_signal_connect(M.list, "row-selected", G_CALLBACK(on_macro_row_selected), app);
    gtk_box_append(GTK_BOX(lcb), M.list);
    gtk_box_append(GTK_BOX(columns), lcard);

    /* right side: either the editor or an empty state */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(right, TRUE);

    M.empty = ui_empty_new("input-keyboard-symbolic", _("No macro selected"),
        _("Create a macro, or pick one from the library."));
    gtk_box_append(GTK_BOX(right), M.empty);

    M.editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_box_append(GTK_BOX(right), M.editor);

    GtkWidget *cb;
    GtkWidget *card = ui_card_new(NULL, NULL, &cb);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(top, "aj-card-head");
    M.name_entry = gtk_entry_new();
    gtk_widget_set_hexpand(M.name_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(M.name_entry), _("Macro name"));
    g_signal_connect(M.name_entry, "changed", G_CALLBACK(on_name_changed), app);
    gtk_box_append(GTK_BOX(top), M.name_entry);
    M.count_chip = ui_chip_new("0", UI_CHIP_NEUTRAL);
    gtk_box_append(GTK_BOX(top), M.count_chip);
    GtkWidget *dup = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_set_tooltip_text(dup, _("Duplicate"));
    gtk_widget_add_css_class(dup, "flat");
    g_signal_connect(dup, "clicked", G_CALLBACK(on_duplicate), app);
    GtkWidget *exp = gtk_button_new_from_icon_name("document-save-as-symbolic");
    gtk_widget_set_tooltip_text(exp, _("Export\xE2\x80\xA6"));
    gtk_widget_add_css_class(exp, "flat");
    g_signal_connect(exp, "clicked", G_CALLBACK(on_export), app);
    GtkWidget *del = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_set_tooltip_text(del, _("Delete"));
    gtk_widget_add_css_class(del, "flat");
    g_signal_connect(del, "clicked", G_CALLBACK(on_delete), app);
    gtk_box_append(GTK_BOX(top), dup);
    gtk_box_append(GTK_BOX(top), exp);
    gtk_box_append(GTK_BOX(top), del);
    gtk_box_append(GTK_BOX(cb), top);

    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(tools, 18);
    gtk_widget_set_margin_end(tools, 18);
    M.rec_btn = ui_button_new(_("Record"), "media-record-symbolic", "primary");
    g_signal_connect(M.rec_btn, "clicked", G_CALLBACK(on_record), app);
    gtk_box_append(GTK_BOX(tools), M.rec_btn);
    gtk_box_append(GTK_BOX(tools), mouse_add_button(_("+ Left click"), 1, app));
    gtk_box_append(GTK_BOX(tools), mouse_add_button(_("+ Right click"), 2, app));
    gtk_box_append(GTK_BOX(tools), mouse_add_button(_("+ Middle click"), 4, app));
    GtkWidget *clear = ui_button_new(_("Clear"), "edit-clear-all-symbolic", "ghost");
    gtk_widget_set_hexpand(clear, TRUE);
    gtk_widget_set_halign(clear, GTK_ALIGN_END);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_steps), NULL);
    gtk_box_append(GTK_BOX(tools), clear);
    gtk_box_append(GTK_BOX(cb), tools);

    M.events_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(M.events_box, 18);
    gtk_widget_set_margin_end(M.events_box, 18);
    gtk_widget_set_margin_top(M.events_box, 14);
    gtk_widget_set_margin_bottom(M.events_box, 14);
    gtk_box_append(GTK_BOX(cb), M.events_box);

    M.limit_note = gtk_label_new(_("This is the most steps the mouse can store."));
    gtk_widget_add_css_class(M.limit_note, "aj-callout");
    gtk_widget_set_margin_start(M.limit_note, 18);
    gtk_widget_set_margin_end(M.limit_note, 18);
    gtk_widget_set_margin_bottom(M.limit_note, 14);
    gtk_box_append(GTK_BOX(cb), M.limit_note);
    gtk_box_append(GTK_BOX(M.editor), card);

    /* playback + assignment */
    card = ui_card_new(_("Playback"), NULL, &cb);
    GtkWidget *list = ui_list_new();
    const char *loops[] = {_("Once"), _("While held"), _("Until a click"), _("N times")};
    M.loop_seg = ui_segmented_new(loops, 4, 0, on_loop_selected, NULL);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ui_row_new(_("Repeat"),
        _("What happens when you press the button."), M.loop_seg)));
    M.loop_count = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 255, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(M.loop_count), _("Number of repeats"));
    g_signal_connect(M.loop_count, "notify::value", G_CALLBACK(on_loop_count), NULL);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(M.loop_count));

    M.bulk_delay = ADW_SPIN_ROW(adw_spin_row_new_with_range(0, 2000, 5));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(M.bulk_delay), _("Default delay (ms)"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(M.bulk_delay), _("Used for steps you add by hand."));
    adw_spin_row_set_value(M.bulk_delay, 30);
    GtkWidget *apply_all = gtk_button_new_with_label(_("Apply to all"));
    gtk_widget_add_css_class(apply_all, "aj-btn");
    gtk_widget_add_css_class(apply_all, "aj-btn-secondary");
    gtk_widget_set_valign(apply_all, GTK_ALIGN_CENTER);
    g_signal_connect(apply_all, "clicked", G_CALLBACK(on_bulk_delay), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(M.bulk_delay), apply_all);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(M.bulk_delay));

    M.slot_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(M.slot_combo), _("Put it on button"));
    const char *slots[] = {_("Button 2 (right)"), _("Button 3 (middle)"), _("Button 4 (back)"),
        _("Button 5 (forward)"), _("Button 6 (DPI)"), NULL};
    adw_combo_row_set_model(M.slot_combo, G_LIST_MODEL(gtk_string_list_new(slots)));
    adw_combo_row_set_selected(M.slot_combo, 2);
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(M.slot_combo));
    gtk_box_append(GTK_BOX(cb), list);

    M.assign_btn = ui_button_new(_("Write to the mouse"), "object-select-symbolic", "primary");
    gtk_widget_set_halign(M.assign_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_start(M.assign_btn, 18);
    gtk_widget_set_margin_bottom(M.assign_btn, 18);
    gtk_widget_set_margin_top(M.assign_btn, 6);
    g_signal_connect(M.assign_btn, "clicked", G_CALLBACK(on_assign), app);
    gtk_box_append(GTK_BOX(cb), M.assign_btn);
    gtk_box_append(GTK_BOX(M.editor), card);

    gtk_box_append(GTK_BOX(columns), right);
    gtk_box_append(GTK_BOX(body), columns);

    rebuild_list(app);
    sync_editor(app);
    return page;
}
