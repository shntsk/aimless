#include "font.h"
#include "plat.h"
#include <stdio.h>
#include <string.h>

TTF_Font *font_open(int size) {
    static char path[512]; static int probed;
    if (!probed) { probed = 1; plat_find_font(0, "sans", path, sizeof path); }
    TTF_Font *f = path[0] ? TTF_OpenFont(path, size) : NULL;
    if (!f) {                                        /* the chosen file did not load: try the known fonts */
        static char alt[512]; static int probed2;
        if (!probed2) { probed2 = 1; plat_find_font(0, NULL, alt, sizeof alt); }
        if (alt[0] && strcmp(alt, path)) f = TTF_OpenFont(alt, size);
    }
    return f;
}

void font_draw(SDL_Renderer *ren, TTF_Font *f, const char *s, int x, int y, SDL_Color c, int max_w) {
    if (!f || !*s) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect src = { 0, 0, surf->w > max_w ? max_w : surf->w, surf->h };
    SDL_Rect dst = { x, y, src.w, src.h };
    SDL_RenderCopy(ren, t, &src, &dst);
    SDL_DestroyTexture(t); SDL_FreeSurface(surf);
}

void text_set(SDL_Renderer *ren, TextCache *c, TTF_Font *f, const char *s, SDL_Color col) {
    Uint32 k = ((Uint32)col.r << 24) | ((Uint32)col.g << 16) | ((Uint32)col.b << 8) | col.a;
    if (c->t && c->col == k && !strcmp(c->s, s)) return;
    if (c->t) { SDL_DestroyTexture(c->t); c->t = NULL; }
    snprintf(c->s, sizeof c->s, "%s", s); c->col = k;
    if (!f || !*s) { c->w = c->h = 0; return; }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, col);
    if (!surf) return;
    c->t = SDL_CreateTextureFromSurface(ren, surf);
    c->w = surf->w; c->h = surf->h;
    SDL_SetTextureBlendMode(c->t, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
}

void text_blit(SDL_Renderer *ren, TextCache *c, int x, int y, int alpha) {
    if (!c->t) return;
    SDL_SetTextureAlphaMod(c->t, alpha);
    SDL_Rect d = { x, y, c->w, c->h };
    SDL_RenderCopy(ren, c->t, NULL, &d);
}

void text_free(TextCache *c) { if (c->t) SDL_DestroyTexture(c->t); memset(c, 0, sizeof *c); }
