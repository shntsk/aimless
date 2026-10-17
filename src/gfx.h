#ifndef GFX_H
#define GFX_H
#include <SDL.h>

/* Small drawing toolkit for the iOS-style ("Cupertino") UI: rounded rects, AA circles, cached text. */
extern SDL_Color C_BG, C_CELL, C_CELL2, C_SEP, C_LABEL, C_LABEL2, C_ACCENT, C_GREEN, C_RED, C_WHITE;

void  gfx_init(SDL_Renderer *r);
void  gfx_quit(void);
SDL_Renderer *gfx_ren(void);

void  gfx_rrect(float x, float y, float w, float h, float r, SDL_Color c);
void  gfx_circle(float cx, float cy, float r, SDL_Color c);
void  gfx_round_mask(float x, float y, float w, float h, float r, SDL_Color bg); /* rounds an image's corners */
void  gfx_star(float cx, float cy, float r, SDL_Color c);
void  gfx_line_h(float x, float y, float w, SDL_Color c);

enum { A_LEFT, A_CENTER, A_RIGHT };
/* y is the top of the text. returns drawn width. maxw <= 0: no limit (ellipsis otherwise) */
float gfx_text(const char *s, float x, float y, int size, int bold, SDL_Color c, float maxw, int align);
float gfx_text_w(const char *s, int size, int bold);

SDL_Texture *gfx_load_image(const char *path, int *w, int *h);
/* draws tex scaled to cover the dst rect, cropping the overflow; colour-mod is applied by the caller */
void  gfx_cover(SDL_Texture *t, int tw, int th, float x, float y, float w, float h);

SDL_Color gfx_alpha(SDL_Color c, int a);

#endif
