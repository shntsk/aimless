/* Tolerant skin.ini reader. See skinini.h. */
#include "skinini.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_INI (4L << 20) /* a skin.ini larger than 4 MB is not a skin.ini */

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void handle_line(char *line, char *section, size_t sn, skinini_cb cb, void *ud) {
    char *s = trim(line);
    if (!*s || (s[0] == '/' && s[1] == '/')) return;
    /* trailing comment: "//" preceded by whitespace (keeps "http://..." style values intact) */
    for (char *p = s + 1; *p; p++)
        if (p[0] == '/' && p[1] == '/' && isspace((unsigned char)p[-1])) { *p = 0; break; }
    s = trim(s);
    if (!*s) return;
    if (*s == '[') {
        char *e = strchr(s, ']');
        if (!e) return;
        *e = 0;
        snprintf(section, sn, "%s", trim(s + 1));
        return;
    }
    char *colon = strchr(s, ':');
    if (!colon) return;
    *colon = 0;
    char *key = trim(s), *val = trim(colon + 1);
    if (!*key) return;
    cb(section, key, val, ud);
}

void skinini_parse(const char *buf, long len, skinini_cb cb, void *ud) {
    if (!buf || len <= 0) return;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) { buf += 3; len -= 3; }
    char section[64] = "";
    long i = 0;
    while (i < len) {
        long j = i;
        while (j < len && buf[j] != '\n' && buf[j] != '\r') j++;
        char *line = malloc((size_t)(j - i) + 1);
        if (!line) return;
        memcpy(line, buf + i, (size_t)(j - i));
        line[j - i] = 0;
        /* embedded NULs would silently truncate a line; that only affects broken files */
        handle_line(line, section, sizeof section, cb, ud);
        free(line);
        if (j < len && buf[j] == '\r' && j + 1 < len && buf[j + 1] == '\n') j++;
        i = j + 1;
    }
}

int skinini_read(const char *path, skinini_cb cb, void *ud) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n > MAX_INI) { fclose(f); return -1; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    int bad = ferror(f);
    fclose(f);
    if (bad) { free(buf); return -1; }
    skinini_parse(buf, (long)got, cb, ud);
    free(buf);
    return 0;
}

int skinini_color(const char *value, int rgb[3]) {
    int v[4], n = 0;
    const char *p = value;
    while (*p && n < 4) {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p) return 0;
        v[n++] = x < 0 ? 0 : x > 255 ? 255 : (int)x;
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end == ',') end++; else if (*end) return 0;
        p = end;
        while (*p && isspace((unsigned char)*p)) p++;
    }
    if (n < 3) return 0;
    rgb[0] = v[0]; rgb[1] = v[1]; rgb[2] = v[2];
    return 1;
}

int skinini_bool(const char *value, int *out) {
    if (!strcasecmp(value, "true")) { *out = 1; return 1; }
    if (!strcasecmp(value, "false")) { *out = 0; return 1; }
    char *end;
    long x = strtol(value, &end, 10);
    if (end == value) return 0;
    *out = x != 0;
    return 1;
}
