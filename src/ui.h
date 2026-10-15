#ifndef UI_H
#define UI_H
#include "game.h"
#include <SDL.h>

/* Song/skin select screen. Returns 1 when a map was chosen (map_out/skin_out filled), 0 on quit. */
int menu_run(SDL_Window *win, SDL_Renderer *ren, Settings *st,
             char *map_out, size_t map_n, char *skin_out, size_t skin_n);
#endif
