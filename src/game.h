#ifndef GAME_H
#define GAME_H
#include "settings.h"
#include <SDL.h>

/* Plays one beatmap. Returns 0 to go back to the menu, 1 if the window was closed. */
int game_play(SDL_Window *win, SDL_Renderer *ren, const char *map, const char *skin_dir, Settings *st);

#endif
