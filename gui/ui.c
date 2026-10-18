#define _DEFAULT_SOURCE
#include <glib/gi18n.h>
#include <string.h>

#include "ui.h"
#include <unistd.h>

#ifndef IMAGEDIR
#define IMAGEDIR "gui/images"
#endif

void ui_set_class(GtkWidget *w, const char *css_class, gboolean on)
{
    if (!w)
        return;
    if (on)
        gtk_widget_add_css_class(w, css_class);
    else
        gtk_widget_remove_css_class(w, css_class);
}

GtkWidget *ui_page_new(const char *title, const char *subtitle, GtkWidget *actions, GtkWidget **body)
{
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(content, 28);
    gtk_widget_set_margin_bottom(content, 32);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_bottom(header, 4);

    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(texts, TRUE);
    gtk_widget_set_valign(texts, GTK_ALIGN_CENTER);
    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "aj-page-title");
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    gtk_box_append(GTK_BOX(texts), t);
    if (subtitle && *subtitle) {
        GtkWidget *s = gtk_label_new(subtitle);
        gtk_widget_add_css_class(s, "aj-page-sub");
        gtk_widget_set_halign(s, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(s), 0);
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_box_append(GTK_BOX(texts), s);
    }
    gtk_box_append(GTK_BOX(header), texts);
    if (actions) {
        gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(header), actions);
    }
    gtk_box_append(GTK_BOX(content), header);

    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_box_append(GTK_BOX(content), b);
    if (body)
        *body = b;

    /* hexpand so every page takes the same column width instead of its own natural width */
    gtk_widget_set_hexpand(content, TRUE);
    AdwClamp *clamp = ADW_CLAMP(adw_clamp_new());
    adw_clamp_set_maximum_size(clamp, 980);
    adw_clamp_set_tightening_threshold(clamp, 720);
    adw_clamp_set_child(clamp, content);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), GTK_WIDGET(clamp));
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    return scroller;
}

GtkWidget *ui_card_new(const char *title, const char *description, GtkWidget **body)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card, "aj-card");

    if ((title && *title) || (description && *description)) {
        GtkWidget *head = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_add_css_class(head, "aj-card-head");
        if (title && *title) {
            GtkWidget *t = gtk_label_new(title);
            gtk_widget_add_css_class(t, "aj-card-title");
            gtk_widget_set_halign(t, GTK_ALIGN_START);
            gtk_label_set_xalign(GTK_LABEL(t), 0);
            gtk_box_append(GTK_BOX(head), t);
        }
        if (description && *description) {
            GtkWidget *d = gtk_label_new(description);
            gtk_widget_add_css_class(d, "aj-card-desc");
            gtk_widget_set_halign(d, GTK_ALIGN_START);
            gtk_label_set_xalign(GTK_LABEL(d), 0);
            gtk_label_set_wrap(GTK_LABEL(d), TRUE);
            gtk_box_append(GTK_BOX(head), d);
        }
        gtk_box_append(GTK_BOX(card), head);
    }

    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(card), b);
    if (body)
        *body = b;
    return card;
}

GtkWidget *ui_card_pad(GtkWidget *child)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "aj-card-pad");
    gtk_box_append(GTK_BOX(box), child);
    return box;
}

GtkWidget *ui_list_new(void)
{
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "aj-list");
    return list;
}

AdwActionRow *ui_row_new(const char *title, const char *subtitle, GtkWidget *control)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle && *subtitle)
        adw_action_row_set_subtitle(row, subtitle);
    if (control) {
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(row, control);
    }
    return row;
}

static const char *chip_class(UiChipKind kind)
{
    switch (kind) {
        case UI_CHIP_OK:     return "aj-chip-ok";
        case UI_CHIP_WARN:   return "aj-chip-warn";
        case UI_CHIP_ERROR:  return "aj-chip-error";
        case UI_CHIP_ACCENT: return "aj-chip-accent";
        default:             return "aj-chip-neutral";
    }
}

static const char *const CHIP_CLASSES[] = {
    "aj-chip-ok", "aj-chip-warn", "aj-chip-error", "aj-chip-accent", "aj-chip-neutral",
};

void ui_chip_set(GtkWidget *chip, const char *text, UiChipKind kind)
{
    gtk_label_set_text(GTK_LABEL(chip), text);
    for (guint i = 0; i < G_N_ELEMENTS(CHIP_CLASSES); i++)
        gtk_widget_remove_css_class(chip, CHIP_CLASSES[i]);
    gtk_widget_add_css_class(chip, chip_class(kind));
}

