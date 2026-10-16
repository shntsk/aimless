#ifndef SKIN_H
#define SKIN_H
#include <SDL.h>
#include <SDL_ttf.h>

typedef struct { SDL_Texture *t; int w, h; } Tex; /* w/h already normalised to @1x */

typedef struct {
    Tex hitcircle, overlay, approach, cursor;
    Tex digit[10];      /* circle numbers (default-N)  */
    Tex score[10];      /* HUD digits (score-N)        */
    Tex score_x;        /* "x" after combo             */
    Tex hit[4];         /* hit300, hit100, hit50, hit0 */
    Uint8 combo[8][3];
    int n_combo;
} Skin;

/* when the skin has no digit images, render them from a TTF font (white, tinted by colour mod) */
void skin_fallback_digits(Skin *s, SDL_Renderer *r, TTF_Font *f);

/* dir may be NULL -> built-in procedural fallback */
void skin_load(Skin *s, SDL_Renderer *r, const char *dir);
void skin_free(Skin *s);

#endif
