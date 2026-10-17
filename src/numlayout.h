#ifndef NUMLAYOUT_H
#define NUMLAYOUT_H
/* Hit circle numbers: which image file a digit comes from, and where each digit goes. Pure C, no SDL,
 * so tests/test_skin.c can check it without a window.
 *
 * Naming (osu! stable): the digit d of a font is the image "<prefix>-<d>", looked up with the usual
 * name@2x.png / name@2x.jpg / name.png / name.jpg order, ignoring case, prefix may contain sub-folders
 * ("fonts/hit/score" -> fonts/hit/score-0.png). skin.ini [Fonts] HitCirclePrefix sets the prefix, default "default".
 *
 * Spacing: every digit keeps its own width (skins are free to make "1" narrower than "0"). The next digit
 * starts (width - overlap) after the previous one starts, so a positive HitCircleOverlap pulls digits
 * together and a negative one spreads them. Overlap is in logical (@1x) skin pixels, like the sprites, and
 * is scaled together with the digits. */
#include "skinfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NL_DEFAULT_PREFIX "default"

/* where the circle digit scale falls back to when the digits are drawn by the TTF fallback (legacy factor) */
#define NL_LEGACY_SCALE 0.9f

static inline const char *nl_prefix_or_default(const char *prefix) {
    return (prefix && *prefix) ? prefix : NL_DEFAULT_PREFIX;
}

/* "<prefix>-<d>"; returns 1 when it fits in out */
static inline int nl_digit_name(const char *prefix, int d, char *out, size_t n) {
    if (d < 0 || d > 9) return 0;
    int w = snprintf(out, n, "%s-%d", nl_prefix_or_default(prefix), d);
    return w > 0 && (size_t)w < n;
}

/* skin.ini value -> prefix: Windows separators become '/', anything else is kept (skinfs sandboxes the path later) */
static inline void nl_copy_prefix(char *dst, size_t n, const char *v) {
    snprintf(dst, n, "%s", v);
    for (char *p = dst; *p; p++) if (*p == '\\') *p = '/';
}

/* skin.ini integer (HitCircleOverlap etc., may be negative). Returns 1 and sets *out only for a clean integer. */
static inline int nl_parse_overlap(const char *v, int *out) {
    char *end;
    long x = strtol(v, &end, 10);
    if (end == v) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end) return 0;
    if (x < -10000) x = -10000;
    if (x > 10000) x = 10000;
    *out = (int)x;
    return 1;
}

/* Which file provides digit d: NL_FROM_PREFIX (<prefix>-d), NL_FROM_DEFAULT (default-d, used when the prefix
 * is custom and its file does not exist) or NL_MISSING (the caller draws the digit from a font). */
enum { NL_MISSING = 0, NL_FROM_PREFIX = 1, NL_FROM_DEFAULT = 2 };

static inline int nl_digit_source(const char *dir, const char *prefix, int d) {
    char name[256], path[1700]; int sc;
    if (!dir) return NL_MISSING;
    if (nl_digit_name(prefix, d, name, sizeof name))
        for (int i = 0; i < SKINFS_N_IMG; i++)
            if (skinfs_image_candidate(dir, name, i, &sc, path, sizeof path)) return NL_FROM_PREFIX;
    if (strcasecmp(nl_prefix_or_default(prefix), NL_DEFAULT_PREFIX) &&
        nl_digit_name(NL_DEFAULT_PREFIX, d, name, sizeof name))
        for (int i = 0; i < SKINFS_N_IMG; i++)
            if (skinfs_image_candidate(dir, name, i, &sc, path, sizeof path)) return NL_FROM_DEFAULT;
    return NL_MISSING;
}

/* Layout of a run of n digits whose logical widths are w[]. left[i] receives the x of digit i's left edge
 * relative to the start of the run (already multiplied by scale); the return value is the run's total
 * width (scaled). n <= 0 -> 0. */
static inline float nl_layout(const float *w, int n, float overlap, float scale, float *left) {
    float x = 0, right = 0;
    for (int i = 0; i < n; i++) {
        if (left) left[i] = x * scale;
        right = x + w[i];
        x += w[i] - overlap;
    }
    return n > 0 ? right * scale : 0;
}

#endif
