#include "gfx.h"
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Color C_BG = {0, 0, 0, 255}, C_CELL = {28, 28, 30, 255}, C_CELL2 = {44, 44, 46, 255}, C_SEP = {56, 56, 58, 255},
          C_LABEL = {255, 255, 255, 255}, C_LABEL2 = {152, 152, 159, 255}, C_ACCENT = {10, 132, 255, 255},
          C_GREEN = {48, 209, 88, 255}, C_RED = {255, 69, 58, 255}, C_WHITE = {255, 255, 255, 255};

static SDL_Renderer *R;
static SDL_Texture *disc, *inv;      /* 64x64 AA disc and its inverse (for corner masks) */
static TTF_Font *fonts[2][80];
static char font_path[2][512];

#define CACHE_N 600
static struct { char *key; SDL_Texture *t; int w, h; } cache[CACHE_N];
static int cache_n;

SDL_Renderer *gfx_ren(void) { return R; }
SDL_Color gfx_alpha(SDL_Color c, int a) { c.a = (Uint8)a; return c; }

static SDL_Texture *make_disc(int inverse) {
    const int N = 64;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, N, N, 32, SDL_PIXELFORMAT_RGBA32);
    Uint32 *px = s->pixels;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float d = hypotf(x + .5f - N / 2.0f, y + .5f - N / 2.0f);
        float a = fminf(1, fmaxf(0, N / 2.0f - d));
        if (inverse) a = 1 - a;
        px[y * N + x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(a * 255));
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    SDL_FreeSurface(s);
    return t;
}

static void find_font(int bold, char *out, size_t n) {
    const char *q = bold ? "fc-match -f '%{file}' 'SF Pro Display:bold,Inter:bold,Helvetica Neue:bold,sans:bold' 2>/dev/null"
                         : "fc-match -f '%{file}' 'SF Pro Text,Inter,Helvetica Neue,sans' 2>/dev/null";
    out[0] = 0;
    FILE *p = popen(q, "r");
    if (p) { if (!fgets(out, (int)n, p)) out[0] = 0; pclose(p); }
    if (out[0]) { FILE *t = fopen(out, "r"); if (t) fclose(t); else out[0] = 0; }
    static const char *fb[] = { "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    static const char *fbb[] = { "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
                                 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL };
    const char **l = bold ? fbb : fb;
    for (int i = 0; !out[0] && l[i]; i++) { FILE *t = fopen(l[i], "r"); if (t) { fclose(t); snprintf(out, n, "%s", l[i]); } }
}

void gfx_init(SDL_Renderer *r) {
    R = r;
    disc = make_disc(0); inv = make_disc(1);
    find_font(0, font_path[0], sizeof font_path[0]);
    find_font(1, font_path[1], sizeof font_path[1]);
    if (!font_path[1][0]) strcpy(font_path[1], font_path[0]);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
}

static void cache_clear(void) {
    for (int i = 0; i < cache_n; i++) { free(cache[i].key); SDL_DestroyTexture(cache[i].t); }
    cache_n = 0;
}

void gfx_quit(void) {
    cache_clear();
    for (int b = 0; b < 2; b++) for (int i = 0; i < 80; i++) if (fonts[b][i]) TTF_CloseFont(fonts[b][i]);
    SDL_DestroyTexture(disc); SDL_DestroyTexture(inv);
}

static TTF_Font *font(int size, int bold) {
    if (size < 6) size = 6;
    if (size > 79) size = 79;
    bold = bold ? 1 : 0;
    if (!fonts[bold][size] && font_path[bold][0]) fonts[bold][size] = TTF_OpenFont(font_path[bold], size);
    return fonts[bold][size];
}

float gfx_text_w(const char *s, int size, int bold) {
    TTF_Font *f = font(size, bold); int w = 0, h = 0;
    if (f && *s) TTF_SizeUTF8(f, s, &w, &h);
    return (float)w;
}

