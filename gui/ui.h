/*
 * Small UI kit shared by every page, so the whole app reads as one product:
 * the same page header, the same card, the same setting row, the same
 * status chip. Pages never hand-roll these -- if a page needs something
 * the kit lacks, add it here rather than styling it inline.
 *
 * Visual language lives in gui/style.c (CSS); this file is only structure.
 */
#ifndef AJAZZY_GUI_UI_H
#define AJAZZY_GUI_UI_H

#include <adwaita.h>

/* The CSS for the whole app, installed once from main.c. */
extern const char *AJAZZ_CSS;

typedef enum {
    UI_CHIP_NEUTRAL,
    UI_CHIP_OK,
    UI_CHIP_WARN,
    UI_CHIP_ERROR,
    UI_CHIP_ACCENT,
} UiChipKind;

/*
 * A scrolling page: title + subtitle + an optional widget on the right of
 * the header (e.g. a primary action), then a vertical body that cards are
 * appended to. Returns the outermost widget to add to the stack; *body is
 * the box to append cards to. `actions` may be NULL.
 */
GtkWidget *ui_page_new(const char *title, const char *subtitle, GtkWidget *actions, GtkWidget **body);

/*
 * A titled card. `title`/`description` may be NULL. *body is the box rows
 * and controls go into (zero padding -- rows bring their own, free-form
 * content should use ui_card_pad()).
 */
GtkWidget *ui_card_new(const char *title, const char *description, GtkWidget **body);
/* Wraps free-form content in the card's standard inner padding. */
GtkWidget *ui_card_pad(GtkWidget *child);

/* A list that holds ui_row()s inside a card, with hairline separators. */
GtkWidget *ui_list_new(void);

/* One setting row: title (+ optional subtitle) on the left, `control` on
 * the right. Returns the AdwActionRow so callers can keep a handle for
 * show/hide or subtitle updates. `control` may be NULL for a plain info row. */
AdwActionRow *ui_row_new(const char *title, const char *subtitle, GtkWidget *control);

/* Status chip: a small rounded pill with text, coloured by kind. */
GtkWidget *ui_chip_new(const char *text, UiChipKind kind);
void ui_chip_set(GtkWidget *chip, const char *text, UiChipKind kind);

/* Buttons. icon may be NULL. kind is one of: "primary", "secondary",
 * "ghost", "danger". */
GtkWidget *ui_button_new(const char *label, const char *icon, const char *kind);

/* Segmented control: linked, mutually-exclusive toggle buttons. Calls
 * `cb(index, user_data)` on user selection only (not on ui_segmented_set()).
 * Returns the container. */
typedef void (*UiSegmentedCb)(int index, gpointer user_data);
GtkWidget *ui_segmented_new(const char *const *labels, int n, int selected, UiSegmentedCb cb, gpointer user_data);
void ui_segmented_set(GtkWidget *seg, int index);
int ui_segmented_get(GtkWidget *seg);

/* Empty-state block: icon, headline and a hint line. */
GtkWidget *ui_empty_new(const char *icon, const char *title, const char *hint);

/* Section label above a group of cards ("Performance", "Power", ...). */
GtkWidget *ui_section_label(const char *text);

/* Debounces a burst of changes (a dragged slider, a spinning number
 * field) into one call, `delay_ms` after the last one. Keep one
 * UiDebounce per control. */
typedef struct UiDebounce UiDebounce;
UiDebounce *ui_debounce_new(guint delay_ms, GSourceFunc fn, gpointer user_data);
void ui_debounce_poke(UiDebounce *d);
void ui_debounce_cancel(UiDebounce *d);

/* Colour helpers: 0-255 triple <-> GdkRGBA. */
void ui_rgba_from_rgb(GdkRGBA *out, const uint8_t rgb[3]);
void ui_rgb_from_rgba(uint8_t out[3], const GdkRGBA *rgba);

/* Full path of gui/images/mouse_<stem>.png wherever it is installed (system
 * data dirs, next to the binary, or the source tree), or NULL. Caller frees. */
char *ui_find_mouse_image(const char *stem);
/* The picture scaled to fit w x h (aspect kept) as a texture whose intrinsic size is
 * exactly that, so layout code can position things over it. NULL if not found. */
GdkTexture *ui_mouse_texture(const char *stem, int w, int h);

/* CSS-class toggle that is a no-op when already in the requested state. */
void ui_set_class(GtkWidget *w, const char *css_class, gboolean on);

#endif
