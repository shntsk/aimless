#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int fake_textures_alive;   /* leak check */
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 fl, int w, int h, int d, Uint32 f) {
    (void)fl; (void)d; (void)f;
    SDL_Surface *s = calloc(1, sizeof *s); s->w = w; s->h = h; s->pixels = calloc((size_t)w * h, 4); s->format = calloc(1, sizeof *s->format);
    return s;
}
Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a) { (void)f; return (Uint32)r | (Uint32)g << 8 | (Uint32)b << 16 | (Uint32)a << 24; }
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s) { (void)r; SDL_Texture *t = calloc(1, sizeof *t); t->w = s->w; t->h = s->h; fake_textures_alive++; return t; }
void SDL_FreeSurface(SDL_Surface *s) { if (!s) return; free(s->pixels); free(s->format); free(s); }
void SDL_DestroyTexture(SDL_Texture *t) { if (t) { free(t); fake_textures_alive--; } }
int SDL_SetTextureBlendMode(SDL_Texture *t, int m) { (void)t; (void)m; return 0; }
int SDL_SetTextureScaleMode(SDL_Texture *t, int m) { (void)t; (void)m; return 0; }
const char *SDL_GetError(void) { return "fake"; }
const char *IMG_GetError(void) { return "not a PNG (fake loader)"; }
/* a file counts as decodable when it starts with a PNG header; size comes from IHDR */
SDL_Surface *IMG_Load(const char *path) {
    unsigned char b[24]; FILE *f = fopen(path, "rb"); if (!f) return NULL;
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    if (n < sizeof b || memcmp(b, "\x89PNG", 4)) return NULL;
    SDL_Surface *s = calloc(1, sizeof *s);
    s->w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    s->h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return s;
}
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *t, SDL_Color c) { (void)f; (void)t; (void)c; return NULL; }
