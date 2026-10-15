#include "ui.h"
#include "paths.h"
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TTF_Font *open_font(int size) {
    char path[512] = "";
    FILE *p = popen("fc-match -f '%{file}' sans 2>/dev/null", "r");
    if (p) { if (!fgets(path, sizeof path, p)) path[0] = 0; pclose(p); }
    TTF_Font *f = path[0] ? TTF_OpenFont(path, size) : NULL;
    static const char *fallback[] = { "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                                      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    for (int i = 0; !f && fallback[i]; i++) f = TTF_OpenFont(fallback[i], size);
    return f;
}

static void text(SDL_Renderer *ren, TTF_Font *f, const char *s, int x, int y, SDL_Color c, int max_w) {
    if (!f || !*s) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect src = { 0, 0, surf->w > max_w ? max_w : surf->w, surf->h };
    SDL_Rect dst = { x, y, src.w, src.h };
    SDL_RenderCopy(ren, t, &src, &dst);
    SDL_DestroyTexture(t); SDL_FreeSurface(surf);
}

int menu_run(SDL_Window *win, SDL_Renderer *ren, Settings *st,
             char *map_out, size_t map_n, char *skin_out, size_t skin_n) {
    TTF_Font *big = open_font(20), *small = open_font(14);
    if (!big || !small) { fprintf(stderr, "no font found (install fontconfig + a TTF font)\n"); return 0; }
    static const float ASSISTS[] = { 2.5f, 0.0f, 1.0f };
    int ai = 0;
    for (int i = 0; i < 3; i++) if (st->assist == ASSISTS[i]) ai = i;

    List songs = {0}, skins = {0};
    scan_songs(&songs, st->songs_extra); scan_skins(&skins, st->skins_extra);
    int sel = 0, scroll = 0, skin_sel = -1; /* -1 = built-in */
    int redraw = 1, result = 0, running = 1;
    const int ROW = 26, TOP = 64;
    SDL_Color white = {235, 235, 240, 255}, grey = {140, 140, 155, 255}, pink = {255, 102, 170, 255};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetVSync(ren, 1);

    while (running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 250)) do {
            int W, H; SDL_GetWindowSize(win, &W, &H);
            int rows = (H - TOP - 20) / ROW; if (rows < 1) rows = 1;
            redraw = 1;
            if (e.type == SDL_QUIT) { running = 0; result = 0; }
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; result = 0; break;
                case SDLK_DOWN: if (sel + 1 < songs.n) sel++; break;
                case SDLK_UP: if (sel > 0) sel--; break;
                case SDLK_PAGEDOWN: sel += rows; if (sel >= songs.n) sel = songs.n - 1; break;
                case SDLK_PAGEUP: sel -= rows; if (sel < 0) sel = 0; break;
                case SDLK_HOME: sel = 0; break;
                case SDLK_END: sel = songs.n - 1; break;
                case SDLK_RIGHT: if (skin_sel + 1 < skins.n) skin_sel++; break;
                case SDLK_LEFT: if (skin_sel > -1) skin_sel--; break;
                case SDLK_TAB: ai = (ai + 1) % 3; st->assist = ASSISTS[ai]; break;
                case SDLK_F5:
                    list_free(&songs); list_free(&skins);
                    scan_songs(&songs, st->songs_extra); scan_skins(&skins, st->skins_extra);
                    sel = 0; skin_sel = -1; break;
                case SDLK_RETURN: case SDLK_KP_ENTER:
                    if (songs.n) { running = 0; result = 1; }
                    break;
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                sel -= e.wheel.y * 3; if (sel < 0) sel = 0; if (sel >= songs.n) sel = songs.n - 1;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int idx = scroll + (e.button.y - TOP) / ROW;
                if (e.button.y >= TOP && e.button.x < W - 340 && idx >= 0 && idx < songs.n) {
                    sel = idx;
                    if (e.button.clicks >= 2) { running = 0; result = 1; }
                }
            } else if (e.type == SDL_DROPFILE) {
                import_path(e.drop.file, st->status, sizeof st->status);
                SDL_free(e.drop.file);
                list_free(&songs); list_free(&skins);
                scan_songs(&songs, st->songs_extra); scan_skins(&skins, st->skins_extra);
            }
            if (songs.n) { if (sel < scroll) scroll = sel; if (sel >= scroll + rows) scroll = sel - rows + 1; }
        } while (running && SDL_PollEvent(&e));
        if (!running) break;

        if (redraw) {
            int W, H; SDL_GetWindowSize(win, &W, &H);
            int rows = (H - TOP - 20) / ROW; if (rows < 1) rows = 1;
            SDL_SetRenderDrawColor(ren, 12, 12, 16, 255); SDL_RenderClear(ren);
            text(ren, big, "aimless", 20, 18, pink, 300);
            char b[600]; snprintf(b, sizeof b, "%d maps", songs.n);
            text(ren, small, b, 110, 24, grey, 200);
            for (int i = 0; i < rows && scroll + i < songs.n; i++) {
                int y = TOP + i * ROW;
                if (scroll + i == sel) { SDL_SetRenderDrawColor(ren, 255, 102, 170, 70); SDL_Rect r = { 12, y - 2, W - 364, ROW }; SDL_RenderFillRect(ren, &r); }
                text(ren, small, songs.e[scroll + i].label, 20, y + 2, scroll + i == sel ? white : grey, W - 380);
            }
            if (!songs.n) text(ren, small, "no maps yet: drop a .osz here, or use --songs=DIR", 20, TOP, grey, W - 380);
            int x = W - 320, y = TOP;
            text(ren, small, "SKIN  (left/right)", x, y, pink, 300); y += 22;
            text(ren, small, skin_sel < 0 ? "built-in" : skins.e[skin_sel].label, x, y, white, 300); y += 36;
            text(ren, small, "AIM  (tab)", x, y, pink, 300); y += 22;
            text(ren, small, ai == 0 ? "silent aim, soft (2.5x)" : ai == 1 ? "silent aim, anywhere" : "off (normal aim)", x, y, white, 300); y += 36;
            text(ren, small, "enter / double-click: play", x, y, grey, 300); y += 20;
            text(ren, small, "drop .osz / .osk / folder: import", x, y, grey, 300); y += 20;
            text(ren, small, "F5: rescan    esc: quit", x, y, grey, 300); y += 36;
            text(ren, small, st->status, x, y, white, 300);
            SDL_RenderPresent(ren);
            redraw = 0;
        }
    }

    if (result && songs.n) {
        snprintf(map_out, map_n, "%s", songs.e[sel].path);
        snprintf(skin_out, skin_n, "%s", skin_sel < 0 ? "" : skins.e[skin_sel].path);
    }
    list_free(&songs); list_free(&skins);
    TTF_CloseFont(big); TTF_CloseFont(small);
    return result;
}
