#ifndef SKIN_H
#define SKIN_H
#include <SDL.h>
#include <SDL_ttf.h>

typedef struct { SDL_Texture *t; int w, h; } Tex; /* w/h already normalised to @1x */

typedef struct {
    Tex hitcircle, overlay, approach, cursor;
    Tex digit[10];      /* circle numbers: <HitCirclePrefix>-N, else default-N (see numlayout.h) */
    int digit_img;      /* 1 when all ten circle digits come from image files (logical sizes known), 0 when some are drawn from a TTF */
    Tex score[10];      /* HUD digits (score-N)        */
    Tex score_x;        /* "x" after combo             */
    Tex hit[4];         /* hit300, hit100, hit50, hit0 */
    Tex disc;           /* plain white disc (slider body stamps), always generated */
    Tex sliderb[10]; int n_sliderb;  /* ball frames (sliderb0..9 or sliderb) */
    Tex follow, arrow, scorepoint, endcircle, endoverlay;
    Uint8 slider_border[3], slider_track[3]; int has_track;  /* skin.ini SliderBorder / SliderTrackOverride */
    int overlay_above_number;  /* skin.ini HitCircleOverlayAboveNumber (default 1): overlay is drawn over the number */
    int ball_tint;  /* skin.ini AllowSliderBallTint: tint the slider ball with the combo colour */
    Uint8 combo[8][3];
    int n_combo;
    /* skin.ini [Fonts]: prefixes are relative to the skin folder and may contain sub-folders
     * (e.g. "fonts/hit/score" -> fonts/hit/score-0.png). hitcircle_prefix / hitcircle_overlap drive the circle
     * numbers (skin.c loads digit[], game.c spaces them). The score_* and combo_* values are parsed and kept, but the
     * HUD digits (score[]) still use the fixed score-N names and no overlap: nothing reads them yet. */
    char hitcircle_prefix[128], score_prefix[128], combo_prefix[128];
    int hitcircle_overlap, score_overlap, combo_overlap;
    char name[96];      /* skin.ini [General] Name, for logs */
    int has_ini;        /* a skin.ini was found and read */
} Skin;

/* when the skin has no digit images, render them from a TTF font (white, tinted by colour mod) */
void skin_fallback_digits(Skin *s, SDL_Renderer *r, TTF_Font *f);

/* dir may be NULL -> built-in procedural fallback. A dir that is not an existing folder is reported on
 * stderr and treated as NULL. Missing, unreadable or undecodable files fall back per element. */
void skin_load(Skin *s, SDL_Renderer *r, const char *dir);
void skin_free(Skin *s);

#endif
