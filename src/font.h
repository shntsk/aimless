#ifndef FONT_H
#define FONT_H
#include <SDL.h>
#include <SDL_ttf.h>

TTF_Font *font_open(int size);
/* one-shot draw (creates + destroys a texture): fine for menus, not for per-frame HUD */
void font_draw(SDL_Renderer *ren, TTF_Font *f, const char *s, int x, int y, SDL_Color c, int max_w);

/* cached text: re-rendered only when the string or colour changes */
typedef struct { SDL_Texture *t; int w, h; char s[96]; Uint32 col; } TextCache;
void text_set(SDL_Renderer *ren, TextCache *c, TTF_Font *f, const char *s, SDL_Color col);
void text_blit(SDL_Renderer *ren, TextCache *c, int x, int y, int alpha);
void text_free(TextCache *c);

#endif
