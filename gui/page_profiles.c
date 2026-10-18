/*
 * Profiles: whole-mouse snapshots stored on this PC (the mouse has no
 * onboard profile slots in this protocol). A profile captures DPI stages and
 * colours, report rate, sensor and power settings, lighting, the button
 * table and any macros those buttons play -- and puts all of it back with
 * one click. Profiles can be exported/imported as plain JSON files.
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glib/gstdio.h>

#include "app.h"
#include "jsonutil.h"
#include "../devices/device.h"

#define PROFILE_VERSION 2

typedef struct {
    char name[64];
    uint16_t dpi[AJAZZ_DPI_STAGES];
    uint8_t dpi_color[AJAZZ_DPI_STAGES][3];
    int dpi_selected;
    int dpi_count;
    int rate_hz;
    AppLight light;
    AppSensor sensor;
    uint8_t buttons[AJAZZ_KEY_SLOTS][3];
    gboolean has_macro[AJAZZ_KEY_SLOTS];
    AppMacro macro[AJAZZ_KEY_SLOTS];
} Profile;

static GtkWidget *g_flow;
static GtkWidget *g_empty;
static GtkWidget *g_save_entry;
static App *g_app;

/* ---------------- files ---------------- */

static char *profile_path(const char *name)
{
    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return NULL;
    char *safe = g_strdup(name);
    for (char *p = safe; *p; p++)
        if (*p == '/' || *p == '\\') *p = '_';
    char *fn = g_strdup_printf("%s.json", safe);
    char *path = g_build_filename(dir, fn, NULL);
    g_free(dir); g_free(safe); g_free(fn);
    return path;
}

static char *profile_to_json(const Profile *p)
{
    GString *o = g_string_new("{\n  ");
#define SEP g_string_append(o, ",\n  ")
    json_put_int(o, "version", PROFILE_VERSION); SEP;
    json_put_string(o, "name", p->name); SEP;
    int tmp[64];
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) tmp[i] = p->dpi[i];
    json_put_int_array(o, "dpi", tmp, AJAZZ_DPI_STAGES); SEP;
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
        for (int c = 0; c < 3; c++) tmp[i * 3 + c] = p->dpi_color[i][c];
    json_put_int_array(o, "dpi_color", tmp, AJAZZ_DPI_STAGES * 3); SEP;
    json_put_int(o, "dpi_selected", p->dpi_selected); SEP;
    json_put_int(o, "dpi_count", p->dpi_count); SEP;
    json_put_int(o, "rate_hz", p->rate_hz); SEP;

    json_put_int(o, "l_effect", p->light.effect); SEP;
    json_put_int(o, "l_brightness", p->light.brightness); SEP;
    json_put_int(o, "l_speed", p->light.speed); SEP;
    json_put_int(o, "l_transition", p->light.soft_transition); SEP;
    json_put_int(o, "l_palette_n", p->light.palette_n); SEP;
    for (int c = 0; c < 3; c++) tmp[c] = p->light.color[c];
    json_put_int_array(o, "l_color", tmp, 3); SEP;
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        for (int c = 0; c < 3; c++) tmp[i * 3 + c] = p->light.palette[i][c];
    json_put_int_array(o, "l_palette", tmp, LIGHT_PALETTE_MAX * 3); SEP;

    json_put_int(o, "s_liftoff", p->sensor.liftoff_mm); SEP;
    json_put_int(o, "s_angle", p->sensor.angle_snap); SEP;
    json_put_int(o, "s_ripple", p->sensor.ripple); SEP;
    json_put_int(o, "s_msync", p->sensor.motion_sync); SEP;
    json_put_int(o, "s_sleep", p->sensor.sleep_seconds); SEP;
    json_put_int(o, "s_wake", p->sensor.wake_on_move); SEP;
    json_put_int(o, "s_idle", p->sensor.idle_light_off); SEP;
    json_put_int(o, "s_debounce", p->sensor.debounce_ms); SEP;
    json_put_int(o, "s_valid", p->sensor.valid); SEP;

    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        for (int c = 0; c < 3; c++) tmp[i * 3 + c] = p->buttons[i][c];
    json_put_int_array(o, "buttons", tmp, AJAZZ_KEY_SLOTS * 3);

    for (int s = 0; s < AJAZZ_KEY_SLOTS; s++) {
        if (!p->has_macro[s]) continue;
        const AppMacro *m = &p->macro[s];
        char k[24];
        SEP;
        snprintf(k, sizeof(k), "m%d_name", s); json_put_string(o, k, m->name); SEP;
        snprintf(k, sizeof(k), "m%d_loop", s); json_put_int(o, k, m->loop_mode); SEP;
        snprintf(k, sizeof(k), "m%d_loops", s); json_put_int(o, k, m->loop_count); SEP;
        snprintf(k, sizeof(k), "m%d_n", s); json_put_int(o, k, m->n_steps); SEP;
        int kinds[AJAZZ_MACRO_MAX_CHUNKED_STEPS], delays[AJAZZ_MACRO_MAX_CHUNKED_STEPS], vals[AJAZZ_MACRO_MAX_CHUNKED_STEPS];
        int n = m->n_steps ? m->n_steps : 1;
        for (int i = 0; i < m->n_steps; i++) {
            kinds[i] = m->steps[i].kind;
            delays[i] = m->steps[i].delay_ms;
            vals[i] = m->steps[i].value;
        }
        snprintf(k, sizeof(k), "m%d_kinds", s); json_put_int_array(o, k, kinds, n); SEP;
        snprintf(k, sizeof(k), "m%d_delays", s); json_put_int_array(o, k, delays, n); SEP;
        snprintf(k, sizeof(k), "m%d_values", s); json_put_int_array(o, k, vals, n);
    }