float gfx_text(const char *s, float x, float y, int size, int bold, SDL_Color c, float maxw, int align) {
    TTF_Font *f = font(size, bold);
    if (!f || !s || !*s) return 0;
    char buf[400]; snprintf(buf, sizeof buf, "%s", s);
    if (maxw > 0) {
        int w = 0, h = 0; TTF_SizeUTF8(f, buf, &w, &h);
        while (w > maxw && strlen(buf) > 1) {
            size_t n = strlen(buf); if (n > 3 && !strcmp(buf + n - 3, "\xe2\x80\xa6")) n -= 3;
            n--; while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;
            buf[n] = 0; strcat(buf, "\xe2\x80\xa6"); TTF_SizeUTF8(f, buf, &w, &h);
        }
    }
    char key[460]; snprintf(key, sizeof key, "%d|%d|%02x%02x%02x|%s", size, bold, c.r, c.g, c.b, buf);
    int idx = -1;
    for (int i = 0; i < cache_n; i++) if (!strcmp(cache[i].key, key)) { idx = i; break; }
    if (idx < 0) {
        if (cache_n == CACHE_N) cache_clear();
        SDL_Surface *surf = TTF_RenderUTF8_Blended(f, buf, (SDL_Color){c.r, c.g, c.b, 255});
        if (!surf) return 0;
        idx = cache_n++;
        cache[idx].key = strdup(key); cache[idx].t = SDL_CreateTextureFromSurface(R, surf);
        cache[idx].w = surf->w; cache[idx].h = surf->h;
        SDL_SetTextureBlendMode(cache[idx].t, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(surf);
    }
    float dx = align == A_CENTER ? x - cache[idx].w / 2.0f : align == A_RIGHT ? x - cache[idx].w : x;
    SDL_SetTextureAlphaMod(cache[idx].t, c.a);
    SDL_FRect d = { roundf(dx), roundf(y), (float)cache[idx].w, (float)cache[idx].h };
    SDL_RenderCopyF(R, cache[idx].t, NULL, &d);
    return (float)cache[idx].w;
}

static void fill(float x, float y, float w, float h, SDL_Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_FRect r = { x, y, w, h };
    SDL_RenderFillRectF(R, &r);
}

static void corners(SDL_Texture *t, float x, float y, float w, float h, float r, SDL_Color c) {
    SDL_SetTextureColorMod(t, c.r, c.g, c.b); SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect q[4] = { {0,0,32,32}, {32,0,32,32}, {0,32,32,32}, {32,32,32,32} };
    SDL_FRect d[4] = { {x,y,r,r}, {x+w-r,y,r,r}, {x,y+h-r,r,r}, {x+w-r,y+h-r,r,r} };
    for (int i = 0; i < 4; i++) SDL_RenderCopyF(R, t, &q[i], &d[i]);
}

void gfx_rrect(float x, float y, float w, float h, float r, SDL_Color c) {
    x = roundf(x); y = roundf(y); w = roundf(w); h = roundf(h);
    if (r > w / 2) r = floorf(w / 2);
    if (r > h / 2) r = floorf(h / 2);
    if (r < 1) { fill(x, y, w, h, c); return; }
    fill(x + r, y, w - 2 * r, h, c);
    fill(x, y + r, r, h - 2 * r, c);
    fill(x + w - r, y + r, r, h - 2 * r, c);
    corners(disc, x, y, w, h, r, c);
}

void gfx_round_mask(float x, float y, float w, float h, float r, SDL_Color bg) {
    x = roundf(x); y = roundf(y); w = roundf(w); h = roundf(h);
    corners(inv, x, y, w, h, r, bg);
}

void gfx_circle(float cx, float cy, float r, SDL_Color c) {
    SDL_SetTextureColorMod(disc, c.r, c.g, c.b); SDL_SetTextureAlphaMod(disc, c.a);
    SDL_FRect d = { cx - r, cy - r, 2 * r, 2 * r };
    SDL_RenderCopyF(R, disc, NULL, &d);
}

void gfx_line_h(float x, float y, float w, SDL_Color c) { fill(x, y, w, 1, c); }

SDL_Texture *gfx_load_image(const char *path, int *w, int *h) {
    SDL_Surface *s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
    if (w) *w = s->w;
    if (h) *h = s->h;
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    SDL_FreeSurface(s);
    return t;
}

void gfx_cover(SDL_Texture *t, int tw, int th, float x, float y, float w, float h) {
    if (!t || tw <= 0 || th <= 0) return;
    float sc = fmaxf(w / tw, h / th), vw = w / sc, vh = h / sc;
    SDL_Rect src = { (int)((tw - vw) / 2), (int)((th - vh) / 2), (int)vw, (int)vh };
    SDL_FRect dst = { x, y, w, h };
    SDL_RenderCopyF(R, t, &src, &dst);
}
