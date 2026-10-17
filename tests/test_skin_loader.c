/* Runs the real skin_load() from src/skin.c against a FAKE SDL (tests/sdlfake): no window, no pixel decoding,
 * images are "decoded" by reading the PNG header only. It proves which files the loader picks for the circle
 * digits, their logical sizes, the skin.ini prefix/overlap wiring and the fallbacks. It does NOT prove anything
 * about rendering: how the numbers look in gameplay needs the real SDL build.
 *   make test OSK=/path/to/skin.osk   (the .osk is extracted to a temp folder) */
#define _GNU_SOURCE
#include "../src/skin.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int fake_textures_alive;
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char root[512];
static void sh(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void sh(const char *fmt, ...) {
    char cmd[2048]; va_list ap; va_start(ap, fmt); vsnprintf(cmd, sizeof cmd, fmt, ap); va_end(ap);
    if (system(cmd)) { /* results are checked through the loader */ }
}
static void mkpng(const char *path, int w, int h) {
    unsigned char b[24] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                            (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                            (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h };
    FILE *f = fopen(path, "wb"); if (!f) return; fwrite(b, 1, sizeof b, f); fclose(f);
}
static void put(const char *dir, const char *rel, int w, int h) { char p[900]; snprintf(p, sizeof p, "%s/%s", dir, rel); mkpng(p, w, h); }
static void text(const char *dir, const char *rel, const char *body) {
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "wb"); if (f) { fputs(body, f); fclose(f); }
}
static SDL_Renderer R;

static void test_default_names(void) {
    puts("loader: skin that uses the default names (no skin.ini)");
    char d[600]; snprintf(d, sizeof d, "%s/def", root); sh("mkdir -p '%s'", d);
    for (int i = 0; i < 10; i++) { char n[64]; snprintf(n, sizeof n, "default-%d.png", i); put(d, n, 40, 50); }
    put(d, "default-3@2x.png", 80, 100);                                     /* @2x wins and halves to 40x50 logical... */
    put(d, "default-5@2x.png", 60, 90);                                      /* ...or 30x45 for this one */
    Skin s; skin_load(&s, &R, d);
    int ok = 1; for (int i = 0; i < 10; i++) ok &= s.digit[i].t != NULL;
    CHECK(ok && s.digit_img == 1);
    CHECK(s.digit[0].w == 40 && s.digit[0].h == 50);
    CHECK(s.digit[3].w == 40 && s.digit[3].h == 50);
    CHECK(s.digit[5].w == 30 && s.digit[5].h == 45);                          /* digits keep their own widths */
    CHECK(s.hitcircle_overlap == 0 && !strcmp(s.hitcircle_prefix, "default"));
    skin_free(&s);
}

static void test_custom_prefix_and_fallback(void) {
    puts("loader: custom prefix, missing digits, undecodable file");
    char d[600]; snprintf(d, sizeof d, "%s/cus", root); sh("mkdir -p '%s/Fonts/Hit'", d);
    text(d, "skin.ini", "\xEF\xBB\xBF[General]\r\nName: t\r\n[Fonts]\r\n   HitCirclePrefix: Fonts\\Hit\\Num\r\n   HitCircleOverlap: -3\r\n   ScorePrefix: x/y\r\n");
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 9 || i == 7) continue;
        char n[64]; snprintf(n, sizeof n, "Fonts/Hit/NUM-%d.PNG", i); put(d, n, 20 + i, 60);
    }
    text(d, "Fonts/Hit/num-7.png", "garbage, not a PNG");                   /* exists but cannot be decoded */
    put(d, "default-4.png", 33, 44);                                        /* digit 4 only exists as default-N */
    put(d, "default-7.png", 17, 44);                                        /* so does a good copy of 7 */
    Skin s; skin_load(&s, &R, d);
    CHECK(!strcmp(s.hitcircle_prefix, "Fonts/Hit/Num"));                    /* backslashes normalised */
    CHECK(s.hitcircle_overlap == -3);                                       /* negative overlap kept */
    CHECK(s.digit[0].t && s.digit[0].w == 20 && s.digit[8].w == 28);        /* from the prefix, each its own width */
    CHECK(s.digit[4].t && s.digit[4].w == 33 && s.digit[4].h == 44);        /* default-N fallback */
    CHECK(s.digit[7].t && s.digit[7].w == 17);                              /* undecodable prefix file -> default-7 */
    CHECK(s.digit[9].t == NULL);                                            /* missing everywhere: left for the TTF fallback */
    CHECK(s.digit_img == 0);                                                /* so the number is drawn at the legacy scale */
    skin_free(&s);

    /* an empty prefix value is the default prefix */
    text(d, "skin.ini", "[Fonts]\nHitCirclePrefix:\n");
    skin_load(&s, &R, d);
    CHECK(s.digit[4].t && s.digit[4].w == 33 && s.digit[0].t == NULL);      /* default-N only: 4 and 7 exist */
    skin_free(&s);
}

static void test_no_skin(void) {
    puts("loader: no skin folder");
    Skin s; skin_load(&s, &R, NULL);
    int none = 1; for (int i = 0; i < 10; i++) none &= s.digit[i].t == NULL;
    CHECK(none && s.digit_img == 0 && s.hitcircle_overlap == 0);
    CHECK(s.hitcircle.t && s.overlay.t && s.approach.t);                    /* procedural circles still there */
    skin_free(&s);
}

static void test_real(const char *dir) {
    puts("loader: real landmine skin");
    Skin s; skin_load(&s, &R, dir);
    CHECK(!strcmp(s.name, "landmine (std ver)"));
    CHECK(!strcmp(s.hitcircle_prefix, "fonts/hit/score") && s.hitcircle_overlap == 1);
    CHECK(!strcmp(s.score_prefix, "fonts/score/score") && s.score_overlap == 1 && !strcmp(s.combo_prefix, "fonts/score/score") && s.combo_overlap == 2);
    int ok = 1; for (int i = 0; i < 10; i++) ok &= s.digit[i].t && s.digit[i].w == 26 && s.digit[i].h == 96;
    CHECK(ok && s.digit_img == 1);
    /* stage 4A and earlier are still there */
    CHECK(s.hitcircle.w == 128 && s.hitcircle.h == 128);                    /* hitcircle@2x 256px -> 128 */
    CHECK(s.overlay.w == 128 && s.approach.w == 192 && s.approach.h == 192);
    CHECK(s.overlay_above_number == 1);                                     /* the "...Numer" typo key */
    CHECK(s.has_track && s.ball_tint == 1 && s.n_combo == 7);
    CHECK(s.n_sliderb >= 1 && s.hit[3].t);
    skin_free(&s);
}

int main(int argc, char **argv) {
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/aimless-loader-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    test_default_names();
    test_custom_prefix_and_fallback();
    test_no_skin();
    if (argc > 1) {
        char dir[700]; snprintf(dir, sizeof dir, "%s/real", root);
        sh("mkdir -p '%s' && unzip -qq -o '%s' -d '%s' >/dev/null 2>&1", dir, argv[1], dir);
        test_real(dir);
    }
    CHECK(fake_textures_alive == 0);                                        /* skin_free released everything */
    sh("rm -rf '%s'", root);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