GtkWidget *ui_chip_new(const char *text, UiChipKind kind)
{
    GtkWidget *chip = gtk_label_new("");
    gtk_widget_add_css_class(chip, "aj-chip");
    gtk_widget_set_halign(chip, GTK_ALIGN_START);
    gtk_widget_set_valign(chip, GTK_ALIGN_CENTER);
    ui_chip_set(chip, text, kind);
    return chip;
}

GtkWidget *ui_button_new(const char *label, const char *icon, const char *kind)
{
    GtkWidget *btn = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    if (icon)
        gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
    if (label && *label)
        gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(btn), box);
    gtk_widget_add_css_class(btn, "aj-btn");
    if (kind && strcmp(kind, "primary") == 0)
        gtk_widget_add_css_class(btn, "aj-btn-primary");
    else if (kind && strcmp(kind, "danger") == 0)
        gtk_widget_add_css_class(btn, "aj-btn-danger");
    else if (kind && strcmp(kind, "ghost") == 0)
        gtk_widget_add_css_class(btn, "aj-btn-ghost");
    else
        gtk_widget_add_css_class(btn, "aj-btn-secondary");
    return btn;
}

/* ---------------- segmented control ---------------- */

typedef struct {
    UiSegmentedCb cb;
    gpointer user_data;
    gboolean suppress;
    int n;
} SegState;

static void seg_free(gpointer p) { g_free(p); }

static void on_seg_toggled(GtkToggleButton *b, gpointer user_data)
{
    GtkWidget *seg = user_data;
    if (!gtk_toggle_button_get_active(b))
        return;
    SegState *st = g_object_get_data(G_OBJECT(seg), "seg-state");
    if (!st || st->suppress || !st->cb)
        return;
    st->cb(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "seg-index")), st->user_data);
}

GtkWidget *ui_segmented_new(const char *const *labels, int n, int selected, UiSegmentedCb cb, gpointer user_data)
{
    GtkWidget *seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(seg, "linked");
    gtk_widget_add_css_class(seg, "aj-seg");

    SegState *st = g_new0(SegState, 1);
    st->cb = cb;
    st->user_data = user_data;
    st->n = n;
    g_object_set_data_full(G_OBJECT(seg), "seg-state", st, seg_free);

    GtkToggleButton *first = NULL;
    for (int i = 0; i < n; i++) {
        GtkWidget *b = gtk_toggle_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(b), "seg-index", GINT_TO_POINTER(i));
        if (first)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), first);
        else
            first = GTK_TOGGLE_BUTTON(b);
        if (i == selected)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
        g_signal_connect(b, "toggled", G_CALLBACK(on_seg_toggled), seg);
        gtk_box_append(GTK_BOX(seg), b);
    }
    return seg;
}

void ui_segmented_set(GtkWidget *seg, int index)
{
    SegState *st = g_object_get_data(G_OBJECT(seg), "seg-state");
    if (!st)
        return;
    st->suppress = TRUE;
    int i = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(seg); c; c = gtk_widget_get_next_sibling(c), i++)
        if (i == index)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), TRUE);
    st->suppress = FALSE;
}

int ui_segmented_get(GtkWidget *seg)
{
    int i = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(seg); c; c = gtk_widget_get_next_sibling(c), i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c)))
            return i;
    return -1;
}

GtkWidget *ui_empty_new(const char *icon, const char *title, const char *hint)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(box, "aj-empty");
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    GtkWidget *img = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 40);
    gtk_widget_add_css_class(img, "aj-empty-icon");
    gtk_box_append(GTK_BOX(box), img);
    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "aj-empty-title");
    gtk_box_append(GTK_BOX(box), t);
    if (hint && *hint) {
        GtkWidget *h = gtk_label_new(hint);
        gtk_widget_add_css_class(h, "aj-empty-hint");
        gtk_label_set_wrap(GTK_LABEL(h), TRUE);
        gtk_label_set_justify(GTK_LABEL(h), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(box), h);
    }
    return box;
}

GtkWidget *ui_section_label(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "aj-section");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

/* ---------------- debounce ---------------- */

struct UiDebounce {
    guint delay_ms;
    guint source;
    GSourceFunc fn;
    gpointer user_data;
};

static gboolean debounce_fire(gpointer p)
{
    UiDebounce *d = p;
    d->source = 0;
    d->fn(d->user_data);
    return G_SOURCE_REMOVE;
}