#undef SEP
    g_string_append(o, "\n}\n");
    return g_string_free(o, FALSE);
}

static gboolean profile_from_json(const char *t, Profile *p)
{
    memset(p, 0, sizeof(*p));
    if (!json_get_string(t, "name", p->name, sizeof(p->name)) || !p->name[0])
        return FALSE;
    int tmp[64] = {0};
    json_get_int_array(t, "dpi", tmp, AJAZZ_DPI_STAGES);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++) p->dpi[i] = (uint16_t)tmp[i];
    memset(tmp, 0, sizeof(tmp));
    json_get_int_array(t, "dpi_color", tmp, AJAZZ_DPI_STAGES * 3);
    for (int i = 0; i < AJAZZ_DPI_STAGES; i++)
        for (int c = 0; c < 3; c++) p->dpi_color[i][c] = (uint8_t)tmp[i * 3 + c];
    p->dpi_selected = json_get_int(t, "dpi_selected", 0);
    if (p->dpi_selected < 0 || p->dpi_selected >= AJAZZ_DPI_STAGES)
        p->dpi_selected = 0;
    p->dpi_count = json_get_int(t, "dpi_count", 5);
    if (p->dpi_count < 1 || p->dpi_count > AJAZZ_DPI_STAGES)
        p->dpi_count = 5;
    p->rate_hz = json_get_int(t, "rate_hz", 1000);
    if (p->rate_hz < 125 || p->rate_hz > 1000)
        p->rate_hz = 1000;

    p->light.effect = json_get_int(t, "l_effect", LFX_STEADY);
    if (p->light.effect < 0 || p->light.effect >= LFX_COUNT) p->light.effect = LFX_STEADY;
    p->light.brightness = CLAMP(json_get_int(t, "l_brightness", 4), 0, 4);
    p->light.speed = CLAMP(json_get_int(t, "l_speed", 2), 0, 4);
    p->light.soft_transition = json_get_int(t, "l_transition", 0) ? 1 : 0;
    p->light.palette_n = json_get_int(t, "l_palette_n", LIGHT_PALETTE_MAX);
    if (p->light.palette_n < 1 || p->light.palette_n > LIGHT_PALETTE_MAX) p->light.palette_n = LIGHT_PALETTE_MAX;
    memset(tmp, 0, sizeof(tmp));
    json_get_int_array(t, "l_color", tmp, 3);
    for (int c = 0; c < 3; c++) p->light.color[c] = (uint8_t)tmp[c];
    memset(tmp, 0, sizeof(tmp));
    json_get_int_array(t, "l_palette", tmp, LIGHT_PALETTE_MAX * 3);
    for (int i = 0; i < LIGHT_PALETTE_MAX; i++)
        for (int c = 0; c < 3; c++) p->light.palette[i][c] = (uint8_t)tmp[i * 3 + c];

    p->sensor.liftoff_mm = json_get_int(t, "s_liftoff", 1) >= 2 ? 2 : 1;
    p->sensor.angle_snap = json_get_int(t, "s_angle", 0);
    p->sensor.ripple = json_get_int(t, "s_ripple", 0);
    p->sensor.motion_sync = json_get_int(t, "s_msync", 1);
    p->sensor.sleep_seconds = CLAMP(json_get_int(t, "s_sleep", 300), 0, 2550);
    p->sensor.wake_on_move = json_get_int(t, "s_wake", 1);
    p->sensor.idle_light_off = json_get_int(t, "s_idle", 0);
    p->sensor.debounce_ms = CLAMP(json_get_int(t, "s_debounce", 8), 0, 255);
    p->sensor.valid = json_get_int(t, "s_valid", 0) != 0;

    memset(tmp, 0, sizeof(tmp));
    json_get_int_array(t, "buttons", tmp, AJAZZ_KEY_SLOTS * 3);
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        for (int c = 0; c < 3; c++) p->buttons[i][c] = (uint8_t)tmp[i * 3 + c];

    for (int s = 0; s < AJAZZ_KEY_SLOTS; s++) {
        char k[24];
        AppMacro *m = &p->macro[s];
        snprintf(k, sizeof(k), "m%d_name", s);
        if (!json_get_string(t, k, m->name, sizeof(m->name)) || !m->name[0]) continue;
        snprintf(k, sizeof(k), "m%d_loop", s); m->loop_mode = CLAMP(json_get_int(t, k, 0), 0, 3);
        snprintf(k, sizeof(k), "m%d_loops", s); m->loop_count = CLAMP(json_get_int(t, k, 1), 1, 255);
        snprintf(k, sizeof(k), "m%d_n", s);
        int n = json_get_int(t, k, 0);
        if (n < 0) n = 0;
        if (n > AJAZZ_MACRO_MAX_CHUNKED_STEPS) n = AJAZZ_MACRO_MAX_CHUNKED_STEPS;
        int kinds[AJAZZ_MACRO_MAX_CHUNKED_STEPS] = {0}, delays[AJAZZ_MACRO_MAX_CHUNKED_STEPS] = {0}, vals[AJAZZ_MACRO_MAX_CHUNKED_STEPS] = {0};
        snprintf(k, sizeof(k), "m%d_kinds", s); json_get_int_array(t, k, kinds, n);
        snprintf(k, sizeof(k), "m%d_delays", s); json_get_int_array(t, k, delays, n);
        snprintf(k, sizeof(k), "m%d_values", s); json_get_int_array(t, k, vals, n);
        int kept = 0;
        for (int i = 0; i < n; i++) {
            if (!ajazz_macro_kind_valid((uint8_t)kinds[i]) || delays[i] < 0 || delays[i] > 65535)
                continue;
            m->steps[kept++] = (ajazz_macro_step_t){(uint8_t)kinds[i], (uint16_t)delays[i], (uint8_t)vals[i]};
        }
        m->n_steps = kept;
        p->has_macro[s] = kept > 0;
    }
    return TRUE;
}

