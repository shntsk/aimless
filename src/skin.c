/* Loads osu! stable skins (name@2x.png preferred, then name.png) with procedural fallbacks. */
#include "skin.h"
#include "skinfs.h"
#include "skinini.h"
#include "numlayout.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Image lookup, osu! stable order: name@2x.png, name@2x.jpg, name.png, name.jpg. Names are matched
 * ignoring case and may contain sub-folders. A file that exists but cannot be decoded is reported once
 * and the next candidate is tried; a file that decodes (even fully transparent) is always used as is. */
static Tex load(SDL_Renderer *r, const char *dir, const char *name) {
    Tex t = {0};
    if (!dir) return t;
    for (int i = 0; i < SKINFS_N_IMG; i++) {
        char p[1700]; int scale = 1;
        if (!skinfs_image_candidate(dir, name, i, &scale, p, sizeof p)) continue;
        SDL_Surface *s = IMG_Load(p);
        if (!s) { fprintf(stderr, "skin: cannot decode %s: %s\n", p, IMG_GetError()); continue; }
        SDL_Texture *tx = SDL_CreateTextureFromSurface(r, s);
        int w = s->w, h = s->h;
        SDL_FreeSurface(s);
        if (!tx) { fprintf(stderr, "skin: cannot create texture for %s: %s\n", p, SDL_GetError()); continue; }
        SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tx, SDL_ScaleModeLinear);
        /* w/h are logical (@1x) sizes: an @2x file is half its pixel size, so mixed 1x/2x elements line up */
        t.t = tx; t.w = (w + scale - 1) / scale; t.h = (h + scale - 1) / scale;
        if (getenv("AIMLESS_SKINLOG")) fprintf(stderr, "skin: %s (%dx%d px, scale %d -> %dx%d)\n", p, w, h, scale, t.w, t.h);
        return t;
    }
    return t;
}

/* ring: 0 = filled disc, otherwise ring thickness in px */
static Tex gen_circle(SDL_Renderer *r, int size, float ring) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
    Uint32 *px = s->pixels;
    float c = size / 2.0f, rad = c - 1;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        float d = hypotf(x + .5f - c, y + .5f - c), a;
        if (ring > 0) a = fminf(1, fmaxf(0, rad - d + 1)) * fminf(1, fmaxf(0, d - (rad - ring) + 1));
        else a = fminf(1, fmaxf(0, rad - d + 1));
        px[y * size + x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(a * 255));
    }
    Tex t = { SDL_CreateTextureFromSurface(r, s), size, size };
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_ScaleModeLinear);
    SDL_FreeSurface(s);
    return t;
}

/* small white right-pointing chevron for the reverse arrow fallback */
static Tex gen_arrow(SDL_Renderer *r) {
    int n = 64;
    SDL_Surface *su = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_RGBA32);
    Uint32 *px = su->pixels;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        float dy = fabsf(y + .5f - n / 2.0f), ax = x + .5f;
        float edge = (ax - 14) * 0.9f;                       /* tip at x~50, arms back to x~14 */
        int in_arm = ax > 14 && ax < 52 && fabsf(dy - (52 - ax) * 0.8f) < 4.5f;
        (void)edge;
        px[y * n + x] = SDL_MapRGBA(su->format, 255, 255, 255, in_arm ? 255 : 0);
    }
    Tex t = { SDL_CreateTextureFromSurface(r, su), 96, 96 };
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_ScaleModeLinear);
    SDL_FreeSurface(su);
    return t;
}

/* judgement image: hitN.png, else first frame of the animated form hitN-0.png (osu! stable convention) */
static Tex load_hit(SDL_Renderer *r, const char *dir, const char *name) {
    Tex t = load(r, dir, name);
    if (!t.t) { char n[64]; snprintf(n, sizeof n, "%s-0", name); t = load(r, dir, n); }
    return t;
}

/* white-ish cross used for the miss mark when neither skin nor map provides one */
static Tex gen_miss(SDL_Renderer *r) {
    int n = 64;
    SDL_Surface *su = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_RGBA32);
    Uint32 *px = su->pixels;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        float a = fabsf((x + .5f) - (y + .5f)), b = fabsf((x + .5f) - (n - (y + .5f)));
        int in = x > 10 && x < n - 10 && y > 10 && y < n - 10 && (a < 4.5f || b < 4.5f);
        px[y * n + x] = SDL_MapRGBA(su->format, 255, 60, 60, in ? 255 : 0);
    }
    Tex t = { SDL_CreateTextureFromSurface(r, su), n, n };
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_ScaleModeLinear);
    SDL_FreeSurface(su);
    return t;
}

typedef struct { Skin *s; Uint8 combo[8][3]; int have[8]; } IniCtx;

/* section-aware, but a key that shows up before any [Section] header is still accepted */
static int in_sec(const char *sec, const char *want) { return !*sec || !strcasecmp(sec, want); }

