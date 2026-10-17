/* Skin discovery tests: skin.ini parsing, file lookup, .osk import. No SDL needed.
 *   make test                         (synthetic fixtures only)
 *   make test OSK=/path/to/skin.osk   (also runs against a real skin archive)
 * These test the loading plumbing, not rendering or gameplay. */
#define _GNU_SOURCE
#include "../src/circlegeom.h"
#include "../src/numlayout.h"
#include "../src/paths.h"
#include "../src/skinfs.h"
#include "../src/skinini.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char tmproot[512];
static void sh(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void sh(const char *fmt, ...) {
    char cmd[2048]; va_list ap;
    va_start(ap, fmt); vsnprintf(cmd, sizeof cmd, fmt, ap); va_end(ap);
    if (system(cmd)) { /* callers check results through the file system */ }
}
static int exists(const char *p) { return access(p, F_OK) == 0; }

/* ---- skin.ini ---- */
typedef struct { char sec[32][64], key[32][64], val[32][128]; int n; } Got;
static void collect(const char *s, const char *k, const char *v, void *ud) {
    Got *g = ud; if (g->n >= 32) return;
    snprintf(g->sec[g->n], 64, "%s", s); snprintf(g->key[g->n], 64, "%s", k); snprintf(g->val[g->n], 128, "%s", v); g->n++;
}
static const char *find(Got *g, const char *sec, const char *key) {
    for (int i = 0; i < g->n; i++) if (!strcasecmp(g->sec[i], sec) && !strcasecmp(g->key[i], key)) return g->val[i];
    return NULL;
}

static void test_ini_synthetic(void) {
    puts("skin.ini: synthetic");
    const char *txt = "\xEF\xBB\xBF// Ini parser header\r\n\r\n[General]\r\n    Name: My Skin  // trailing\r\n"
                      "AllowSliderBallTint:1\r\n\r\n[Colours]\r\n\tCombo1 :  255 , 0,0\r\nCombo2:0,255,0,128\r\n"
                      "  // Combo3: 1,2,3\r\nSliderBorder:  10,20 ,30\n[Fonts]\nHitCirclePrefix: Fonts\\Hit\\Score\nbogus line without colon\n[Broken\nX: y\n";
    Got g = {0};
    skinini_parse(txt, (long)strlen(txt), collect, &g);
    CHECK(find(&g, "General", "Name") && !strcmp(find(&g, "General", "Name"), "My Skin"));
    CHECK(find(&g, "General", "AllowSliderBallTint") && !strcmp(find(&g, "General", "AllowSliderBallTint"), "1"));
    CHECK(find(&g, "Colours", "Combo1") != NULL);          /* BOM, CRLF, indentation, space before colon */
    CHECK(find(&g, "Colours", "Combo3") == NULL);          /* commented out */
    CHECK(find(&g, "Fonts", "HitCirclePrefix") && !strcmp(find(&g, "Fonts", "HitCirclePrefix"), "Fonts\\Hit\\Score"));
    int rgb[3];
    CHECK(skinini_color(find(&g, "Colours", "Combo1"), rgb) && rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0);
    CHECK(skinini_color(find(&g, "Colours", "Combo2"), rgb) && rgb[1] == 255);                 /* alpha ignored */
    CHECK(skinini_color(find(&g, "Colours", "SliderBorder"), rgb) && rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30);
    CHECK(!skinini_color("1,2", rgb) && !skinini_color("a,b,c", rgb) && !skinini_color("", rgb));
    CHECK(skinini_color("300,-5,7", rgb) && rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 7);      /* clamped */
    int b = -1;
    CHECK(skinini_bool("1", &b) && b == 1); CHECK(skinini_bool("0", &b) && b == 0);
    CHECK(skinini_bool("true", &b) && b == 1); CHECK(!skinini_bool("maybe", &b));
    /* a lone CR as line separator, and an empty / NULL buffer */
    Got g2 = {0}; skinini_parse("[General]\rName: A\rVersion: 2.5\r", 31, collect, &g2);
    CHECK(g2.n == 2);
    skinini_parse("", 0, collect, &g2); skinini_parse(NULL, 5, collect, &g2);
    CHECK(skinini_read("/nonexistent/skin.ini", collect, &g2) == -1);
}

/* ---- file lookup ---- */
static void test_skinfs(void) {
    puts("skinfs: case-insensitive lookup");
    char d[600], o[1700];
    snprintf(d, sizeof d, "%s/fs", tmproot);
    sh("mkdir -p '%s/Fonts/Hit' && : > '%s/Hitcircle.PNG' && : > '%s/hitcircle@2x.png' && : > '%s/Fonts/Hit/Score-0.PNG' && : > '%s/Skin.INI'", d, d, d, d, d);
    CHECK(skinfs_resolve(d, "hitcircle", ".png", o, sizeof o) && strstr(o, "Hitcircle.PNG"));
    CHECK(skinfs_resolve(d, "hitcircle", "@2x.png", o, sizeof o) && strstr(o, "hitcircle@2x.png"));    /* exact spelling */
    CHECK(skinfs_resolve(d, "HITCIRCLE", "@2X.PNG", o, sizeof o));
    CHECK(skinfs_resolve(d, "fonts/hit/score-0", ".png", o, sizeof o) && strstr(o, "Fonts/Hit/Score-0.PNG"));
    CHECK(skinfs_resolve(d, "fonts\\hit\\score-0", ".png", o, sizeof o));                              /* Windows separators */
    CHECK(skinfs_resolve(d, "skin", ".ini", o, sizeof o) && strstr(o, "Skin.INI"));
    CHECK(!skinfs_resolve(d, "hit0", ".png", o, sizeof o));                                            /* missing */
    CHECK(!skinfs_resolve(d, "fonts/hit", "", o, sizeof o));                                           /* directory, not a file */
    CHECK(!skinfs_resolve(d, "../fs/Hitcircle", ".PNG", o, sizeof o));                                 /* no escaping */
    CHECK(!skinfs_resolve(d, "fonts/../hitcircle", ".png", o, sizeof o));
    CHECK(!skinfs_resolve(d, "/etc/passwd", "", o, sizeof o));
    CHECK(!skinfs_resolve(NULL, "x", ".png", o, sizeof o) && !skinfs_resolve("", "x", ".png", o, sizeof o));
    CHECK(skinfs_isdir(d) && !skinfs_isdir("/nonexistent") && !skinfs_isdir(NULL));
    CHECK(skinfs_looks_like_skin(d));
    char e[600]; snprintf(e, sizeof e, "%s/empty", tmproot); sh("mkdir -p '%s' && : > '%s/readme.txt'", e, e);
    CHECK(!skinfs_looks_like_skin(e));
}

/* ---- circle sprites: lookup order, logical sizes, layout (no SDL: PNG size read from the IHDR chunk) ---- */
static int png_size(const char *path, int *w, int *h) {
    unsigned char b[24]; FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    if (n < sizeof b || memcmp(b, "\x89PNG", 4)) return 0;
    *w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    *h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return 1;
}

/* logical (@1x) size of an element the way skin.c's loader picks it: first existing candidate wins */
static int logical_size(const char *dir, const char *name, int *w, int *h, int *scale, char *file, size_t fn) {
    for (int i = 0; i < SKINFS_N_IMG; i++) {
        int sc = 1, pw, ph;
        if (!skinfs_image_candidate(dir, name, i, &sc, file, fn)) continue;
        if (!png_size(file, &pw, &ph)) continue;
        *w = (pw + sc - 1) / sc; *h = (ph + sc - 1) / sc; *scale = sc;
        return 1;
    }
    return 0;
}

static void test_circle_layout(void) {
    puts("circle sprites: lookup order and layout");
    char d[600], o[1700]; int sc = 0;
    snprintf(d, sizeof d, "%s/cl", tmproot);
    sh("mkdir -p '%s' && : > '%s/hitcircle.png' && : > '%s/Hitcircle@2X.PNG' && : > '%s/hitcircleoverlay.png' && : > '%s/approachcircle.JPG'", d, d, d, d, d);
    CHECK(skinfs_image_candidate(d, "hitcircle", 0, &sc, o, sizeof o) && sc == 2 && strcasestr(o, "hitcircle@2x.png"));   /* @2x first */
    CHECK(!skinfs_image_candidate(d, "hitcircle", 1, &sc, o, sizeof o));
    CHECK(skinfs_image_candidate(d, "hitcircle", 2, &sc, o, sizeof o) && sc == 1);
    CHECK(!skinfs_image_candidate(d, "hitcircleoverlay", 0, &sc, o, sizeof o));                                            /* no @2x: falls to 1x */
    CHECK(skinfs_image_candidate(d, "hitcircleoverlay", 2, &sc, o, sizeof o) && sc == 1);
    CHECK(skinfs_image_candidate(d, "approachcircle", 3, &sc, o, sizeof o) && sc == 1);                                    /* jpg, any case */
    CHECK(!skinfs_image_candidate(d, "approachcircle", 4, &sc, o, sizeof o) && !skinfs_image_candidate(d, "x", -1, &sc, o, sizeof o));

    /* layout arithmetic */
    float dia = 100;
    CHECK(cg_k(dia, 128) == 100.0f / 128 && cg_k(dia, 0) == 1.0f);
    CHECK(cg_approach(0) == 4.0f && cg_approach(1) == 1.0f && cg_approach(0.5f) == 2.5f && cg_approach(-3) == 4.0f && cg_approach(9) == 1.0f);
    int ord[3];
    cg_layers(1, ord); CHECK(ord[0] == CG_BASE && ord[1] == CG_NUMBER && ord[2] == CG_OVERLAY);   /* overlay above the number (default) */
    cg_layers(0, ord); CHECK(ord[0] == CG_BASE && ord[1] == CG_OVERLAY && ord[2] == CG_NUMBER);
}

static void test_real_circles(const char *dir) {
    puts("circle sprites: real skin");
    char f[1700]; int sc;
    int hw, hh, ow, oh, aw, ah;
    CHECK(logical_size(dir, "hitcircle", &hw, &hh, &sc, f, sizeof f) && sc == 2 && strstr(f, "hitcircle@2x.png") && hw == 128 && hh == 128);   /* 256 px file -> 128 logical */
    CHECK(logical_size(dir, "hitcircleoverlay", &ow, &oh, &sc, f, sizeof f) && sc == 1 && ow == 128 && oh == 128);               /* only 1x exists */
    CHECK(logical_size(dir, "approachcircle", &aw, &ah, &sc, f, sizeof f) && sc == 2 && aw == 192 && ah == 192);                 /* 384 px file -> 192 */
    CHECK(!skinfs_resolve(dir, "hitcircleoverlay", "@2x.png", f, sizeof f));
    /* mixed 1x/2x elements end up in proportion: overlay matches the hitcircle, approach is 1.5x */
    float k = cg_k(100, hw);
    CHECK(hw * k == 100 && ow * k == 100);
    CHECK(aw * k * cg_approach(1) == 150 && aw * k * cg_approach(0) == 600);
}


/* ---- circle numbers: names, prefix, missing digits, overlap (stage 4B). Pure logic; the loader itself is
 *      exercised by tests/test_skin_loader.c with a fake SDL ---- */
static void mkpng(const char *path, int w, int h) {
    unsigned char b[24] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                            (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                            (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h };
    FILE *f = fopen(path, "wb"); if (!f) return; fwrite(b, 1, sizeof b, f); fclose(f);
}
static int near(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }

static void test_numlayout(void) {
    puts("circle numbers: names, overlap parsing, layout");
    char n[64]; int v = 99;
    CHECK(nl_digit_name("fonts/hit/score", 3, n, sizeof n) && !strcmp(n, "fonts/hit/score-3"));
    CHECK(nl_digit_name("default", 0, n, sizeof n) && !strcmp(n, "default-0"));
    CHECK(nl_digit_name(NULL, 7, n, sizeof n) && !strcmp(n, "default-7"));            /* no prefix -> default */
    CHECK(nl_digit_name("", 7, n, sizeof n) && !strcmp(n, "default-7"));              /* empty value in skin.ini */
    CHECK(!nl_digit_name("x", 10, n, sizeof n) && !nl_digit_name("x", -1, n, sizeof n));
    CHECK(!nl_digit_name("a/very/long/prefix", 1, n, 8));                              /* does not fit: reported, not truncated */
    char pre[32]; nl_copy_prefix(pre, sizeof pre, "Fonts\\Hit\\Score"); CHECK(!strcmp(pre, "Fonts/Hit/Score"));
    CHECK(nl_parse_overlap("1", &v) && v == 1);
    CHECK(nl_parse_overlap("-2", &v) && v == -2);                                      /* negative: digits spread */
    CHECK(nl_parse_overlap("  5 ", &v) && v == 5);
    CHECK(nl_parse_overlap("0", &v) && v == 0);
    v = 7; CHECK(!nl_parse_overlap("abc", &v) && v == 7);                              /* garbage leaves the value alone */
    CHECK(!nl_parse_overlap("", &v) && !nl_parse_overlap("2.5x", &v) && v == 7);

    /* non-uniform widths: "0" 26 wide, "1" 13 wide, "2" 26 wide */
    float w[3] = { 26, 13, 26 }, left[3], tw;
    tw = nl_layout(w, 3, 0, 1, left);
    CHECK(near(left[0], 0) && near(left[1], 26) && near(left[2], 39) && near(tw, 65));
    tw = nl_layout(w, 3, 1, 1, left);                                                  /* overlap 1: each step pulls 1px back */
    CHECK(near(left[0], 0) && near(left[1], 25) && near(left[2], 37) && near(tw, 63));
    tw = nl_layout(w, 3, -3, 1, left);                                                 /* negative overlap: gaps of 3px */
    CHECK(near(left[1], 29) && near(left[2], 45) && near(tw, 71));
    tw = nl_layout(w, 3, 1, 0.5f, left);                                               /* everything scales together */
    CHECK(near(left[1], 12.5f) && near(left[2], 18.5f) && near(tw, 31.5f));
    tw = nl_layout(w, 1, 5, 2, left);                                                  /* one digit: overlap is irrelevant */
    CHECK(near(left[0], 0) && near(tw, 52));
    CHECK(nl_layout(w, 0, 1, 1, NULL) == 0);
    tw = nl_layout(w, 2, 26, 1, left);                                                 /* overlap == width: digits stacked */
    CHECK(near(left[1], 0) && near(tw, 13));
}

static void test_digit_sources(void) {
    puts("circle numbers: prefix, fallback, missing digits");
    char d[600], p[800];
    snprintf(d, sizeof d, "%s/dg", tmproot);
    sh("mkdir -p '%s/Fonts/Hit' '%s/sub'", d, d);
    for (int i = 0; i < 10; i++) {
        if (i != 4 && i != 9) { snprintf(p, sizeof p, "%s/Fonts/Hit/Score-%d.PNG", d, i); mkpng(p, 26, 96); }   /* custom: no 4, no 9 */
        if (i == 4)           { snprintf(p, sizeof p, "%s/default-4.png", d); mkpng(p, 20, 20); }                /* default-4 exists */
        if (i == 0)           { snprintf(p, sizeof p, "%s/default-0@2x.png", d); mkpng(p, 40, 40); }
    }
    for (int i = 0; i < 10; i++) {
        int src = nl_digit_source(d, "fonts\\hit\\score", i);
        CHECK(src == (i == 4 ? NL_FROM_DEFAULT : i == 9 ? NL_MISSING : NL_FROM_PREFIX));                         /* case/separators resolved */
    }
    CHECK(nl_digit_source(d, "default", 0) == NL_FROM_PREFIX);                         /* default prefix: plain default-N */
    CHECK(nl_digit_source(d, NULL, 4) == NL_FROM_PREFIX && nl_digit_source(d, "", 5) == NL_MISSING);
    CHECK(nl_digit_source(d, "nope/none", 4) == NL_FROM_DEFAULT);                      /* prefix points nowhere -> default-N */
    CHECK(nl_digit_source(d, "nope/none", 3) == NL_MISSING);
    CHECK(nl_digit_source(d, "../dg/default", 4) == NL_FROM_DEFAULT);                  /* cannot escape the folder: falls back */
    CHECK(nl_digit_source(NULL, "default", 0) == NL_MISSING);
    /* @2x preferred: the lookup finds the 2x file first */
    int sc = 0; char o[1700];
    CHECK(skinfs_image_candidate(d, "default-0", 0, &sc, o, sizeof o) && sc == 2);
}

static void test_real_digits(const char *dir) {
    puts("circle numbers: real skin");
    char f[1700]; int sc, w, h; float ws[2], left[2];
    for (int i = 0; i < 10; i++) {
        char nm[64];
        CHECK(nl_digit_name("fonts/hit/score", i, nm, sizeof nm));
        CHECK(nl_digit_source(dir, "fonts/hit/score", i) == NL_FROM_PREFIX);
        CHECK(logical_size(dir, nm, &w, &h, &sc, f, sizeof f) && sc == 1 && w == 26 && h == 96);                  /* 1x only in landmine */
    }
    CHECK(!skinfs_resolve(dir, "default-0", ".png", f, sizeof f));                      /* the skin has no default-N at all */
    CHECK(nl_digit_source(dir, "default", 0) == NL_MISSING);                           /* old behaviour would have found nothing */
    ws[0] = ws[1] = 26;
    float tw = nl_layout(ws, 2, 1, 1, left);                                           /* landmine HitCircleOverlap: 1 -> "12" */
    CHECK(near(left[1], 25) && near(tw, 51));
}

/* ---- import ---- */
static void check_skin_dir(const char *dir) {
    char o[1700];
    CHECK(skinfs_resolve(dir, "skin", ".ini", o, sizeof o));
    CHECK(skinfs_resolve(dir, "hitcircle", ".png", o, sizeof o));
}

static void test_import(void) {
    puts("import: .osk / folders");
    char msg[300], dest[1100];
    char fx[600]; snprintf(fx, sizeof fx, "%s/fx", tmproot);
    if (system("command -v zip >/dev/null 2>&1")) { puts("  (zip not installed: archive cases skipped)"); return; }
    sh("mkdir -p '%s/flat' && cd '%s/flat' && printf '[General]\\nName: T\\n' > skin.ini && : > Hitcircle.PNG", fx, fx);
    sh("cd '%s' && rm -f flat.osk && (cd flat && zip -qr ../flat.osk .) && mkdir -p wrap/MySkin && cp flat/* wrap/MySkin/ && mkdir -p wrap/__MACOSX && : > wrap/__MACOSX/junk && (cd wrap && zip -qr ../wrap.osk .) && "
       "mkdir -p none && : > none/readme.txt && (cd none && zip -qr ../none.osk .) && printf 'not a zip' > bad.osk", fx);

    char p[800];
    snprintf(p, sizeof p, "%s/flat.osk", fx);
    int rc = import_path_ex(p, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Skins/flat")); check_skin_dir(dest);
    CHECK(exists(p));                                                        /* source archive untouched */
    rc = import_path_ex(p, msg, sizeof msg, dest, sizeof dest);              /* same name again: keep the old one */
    CHECK(rc == 0 && strstr(dest, "/Skins/flat (2)") && strstr(msg, "already exists")); check_skin_dir(dest);
    snprintf(p, sizeof p, "%s/wrap.osk", fx);
    rc = import_path_ex(p, msg, sizeof msg, dest, sizeof dest);              /* single wrapper folder is flattened */
    CHECK(rc == 0 && strstr(dest, "/Skins/wrap")); check_skin_dir(dest);
    { char x[1200]; snprintf(x, sizeof x, "%s/MySkin", dest); CHECK(!exists(x)); }
    snprintf(p, sizeof p, "%s/none.osk", fx);
    CHECK(import_path_ex(p, msg, sizeof msg, dest, sizeof dest) != 0 && !dest[0]); printf("  -> %s\n", msg);
    snprintf(p, sizeof p, "%s/bad.osk", fx);
    CHECK(import_path_ex(p, msg, sizeof msg, dest, sizeof dest) != 0); printf("  -> %s\n", msg);
    CHECK(import_path("/nonexistent.osk", msg, sizeof msg) != 0);
    snprintf(p, sizeof p, "%s/readme.txt", fx); sh(": > '%s'", p);
    CHECK(import_path(p, msg, sizeof msg) != 0);                             /* unsupported extension */
    /* nothing half-installed or hidden temp folders left behind */
    char sd[600]; skins_dir(sd, sizeof sd);
    DIR *d = opendir(sd); int hidden = 0, n = 0; struct dirent *e;
    while (d && (e = readdir(d))) { if (e->d_name[0] == '.' && strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) hidden++; else if (e->d_name[0] != '.') n++; }
    if (d) closedir(d);
    CHECK(hidden == 0); CHECK(n == 3);                                       /* flat, flat (2), wrap */

    /* folder import (already-extracted skins keep working) */
    snprintf(p, sizeof p, "%s/flat", fx);
    rc = import_path_ex(p, msg, sizeof msg, dest, sizeof dest);
    CHECK(rc == 0); check_skin_dir(dest); CHECK(exists(p));
    char empty[600]; snprintf(empty, sizeof empty, "%s/empty", tmproot);
    CHECK(import_path(empty, msg, sizeof msg) != 0);                         /* not a skin, not a beatmap */

    /* aimless map.osu <skin> argument */
    char out[1100], m2[300];
    CHECK(resolve_skin_arg("", "", out, sizeof out, m2, sizeof m2) == 0 && !out[0]);
    snprintf(p, sizeof p, "%s/flat", fx);
    CHECK(resolve_skin_arg(p, "", out, sizeof out, m2, sizeof m2) == 0 && !strcmp(out, p));              /* folder as is */
    CHECK(resolve_skin_arg("wrap", "", out, sizeof out, m2, sizeof m2) == 0 && strstr(out, "/Skins/wrap")); /* bare name */
    CHECK(resolve_skin_arg("nope", "", out, sizeof out, m2, sizeof m2) == -1 && !out[0] && m2[0]);
    snprintf(p, sizeof p, "%s/wrap.osk", fx);
    CHECK(resolve_skin_arg(p, "", out, sizeof out, m2, sizeof m2) == 0 && strstr(out, "/Skins/wrap (2)")); /* .osk imported on the fly */
}

/* ---- real skin ---- */
static Got *real_cb_got;
static void real_cb(const char *s, const char *k, const char *v, void *ud) { (void)ud;
    Got *g = real_cb_got; if (g->n < 32 && (!strncasecmp(k, "Combo", 5) || !strcasecmp(k, "HitCircleOverlap") || !strcasecmp(k, "SliderBorder") || !strcasecmp(k, "SliderTrackOverride") ||
        !strcasecmp(k, "AllowSliderBallTint") || !strcasecmp(k, "HitCirclePrefix") || !strcasecmp(k, "ScorePrefix") || !strcasecmp(k, "Name") || !strncasecmp(k, "HitCircleOverlayAbove", 21))) collect(s, k, v, g); }

static void test_real_osk(const char *osk) {
    printf("real skin: %s\n", osk);
    char msg[300], dest[1100], cmd[1400];
    snprintf(cmd, sizeof cmd, "sha256sum '%s' > '%s/before.sha'", osk, tmproot); sh("%s", cmd);
    int rc = import_path_ex(osk, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0); if (rc) return;
    check_skin_dir(dest);
    snprintf(cmd, sizeof cmd, "sha256sum '%s' > '%s/after.sha'", osk, tmproot); sh("%s", cmd);
    snprintf(cmd, sizeof cmd, "cmp -s '%s/before.sha' '%s/after.sha'", tmproot, tmproot); CHECK(system(cmd) == 0);
    snprintf(cmd, sizeof cmd, "unzip -tqq '%s' >/dev/null 2>&1", osk); CHECK(system(cmd) == 0);          /* original still a valid archive */

    char o[1700];
    CHECK(skinfs_resolve(dest, "approachcircle", "@2x.png", o, sizeof o));
    CHECK(skinfs_resolve(dest, "hit0-0", ".png", o, sizeof o));              /* animated judgement name (stage 1) */
    CHECK(!skinfs_resolve(dest, "hit0", ".png", o, sizeof o));
    CHECK(skinfs_resolve(dest, "sliderb0", ".png", o, sizeof o));
    CHECK(skinfs_resolve(dest, "fonts/hit/score-0", ".png", o, sizeof o));
    CHECK(skinfs_resolve(dest, "fonts\\score\\score-9", "@2x.png", o, sizeof o));
    CHECK(skinfs_resolve(dest, "normal-hitnormal", ".ogg", o, sizeof o));
    CHECK(skinfs_resolve(dest, "combobreak", ".ogg", o, sizeof o));

    if (!skinfs_resolve(dest, "skin", ".ini", o, sizeof o)) { CHECK(0); return; }
    Got g = {0}; real_cb_got = &g;
    CHECK(skinini_read(o, real_cb, NULL) == 0);
    int rgb[3];
    const char *v;
    CHECK((v = find(&g, "Colours", "Combo1")) && skinini_color(v, rgb) && rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);
    CHECK((v = find(&g, "Colours", "Combo7")) && skinini_color(v, rgb) && rgb[0] == 255 && rgb[1] == 161 && rgb[2] == 252);
    CHECK(find(&g, "Colours", "Combo8") == NULL);
    CHECK((v = find(&g, "Colours", "SliderTrackOverride")) && skinini_color(v, rgb) && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    CHECK((v = find(&g, "Colours", "SliderBorder")) && skinini_color(v, rgb) && rgb[0] == 255);
    CHECK((v = find(&g, "General", "AllowSliderBallTint")) && !strcmp(v, "1"));
    CHECK((v = find(&g, "Fonts", "HitCirclePrefix")) && !strcmp(v, "fonts/hit/score"));
    { int ov = -99; CHECK((v = find(&g, "Fonts", "HitCircleOverlap")) && nl_parse_overlap(v, &ov) && ov == 1); }
    CHECK((v = find(&g, "General", "Name")) && !strcmp(v, "landmine (std ver)"));
    CHECK((v = find(&g, "General", "HitCircleOverlayAboveNumer")) && !strcmp(v, "1"));   /* misspelt key, shipped by the skin */
    test_real_circles(dest);
    test_real_digits(dest);
}

int main(int argc, char **argv) {
    const char *tmp = getenv("TMPDIR");
    snprintf(tmproot, sizeof tmproot, "%s/aimless-test-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(tmproot)) { perror("mkdtemp"); return 2; }
    setenv("XDG_DATA_HOME", tmproot, 1);                  /* data_dir() reads this once */
    ensure_dirs();
    test_ini_synthetic();
    test_skinfs();
    test_circle_layout();
    test_numlayout();
    test_digit_sources();
    test_import();
    if (argc > 1) test_real_osk(argv[1]);
    sh("rm -rf '%s'", tmproot);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