static gboolean profile_load_file(const char *file, Profile *p)
{
    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return FALSE;
    char *path = g_build_filename(dir, file, NULL);
    char *t = json_read_file(path);
    g_free(path); g_free(dir);
    if (!t) return FALSE;
    gboolean ok = profile_from_json(t, p);
    g_free(t);
    return ok;
}

static gboolean profile_save(const Profile *p)
{
    char *path = profile_path(p->name);
    if (!path) return FALSE;
    char *js = profile_to_json(p);
    gboolean ok = json_write_file(path, js);
    g_free(js);
    g_free(path);
    return ok;
}

/* ---------------- capture & apply ---------------- */

static void profile_capture(App *app, const char *name, Profile *p)
{
    memset(p, 0, sizeof(*p));
    g_strlcpy(p->name, name, sizeof(p->name));
    memcpy(p->dpi, app->dpi_cache, sizeof(p->dpi));
    memcpy(p->dpi_color, app->dpi_color_cache, sizeof(p->dpi_color));
    p->dpi_selected = app->dpi_selected;
    p->dpi_count = app->dpi_count;
    p->rate_hz = app->rate_hz > 0 ? app->rate_hz : 1000;
    p->light = app->light;
    p->sensor = app->sensor;
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++) {
        memcpy(p->buttons[i], app->key_table[i], 3);
        if (p->buttons[i][0] == 0x90 && app->macro_slot_name[i][0]) {
            AppMacro *m = macros_find(app, app->macro_slot_name[i]);
            if (m) {
                p->macro[i] = *m;
                p->has_macro[i] = TRUE;
            }
        }
    }
}

typedef struct {
    Profile p;
    int rc;
} ApplyJob;

