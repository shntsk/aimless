/* aimless - tiny osu!-like fan game. Soft silent aim ON by default.
 *   aimless                      -> song select (drop .osz/.osk files on the window to import)
 *   aimless <map.osu> [skin_dir] -> play directly
 *   aimless import <files...>    -> import .osz / .osk / folders and exit
 * flags: --no-silent  --assist=N  --vsync  --songs=DIR  --skins=DIR */
#include "game.h"
#include "paths.h"
#include "ui.h"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    Settings st; memset(&st, 0, sizeof st);
    st.assist = 2.5f;
    const char *map = NULL, *skin = "";
    int do_import = 0;
    ensure_dirs();

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "import")) do_import = 1;
        else if (!strcmp(argv[i], "--no-silent")) st.assist = 1;
        else if (!strncmp(argv[i], "--assist=", 9)) st.assist = atof(argv[i] + 9);
        else if (!strcmp(argv[i], "--vsync")) st.vsync = 1;
        else if (!strncmp(argv[i], "--songs=", 8)) snprintf(st.songs_extra, sizeof st.songs_extra, "%s", argv[i] + 8);
        else if (!strncmp(argv[i], "--skins=", 8)) snprintf(st.skins_extra, sizeof st.skins_extra, "%s", argv[i] + 8);
        else if (do_import) { char m[256]; import_path(argv[i], m, sizeof m); puts(m); }
        else if (!map) map = argv[i];
        else skin = argv[i];
    }
    if (do_import) return 0;

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG); TTF_Init();
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 512)) fprintf(stderr, "audio: %s\n", Mix_GetError());

    SDL_Window *win = SDL_CreateWindow("aimless", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0); /* software fallback */
    if (!win || !ren) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_ShowCursor(SDL_DISABLE);

    int quit = 0;
    if (map) quit = game_play(win, ren, map, skin, &st);
    else {
        SDL_ShowCursor(SDL_ENABLE);
        char mp[1024], sp[1024];
        while (!quit && menu_run(win, ren, &st, mp, sizeof mp, sp, sizeof sp)) {
            SDL_ShowCursor(SDL_DISABLE);
            quit = game_play(win, ren, mp, sp, &st);
            SDL_ShowCursor(SDL_ENABLE);
        }
    }

    Mix_CloseAudio(); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    TTF_Quit(); IMG_Quit(); SDL_Quit();
    return 0;
}
