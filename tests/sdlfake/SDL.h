/* Minimal fake of the SDL2 calls skin.c makes, for tests/test_skin_loader.c only (no window, no decoding of
 * pixels: IMG_Load just reads the PNG header). Never used by the real build. */
#ifndef FAKE_SDL_H
#define FAKE_SDL_H
#include <stdint.h>
typedef uint8_t Uint8; typedef uint32_t Uint32;
typedef struct SDL_Renderer { int unused; } SDL_Renderer;
typedef struct SDL_Texture { int w, h; } SDL_Texture;
typedef struct SDL_PixelFormat { int unused; } SDL_PixelFormat;
typedef struct SDL_Surface { int w, h; void *pixels; SDL_PixelFormat *format; } SDL_Surface;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
#define SDL_BLENDMODE_BLEND 1
#define SDL_ScaleModeLinear 1
#define SDL_PIXELFORMAT_RGBA32 1
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 fmt);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
void SDL_FreeSurface(SDL_Surface *s);
void SDL_DestroyTexture(SDL_Texture *t);
int SDL_SetTextureBlendMode(SDL_Texture *t, int m);
int SDL_SetTextureScaleMode(SDL_Texture *t, int m);
const char *SDL_GetError(void);
#endif