/* One ordered job for the parts that depend on each other: macro content
 * first, then the button table that points at it. */
static void buttons_work(App *app, gpointer d)
{
    ApplyJob *j = d;
    j->rc = 0;
    ajazz_report_t reply;
    if (ajazz_query_mt(app, AJAZZ_QUERY_KEY_TABLE, &reply, 1000) != 0) { j->rc = -1; return; }

    for (int s = 0; s < AJAZZ_KEY_SLOTS; s++) {
        if (!j->p.has_macro[s]) continue;
        ajazz_report_t chunks[16];
        int n = ajazz_build_macro_content_chunked(chunks, 16, (uint8_t)s, j->p.macro[s].steps, j->p.macro[s].n_steps);
        for (int i = 0; i < n; i++)
            if (ajazz_send_and_wait_mt(app, &chunks[i], 1000) != 0) { j->rc = -1; return; }
    }
    ajazz_report_t req;
    ajazz_build_key_table_from_query(&req, &reply);
    for (int i = 0; i < AJAZZ_KEY_SLOTS; i++)
        memcpy(&req.payload[i * 3], j->p.buttons[i], 3);
    ajazz_finalize(&req);
    if (ajazz_send_and_wait_mt(app, &req, 1000) != 0)
        j->rc = -1;
}

static gboolean refresh_later(gpointer user_data)
{
    app_refresh_all(user_data);
    return G_SOURCE_REMOVE;
}

static void buttons_done(App *app, gpointer d)
{
    ApplyJob *j = d;
    if (j->rc != 0)
        toast(app, _("Some button settings couldn't be applied."));
    for (int s = 0; s < AJAZZ_KEY_SLOTS; s++)
        if (j->p.has_macro[s])
            g_strlcpy(app->macro_slot_name[s], j->p.macro[s].name, sizeof(app->macro_slot_name[s]));
    g_timeout_add(900, refresh_later, app);
}

static gboolean profile_apply(App *app, const Profile *p)
{
    if (!app->connected)
        return FALSE;
    app->dpi_count = p->dpi_count;
    performance_apply_profile(app, p->dpi, p->dpi_color, p->dpi_selected, p->rate_hz, &p->sensor);
    lighting_apply(app, &p->light);

    ApplyJob *j = g_new0(ApplyJob, 1);
    j->p = *p;
    app_async(app, buttons_work, buttons_done, j, g_free);

    g_strlcpy(app->settings_last_profile, p->name, sizeof(app->settings_last_profile));
    settings_save(app);
    return TRUE;
}

gboolean profiles_apply_by_name(App *app, const char *name)
{
    GDir *d;
    char *dir = ajazzy_config_subdir("profiles");
    if (!dir) return FALSE;
    d = g_dir_open(dir, 0, NULL);
    g_free(dir);
    if (!d) return FALSE;
    const char *e;
    gboolean ok = FALSE;
    while (!ok && (e = g_dir_read_name(d))) {
        Profile p;
        if (g_str_has_suffix(e, ".json") && profile_load_file(e, &p) && strcmp(p.name, name) == 0)
            ok = profile_apply(app, &p);
    }
    g_dir_close(d);
    return ok;
}

GPtrArray *profiles_list_names(void)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    char *dir = ajazzy_config_subdir("profiles");
    GDir *d = dir ? g_dir_open(dir, 0, NULL) : NULL;
    const char *e;
    while (d && (e = g_dir_read_name(d))) {
        Profile p;
        if (g_str_has_suffix(e, ".json") && profile_load_file(e, &p))
            g_ptr_array_add(a, g_strdup(p.name));
    }
    if (d) g_dir_close(d);
    g_free(dir);
    return a;
}

/* ---------------- cards ---------------- */

static void populate(App *app);

static void on_apply_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = ((gpointer *)user_data)[0];
    const char *file = ((gpointer *)user_data)[1];
    Profile p;
    if (!profile_load_file(file, &p)) { toast(app, _("Couldn't read that profile.")); return; }
    if (profile_apply(app, &p))
        toastf(app, _("Applying \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6"), p.name);
    else
        toast(app, _("Connect the mouse first."));
    populate(app);
}

static void on_delete_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = ((gpointer *)user_data)[0];
    const char *file = ((gpointer *)user_data)[1];
    char *dir = ajazzy_config_subdir("profiles");
    if (dir) {
        char *path = g_build_filename(dir, file, NULL);
        g_remove(path);
        g_free(path);
        g_free(dir);
    }
    toast(app, _("Profile deleted."));
    populate(app);
}