UiDebounce *ui_debounce_new(guint delay_ms, GSourceFunc fn, gpointer user_data)
{
    UiDebounce *d = g_new0(UiDebounce, 1);
    d->delay_ms = delay_ms;
    d->fn = fn;
    d->user_data = user_data;
    return d;
}

void ui_debounce_poke(UiDebounce *d)
{
    if (d->source)
        g_source_remove(d->source);
    d->source = g_timeout_add(d->delay_ms, debounce_fire, d);
}

void ui_debounce_cancel(UiDebounce *d)
{
    if (d->source) {
        g_source_remove(d->source);
        d->source = 0;
    }
}

void ui_rgba_from_rgb(GdkRGBA *out, const uint8_t rgb[3])
{
    out->red = rgb[0] / 255.0f;
    out->green = rgb[1] / 255.0f;
    out->blue = rgb[2] / 255.0f;
    out->alpha = 1.0f;
}

void ui_rgb_from_rgba(uint8_t out[3], const GdkRGBA *rgba)
{
    out[0] = (uint8_t)(rgba->red * 255.0f + 0.5f);
    out[1] = (uint8_t)(rgba->green * 255.0f + 0.5f);
    out[2] = (uint8_t)(rgba->blue * 255.0f + 0.5f);
}

char *ui_find_mouse_image(const char *stem)
{
    char *file = g_strdup_printf("mouse_%s.png", stem);
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(dirs, g_build_filename(g_get_user_data_dir(), "ajazzy", "images", NULL));
    const char *const *sys = g_get_system_data_dirs();
    for (; sys && *sys; sys++)
        g_ptr_array_add(dirs, g_build_filename(*sys, "ajazzy", "images", NULL));
    g_ptr_array_add(dirs, g_strdup("/usr/local/share/ajazzy/images"));
    g_ptr_array_add(dirs, g_strdup("/usr/share/ajazzy/images"));

    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *bin = g_path_get_dirname(exe);
        g_ptr_array_add(dirs, g_build_filename(bin, "images", NULL));
        g_ptr_array_add(dirs, g_build_filename(bin, "..", "share", "ajazzy", "images", NULL));
        g_free(bin);
    }
    g_ptr_array_add(dirs, g_strdup(IMAGEDIR));

    char *found = NULL;
    for (guint i = 0; i < dirs->len && !found; i++) {
        char *p = g_build_filename(g_ptr_array_index(dirs, i), file, NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS))
            found = p;
        else
            g_free(p);
    }
    g_ptr_array_free(dirs, TRUE);
    g_free(file);
    return found;
}

GdkTexture *ui_mouse_texture(const char *stem, int w, int h)
{
    char *path = ui_find_mouse_image(stem);
    if (!path)
        return NULL;
    /* Decoded with cairo's own PNG reader on purpose: gdk-pixbuf goes through the
     * glycin sandbox, which starts a helper process (bwrap + loader, ~10 MB) just to
     * read a 100 KB file. */
    cairo_surface_t *src = cairo_image_surface_create_from_png(path);
    g_free(path);
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return NULL;
    }
    int sw = cairo_image_surface_get_width(src), sh = cairo_image_surface_get_height(src);
    double scale = MIN((double)w / sw, (double)h / sh);
    int dw = MAX(1, (int)(sw * scale)), dh = MAX(1, (int)(sh * scale));

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    cairo_t *cr = cairo_create(dst);
    cairo_scale(cr, (double)dw / sw, (double)dh / sh);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(dst);

    gsize stride = (gsize)cairo_image_surface_get_stride(dst);
    GBytes *bytes = g_bytes_new(cairo_image_surface_get_data(dst), stride * (gsize)dh);
    cairo_surface_destroy(dst);
    /* cairo ARGB32 is premultiplied BGRA in memory on little-endian machines */
    GdkTexture *tex = gdk_memory_texture_new(dw, dh, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, stride);
    g_bytes_unref(bytes);
    return tex;
}

static void draw_color_dot(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer data)
{
    (void)a;
    const uint8_t *rgb = data;
    cairo_arc(cr, w / 2.0, h / 2.0, MIN(w, h) / 2.0, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
    cairo_fill(cr);
}

GtkWidget *ui_color_dot_new(const uint8_t rgb[3], int size)
{
    GtkWidget *dot = gtk_drawing_area_new();
    gtk_widget_set_size_request(dot, size, size);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
    uint8_t *copy = g_memdup2(rgb, 3);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dot), draw_color_dot, copy, g_free);
    return dot;
}
