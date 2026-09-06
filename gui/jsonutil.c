/* See jsonutil.h for why this is a small hand-rolled reader/writer
 * rather than a general JSON library. */
#include "jsonutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *ajazzy_config_subdir(const char *subdir)
{
    const char *base = g_get_user_config_dir(); /* $XDG_CONFIG_HOME or ~/.config */
    char *dir = g_build_filename(base, "ajazzy", subdir, NULL);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return NULL;
    }
    return dir;
}

gboolean json_write_file(const char *path, const char *text)
{
    GError *err = NULL;
    gboolean ok = g_file_set_contents(path, text, -1, &err);
    if (!ok) {
        g_warning("failed to write %s: %s", path, err ? err->message : "?");
        if (err) g_error_free(err);
    }
    return ok;
}

char *json_read_file(const char *path)
{
    gchar *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL))
        return NULL;
    return contents;
}

void json_put_int_array(GString *out, const char *key, const int *values, int n)
{
    g_string_append_printf(out, "\"%s\": [", key);
    for (int i = 0; i < n; i++)
        g_string_append_printf(out, "%s%d", i ? "," : "", values[i]);
    g_string_append(out, "]");
}

void json_put_int(GString *out, const char *key, int value)
{
    g_string_append_printf(out, "\"%s\": %d", key, value);
}

void json_put_string(GString *out, const char *key, const char *value)
{
    char *escaped = g_strescape(value, NULL);
    g_string_append_printf(out, "\"%s\": \"%s\"", key, escaped);
    g_free(escaped);
}

/* Finds the substring `"key":` (tolerating a space after the colon,
 * which is all this file's own writer ever emits) and returns a pointer
 * just past it, or NULL. */
static const char *find_key(const char *text, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(text, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int json_get_int_array(const char *text, const char *key, int *out, int max_n)
{
    const char *p = find_key(text, key);
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max_n) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break; /* not a number -- malformed, stop here */
        out[n++] = (int)v;
        p = end;
        while (*p == ' ' || *p == ',' ) p++;
    }
    return n;
}

int json_get_int(const char *text, const char *key, int def)
{
    const char *p = find_key(text, key);
    if (!p) return def;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return def;
    return (int)v;
}

gboolean json_get_string(const char *text, const char *key, char *out, size_t outlen)
{
    const char *p = find_key(text, key);
    if (!p || *p != '"') return FALSE;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outlen) {
        if (*p == '\\' && p[1]) {
            p++;
            char c = *p;
            switch (c) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                default: out[i++] = c; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return TRUE;
}
