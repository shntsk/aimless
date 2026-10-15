#ifndef SKIN_H
#define SKIN_H
#include <SDL.h>

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

/* dir may be NULL -> built-in procedural fallback */
void skin_load(Skin *s, SDL_Renderer *r, const char *dir);
void skin_free(Skin *s);

#endif
