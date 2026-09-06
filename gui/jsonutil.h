/*
 * Minimal, schema-specific JSON helpers for gui/page_profiles.c and
 * gui/page_lighting.c's saved presets. NOT a general JSON library --
 * there's no json-glib dependency in this project's build (see the
 * Makefile's pkg-config list), and pulling one in for a handful of flat
 * {string, int, int-array} fields would be a lot of new dependency for
 * very little. This only has to round-trip files this project itself
 * writes, in a fixed shape it controls -- so a small hand-written
 * writer/reader for exactly that shape is enough, not a general parser.
 */
#ifndef AJAZZY_GUI_JSONUTIL_H
#define AJAZZY_GUI_JSONUTIL_H

#include <glib.h>

/* Returns the path to a per-user config subdirectory under
 * ~/.config/ajazzy/<subdir>/, creating it (and its parents) if missing.
 * Caller frees. Returns NULL if the directory couldn't be created. */
char *ajazzy_config_subdir(const char *subdir);

/* Writes `text` to `path` atomically-ish (plain write, no rename dance --
 * these are small single-user config files, not something else reads
 * concurrently). Returns TRUE on success. */
gboolean json_write_file(const char *path, const char *text);

/* Reads the whole file at `path` into a newly-allocated NUL-terminated
 * string. Caller frees. Returns NULL if the file doesn't exist or can't
 * be read. */
char *json_read_file(const char *path);

/* Appends `"key": [n0,n1,...]` (no trailing comma) to `out`. */
void json_put_int_array(GString *out, const char *key, const int *values, int n);
/* Appends `"key": n` (no trailing comma). */
void json_put_int(GString *out, const char *key, int value);
/* Appends `"key": "escaped string"` (no trailing comma). */
void json_put_string(GString *out, const char *key, const char *value);

/* Finds `"key":[...]` in `text` and fills `out` with up to `max_n` comma-
 * separated integers found inside the brackets. Returns how many were
 * found (0 if the key wasn't present). */
int json_get_int_array(const char *text, const char *key, int *out, int max_n);
/* Finds `"key": n` in `text`; returns `def` if not present/parseable. */
int json_get_int(const char *text, const char *key, int def);
/* Finds `"key": "..."` in `text`, unescapes it into `out` (up to outlen-1
 * bytes). Returns TRUE if found. */
gboolean json_get_string(const char *text, const char *key, char *out, size_t outlen);

#endif