static void on_export_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    gpointer *d = user_data;
    App *app = d[0];
    char *file = d[1];
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (f) {
        char *dir = ajazzy_config_subdir("profiles");
        char *path = dir ? g_build_filename(dir, file, NULL) : NULL;
        char *text = path ? json_read_file(path) : NULL;
        if (text && g_file_replace_contents(f, text, strlen(text), NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, NULL))
            toast(app, _("Profile exported."));
        else
            toast(app, _("Couldn't write the file."));
        g_free(text); g_free(path); g_free(dir);
        g_object_unref(f);
    }
    g_free(file);
    g_free(d);
}

static void on_export_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    gpointer *src = user_data;
    gpointer *d = g_new(gpointer, 2);
    d[0] = src[0];
    d[1] = g_strdup(src[1]);
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Export profile"));
    gtk_file_dialog_set_initial_name(dlg, src[1]);
    gtk_file_dialog_save(dlg, GTK_WINDOW(((App *)src[0])->win), NULL, on_export_done, d);
    g_object_unref(dlg);
}

static void on_import_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    App *app = user_data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f) return;
    char *text = NULL;
    Profile p;
    if (g_file_load_contents(f, NULL, &text, NULL, NULL, NULL) && profile_from_json(text, &p) && profile_save(&p)) {
        toastf(app, _("Imported \xE2\x80\x9C%s\xE2\x80\x9D."), p.name);
        populate(app);
    } else {
        toast(app, _("That file isn't an Ajazzy profile."));
    }
    g_free(text);
    g_object_unref(f);
}

static void on_import_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Import profile"));
    gtk_file_dialog_open(dlg, GTK_WINDOW(((App *)user_data)->win), NULL, on_import_done, user_data);
    g_object_unref(dlg);
}

static void free_pair(gpointer data, GClosure *c)
{
    (void)c;
    gpointer *p = data;
    g_free(p[1]);
    g_free(p);
}

static gpointer *pair(App *app, const char *file)
{
    gpointer *d = g_new(gpointer, 2);
    d[0] = app;
    d[1] = g_strdup(file);
    return d;
}

static void populate(App *app)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(g_flow)))
        gtk_flow_box_remove(GTK_FLOW_BOX(g_flow), c);

    char *dir = ajazzy_config_subdir("profiles");
    GDir *d = dir ? g_dir_open(dir, 0, NULL) : NULL;
    int count = 0;
    const char *e;
    while (d && (e = g_dir_read_name(d))) {
        Profile p;
        if (!g_str_has_suffix(e, ".json") || !profile_load_file(e, &p))
            continue;
        count++;

        GtkWidget *cb;
        GtkWidget *card = ui_card_new(NULL, NULL, &cb);
        gtk_widget_set_size_request(card, 280, -1);

        GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(top, "aj-card-head");
        GtkWidget *name = gtk_label_new(p.name);
        gtk_widget_add_css_class(name, "aj-card-title");
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(top), name);
        if (strcmp(app->settings_last_profile, p.name) == 0)
            gtk_box_append(GTK_BOX(top), ui_chip_new(_("Last applied"), UI_CHIP_ACCENT));
        gtk_box_append(GTK_BOX(cb), top);

        int macros = 0;
        for (int s = 0; s < AJAZZ_KEY_SLOTS; s++) macros += p.has_macro[s];
        char l1[96], l2[96], l3[96];
        snprintf(l1, sizeof(l1), _("%u DPI \xC2\xB7 %d Hz"), p.dpi[p.dpi_selected < AJAZZ_DPI_STAGES ? p.dpi_selected : 0], p.rate_hz);
        snprintf(l2, sizeof(l2), _("Lighting: %s"), lighting_effect_name(p.light.effect));
        snprintf(l3, sizeof(l3), macros ? _("%d macro(s) included") : _("No macros"), macros);
        GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_widget_set_margin_start(info, 18);
        gtk_widget_set_margin_end(info, 18);
        const char *ls[] = {l1, l2, l3};
        for (int i = 0; i < 3; i++) {
            GtkWidget *l = gtk_label_new(ls[i]);
            gtk_widget_add_css_class(l, "aj-card-desc");
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(info), l);
        }
        /* DPI colour dots */
        GtkWidget *dots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_margin_top(dots, 6);
        for (int i = 0; i < AJAZZ_DPI_STAGES; i++) {
            GtkWidget *dot = ui_color_dot_new(p.dpi_color[i], 12);
            gtk_box_append(GTK_BOX(dots), dot);
        }
        gtk_box_append(GTK_BOX(info), dots);
        gtk_box_append(GTK_BOX(cb), info);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(row, 18);
        gtk_widget_set_margin_end(row, 14);
        gtk_widget_set_margin_top(row, 14);
        gtk_widget_set_margin_bottom(row, 16);
        GtkWidget *apply = ui_button_new(_("Apply"), "object-select-symbolic", "primary");
        gtk_widget_set_hexpand(apply, TRUE);
        gtk_widget_set_sensitive(apply, app->connected);
        g_signal_connect_data(apply, "clicked", G_CALLBACK(on_apply_clicked), pair(app, e), free_pair, 0);
        GtkWidget *exp = gtk_button_new_from_icon_name("document-save-as-symbolic");
        gtk_widget_set_tooltip_text(exp, _("Export\xE2\x80\xA6"));
        gtk_widget_add_css_class(exp, "flat");
        g_signal_connect_data(exp, "clicked", G_CALLBACK(on_export_clicked), pair(app, e), free_pair, 0);
        GtkWidget *del = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_set_tooltip_text(del, _("Delete"));
        gtk_widget_add_css_class(del, "flat");
        g_signal_connect_data(del, "clicked", G_CALLBACK(on_delete_clicked), pair(app, e), free_pair, 0);
        gtk_box_append(GTK_BOX(row), apply);
        gtk_box_append(GTK_BOX(row), exp);
        gtk_box_append(GTK_BOX(row), del);
        gtk_box_append(GTK_BOX(cb), row);

        gtk_flow_box_append(GTK_FLOW_BOX(g_flow), card);
    }
    if (d) g_dir_close(d);
    g_free(dir);

    gtk_widget_set_visible(g_empty, count == 0);
    gtk_widget_set_visible(g_flow, count > 0);
}

