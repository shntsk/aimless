#ifndef GAME_H
#define GAME_H
#include <SDL.h>

typedef struct {
    float assist;           /* hit radius in circle radii; 0 = anywhere; 1 = no assist */
    int vsync;
    char songs_extra[512];  /* optional extra Songs root (e.g. osu!stable) */
    char skins_extra[512];  /* optional extra Skins root */
    char status[256];       /* last result / import message shown in the menu */
} Settings;

/* Plays one beatmap. Returns 0 to go back to the menu, 1 if the window was closed. */
int game_play(SDL_Window *win, SDL_Renderer *ren, const char *map, const char *skin_dir, Settings *st);

#endif
