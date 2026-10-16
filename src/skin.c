/* Loads osu! stable skins (name@2x.png preferred, then name.png) with procedural fallbacks. */
#include "skin.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Tex load(SDL_Renderer *r, const char *dir, const char *name) {
    Tex t = {0};
    if (!dir) return t;
    char p[1024];
    int scale = 2;
    snprintf(p, sizeof p, "%s/%s@2x.png", dir, name);
    SDL_Surface *s = IMG_Load(p);
    if (!s) { scale = 1; snprintf(p, sizeof p, "%s/%s.png", dir, name); s = IMG_Load(p); }
    if (!s) return t;
    t.t = SDL_CreateTextureFromSurface(r, s);
    t.w = s->w / scale; t.h = s->h / scale;
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_ScaleModeLinear);
    SDL_FreeSurface(s);
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

static void parse_ini(Skin *s, const char *dir) {
    static const Uint8 def[5][3] = {{255,192,0},{0,202,0},{18,124,255},{242,24,57},{200,100,255}};
    char p[1024], line[512];
    FILE *f = NULL;
    if (dir) { snprintf(p, sizeof p, "%s/skin.ini", dir); f = fopen(p, "r"); }
    if (f) {
        while (fgets(line, sizeof line, f)) {
            int i, r, g, b;
            if (sscanf(line, "Combo%d : %d , %d , %d", &i, &r, &g, &b) == 4 ||
                sscanf(line, "Combo%d:%d,%d,%d", &i, &r, &g, &b) == 4) {
                if (i >= 1 && i <= 8) {
                    s->combo[i-1][0] = r; s->combo[i-1][1] = g; s->combo[i-1][2] = b;
                    if (i > s->n_combo) s->n_combo = i;
                }
            }
        }
        fclose(f);
    }
    if (!s->n_combo) { memcpy(s->combo, def, sizeof def); s->n_combo = 5; }
}

void skin_load(Skin *s, SDL_Renderer *r, const char *dir) {
    memset(s, 0, sizeof *s);
    s->hitcircle = load(r, dir, "hitcircle");
    s->overlay   = load(r, dir, "hitcircleoverlay");
    s->approach  = load(r, dir, "approachcircle");
    s->cursor    = load(r, dir, "cursor");
    if (!s->hitcircle.t) s->hitcircle = gen_circle(r, 128, 0);
    if (!s->overlay.t)   s->overlay   = gen_circle(r, 128, 5);
    if (!s->approach.t)  s->approach  = gen_circle(r, 128, 3);
    if (!s->cursor.t)    s->cursor    = gen_circle(r, 32, 0);
    char n[64];
    for (int i = 0; i < 10; i++) {
        snprintf(n, sizeof n, "default-%d", i); s->digit[i] = load(r, dir, n);
        snprintf(n, sizeof n, "score-%d", i);   s->score[i] = load(r, dir, n);
    }
    s->score_x = load(r, dir, "score-x");
    s->hit[0] = load(r, dir, "hit300"); s->hit[1] = load(r, dir, "hit100");
    s->hit[2] = load(r, dir, "hit50");  s->hit[3] = load(r, dir, "hit0");
    parse_ini(s, dir);
}

void skin_free(Skin *s) {
    SDL_Texture *all[] = { s->hitcircle.t, s->overlay.t, s->approach.t, s->cursor.t, s->score_x.t };
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