void refresh_profiles(App *app)
{
    populate(app);
}

/* ---------------- save dialog ---------------- */

static void on_save_response(AdwAlertDialog *dlg, const char *resp, gpointer user_data)
{
    (void)dlg;
    App *app = user_data;
    if (strcmp(resp, "save") != 0) return;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(g_save_entry));
    if (!name || !*name) { toast(app, _("Give the profile a name.")); return; }
    Profile p;
    profile_capture(app, name, &p);
    if (profile_save(&p)) {
        g_strlcpy(app->settings_last_profile, p.name, sizeof(app->settings_last_profile));
        settings_save(app);
        toastf(app, _("Saved \xE2\x80\x9C%s\xE2\x80\x9D."), p.name);
        populate(app);
    } else {
        toast(app, _("Couldn't save the profile."));
    }
}

static void on_save_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    App *app = user_data;
    if (!app->connected) {
        toast(app, _("Connect the mouse to save its current settings."));
        return;
    }
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Save current settings"),
        _("Captures DPI, report rate, sensor and power settings, lighting, buttons and their macros.")));
    g_save_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_save_entry), _("Profile name, e.g. FPS, Work, Travel"));
    gtk_entry_set_activates_default(GTK_ENTRY(g_save_entry), TRUE);
    adw_alert_dialog_set_extra_child(dlg, g_save_entry);
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "save", _("Save"));
    adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "save");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_save_response), app);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->win));
}

GtkWidget *build_profiles_page(App *app)
{
    g_app = app;
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *imp = ui_button_new(_("Import"), "document-open-symbolic", "secondary");
    g_signal_connect(imp, "clicked", G_CALLBACK(on_import_clicked), app);
    GtkWidget *save = ui_button_new(_("Save current settings"), "document-save-symbolic", "primary");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), app);
    gtk_box_append(GTK_BOX(actions), imp);
    gtk_box_append(GTK_BOX(actions), save);

    GtkWidget *body;
    GtkWidget *page = ui_page_new(_("Profiles"),
        _("Switch the whole mouse between setups \xE2\x80\x94 for a game, for work, for travel."), actions, &body);

    g_empty = ui_empty_new("document-save-symbolic", _("No profiles yet"),
        _("Set the mouse up the way you like, then press \xE2\x80\x9CSave current settings\xE2\x80\x9D."));
    gtk_box_append(GTK_BOX(body), g_empty);

    g_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(g_flow), TRUE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_flow), 14);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_flow), 14);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_flow), 3);
    gtk_box_append(GTK_BOX(body), g_flow);

    populate(app);
    return page;
}