static void ini_kv(const char *sec, const char *k, const char *v, void *ud) {
    IniCtx *c = ud; Skin *s = c->s;
    int rgb[3], b, i;
    if (in_sec(sec, "Colours") || !strcasecmp(sec, "Colors")) {
        if (!strcasecmp(k, "SliderBorder") && skinini_color(v, rgb)) { for (i = 0; i < 3; i++) s->slider_border[i] = rgb[i]; }
        else if (!strcasecmp(k, "SliderTrackOverride") && skinini_color(v, rgb)) { for (i = 0; i < 3; i++) s->slider_track[i] = rgb[i]; s->has_track = 1; }
        else if (!strncasecmp(k, "Combo", 5) && k[5] >= '1' && k[5] <= '8' && !k[6] && skinini_color(v, rgb)) {
            i = k[5] - '1';
            for (int j = 0; j < 3; j++) c->combo[i][j] = rgb[j];
            c->have[i] = 1;
        }
    }
    if (in_sec(sec, "General")) {
        if (!strcasecmp(k, "AllowSliderBallTint") && skinini_bool(v, &b)) s->ball_tint = b;
        /* "Numer" is a typo that real skins (landmine among them) ship with; both spellings are read */
        else if ((!strcasecmp(k, "HitCircleOverlayAboveNumber") || !strcasecmp(k, "HitCircleOverlayAboveNumer")) && skinini_bool(v, &b)) s->overlay_above_number = b;
        else if (!strcasecmp(k, "Name")) snprintf(s->name, sizeof s->name, "%s", v);
    }
    if (in_sec(sec, "Fonts")) {
        if (!strcasecmp(k, "HitCirclePrefix")) nl_copy_prefix(s->hitcircle_prefix, sizeof s->hitcircle_prefix, v);
        else if (!strcasecmp(k, "ScorePrefix")) nl_copy_prefix(s->score_prefix, sizeof s->score_prefix, v);
        else if (!strcasecmp(k, "ComboPrefix")) nl_copy_prefix(s->combo_prefix, sizeof s->combo_prefix, v);
        else if (!strcasecmp(k, "HitCircleOverlap")) nl_parse_overlap(v, &s->hitcircle_overlap);
        else if (!strcasecmp(k, "ScoreOverlap")) nl_parse_overlap(v, &s->score_overlap);
        else if (!strcasecmp(k, "ComboOverlap")) nl_parse_overlap(v, &s->combo_overlap);
    }
}

static void parse_ini(Skin *s, const char *dir) {
    static const Uint8 def[5][3] = {{255,192,0},{0,202,0},{18,124,255},{242,24,57},{200,100,255}};
    s->overlay_above_number = 1;
    snprintf(s->hitcircle_prefix, sizeof s->hitcircle_prefix, "default");
    snprintf(s->score_prefix, sizeof s->score_prefix, "score");
    snprintf(s->combo_prefix, sizeof s->combo_prefix, "score");
    IniCtx c; memset(&c, 0, sizeof c); c.s = s;
    char p[1700];
    if (dir && skinfs_resolve(dir, "skin", ".ini", p, sizeof p)) {
        if (skinini_read(p, ini_kv, &c) == 0) s->has_ini = 1;
        else fprintf(stderr, "skin: cannot read %s\n", p);
    }
    /* combos can be sparse (Combo1, Combo3): keep the defined ones, in order */
    for (int i = 0; i < 8; i++) if (c.have[i]) { memcpy(s->combo[s->n_combo], c.combo[i], 3); s->n_combo++; }
    if (!s->n_combo) { memcpy(s->combo, def, sizeof def); s->n_combo = 5; }
}

/* Circle numbers: "<HitCirclePrefix>-N" first; a digit whose file is missing (or undecodable) falls back to
 * "default-N", and one that is missing there too is left empty for skin_fallback_digits (TTF). Each digit is
 * resolved on its own, so a skin may ship only some of them. Every element still goes through load(), i.e.
 * @2x preferred, case-insensitive, sub-folders allowed. */
static void load_circle_digits(Skin *s, SDL_Renderer *r, const char *dir) {
    int n_prefix = 0, n_default = 0, n_missing = 0;
    char n[256], missing[16] = "";
    for (int i = 0; i < 10; i++) {
        int src = nl_digit_source(dir, s->hitcircle_prefix, i);
        Tex t = {0};
        if (src == NL_FROM_PREFIX && nl_digit_name(s->hitcircle_prefix, i, n, sizeof n)) {
            t = load(r, dir, n);
            if (t.t) n_prefix++;
        }
        if (!t.t && nl_digit_name(NL_DEFAULT_PREFIX, i, n, sizeof n)) {
            t = load(r, dir, n);
            if (t.t) n_default++;
        }
        if (!t.t) { missing[n_missing++] = (char)('0' + i); }
        s->digit[i] = t;
    }
    missing[n_missing] = 0;
    s->digit_img = n_missing == 0;
    int custom = strcasecmp(nl_prefix_or_default(s->hitcircle_prefix), NL_DEFAULT_PREFIX) != 0;
    if (getenv("AIMLESS_SKINLOG") || (custom && (n_default || n_missing)))
        fprintf(stderr, "skin: circle digits \"%s-N\": %d from prefix, %d from default-N, %d missing [%s], overlap %d\n",
                nl_prefix_or_default(s->hitcircle_prefix), n_prefix, n_default, n_missing, missing, s->hitcircle_overlap);
}

void skin_load(Skin *s, SDL_Renderer *r, const char *dir) {
    memset(s, 0, sizeof *s);
    if (dir && !skinfs_isdir(dir)) {
        fprintf(stderr, "skin: \"%s\" is not a folder, using the built-in skin\n", dir);
        dir = NULL;
    }
    memset(s->slider_border, 255, 3);
    parse_ini(s, dir);   /* before the digits: HitCirclePrefix decides which files they come from */
    s->hitcircle = load(r, dir, "hitcircle");
    s->overlay   = load(r, dir, "hitcircleoverlay");
    s->approach  = load(r, dir, "approachcircle");
    s->cursor    = load(r, dir, "cursor");
    if (!s->hitcircle.t) s->hitcircle = gen_circle(r, 128, 0);
    if (!s->overlay.t)   s->overlay   = gen_circle(r, 128, 5);
    if (!s->approach.t)  s->approach  = gen_circle(r, 128, 3);
    if (!s->cursor.t)    s->cursor    = gen_circle(r, 32, 0);
    char n[256];
    load_circle_digits(s, r, dir);
    for (int i = 0; i < 10; i++) {
        snprintf(n, sizeof n, "score-%d", i);   s->score[i] = load(r, dir, n);
    }
    s->score_x = load(r, dir, "score-x");
    /* a hit300 etc. that exists but is fully transparent is intentional: only the miss mark gets a generated fallback */
    s->hit[0] = load_hit(r, dir, "hit300"); s->hit[1] = load_hit(r, dir, "hit100");
    s->hit[2] = load_hit(r, dir, "hit50");  s->hit[3] = load_hit(r, dir, "hit0");
    if (!s->hit[3].t) s->hit[3] = gen_miss(r);

    /* sliders */
    s->disc = gen_circle(r, 128, 0);
    for (int i = 0; i < 10; i++) {
        snprintf(n, sizeof n, "sliderb%d", i);
        Tex t = load(r, dir, n);
        if (!t.t) break;
        s->sliderb[s->n_sliderb++] = t;
    }
    if (!s->n_sliderb) { Tex t = load(r, dir, "sliderb"); if (t.t) s->sliderb[s->n_sliderb++] = t; }
    if (!s->n_sliderb) s->sliderb[s->n_sliderb++] = gen_circle(r, 128, 6);
    s->follow = load(r, dir, "sliderfollowcircle");
    if (!s->follow.t) { s->follow = gen_circle(r, 128, 3); s->follow.w = s->follow.h = 300; } /* ~2.4 circle diameters */
    s->arrow = load(r, dir, "reversearrow");
    if (!s->arrow.t) s->arrow = gen_arrow(r);
    s->scorepoint = load(r, dir, "sliderscorepoint");
    if (!s->scorepoint.t) { s->scorepoint = gen_circle(r, 32, 0); s->scorepoint.w = s->scorepoint.h = 16; }
    s->endcircle = load(r, dir, "sliderendcircle");
    s->endoverlay = load(r, dir, "sliderendcircleoverlay");
}

void skin_free(Skin *s) {
    SDL_Texture *all[] = { s->hitcircle.t, s->overlay.t, s->approach.t, s->cursor.t, s->score_x.t,
                           s->disc.t, s->follow.t, s->arrow.t, s->scorepoint.t, s->endcircle.t, s->endoverlay.t };
    for (int i = 0; i < s->n_sliderb; i++) if (s->sliderb[i].t) SDL_DestroyTexture(s->sliderb[i].t);
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (all[i]) SDL_DestroyTexture(all[i]);
    for (int i = 0; i < 10; i++) { if (s->digit[i].t) SDL_DestroyTexture(s->digit[i].t); if (s->score[i].t) SDL_DestroyTexture(s->score[i].t); }
    for (int i = 0; i < 4; i++) if (s->hit[i].t) SDL_DestroyTexture(s->hit[i].t);
}

void skin_fallback_digits(Skin *s, SDL_Renderer *r, TTF_Font *f) {
    if (!f) return;
    for (int i = 0; i < 10; i++) {
        char d[2] = { (char)('0' + i), 0 };
        for (int k = 0; k < 2; k++) {
            Tex *t = k ? &s->score[i] : &s->digit[i];
            if (t->t) continue;
            SDL_Color w = { 255, 255, 255, 255 };
            SDL_Surface *su = TTF_RenderUTF8_Blended(f, d, w);
            if (!su) continue;
            t->t = SDL_CreateTextureFromSurface(r, su);
            t->w = su->w * (k ? 1 : 1); t->h = su->h;
            SDL_SetTextureBlendMode(t->t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t->t, SDL_ScaleModeLinear);
            SDL_FreeSurface(su);
        }
    }
}
