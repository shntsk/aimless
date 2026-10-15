#include "game.h"
#include "audio.h"
#include "beatmap.h"
#include "skin.h"
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LEADIN 1500.0

static void draw_tex(SDL_Renderer *ren, Tex *t, float cx, float cy, float w, float h, int alpha) {
    if (!t->t) return;
    SDL_SetTextureAlphaMod(t->t, alpha);
    SDL_FRect d = { cx - w / 2, cy - h / 2, w, h };
    SDL_RenderCopyF(ren, t->t, NULL, &d);
}

static void draw_num(SDL_Renderer *ren, Tex *set, const char *str, float x, float y, float sc, int center, int alpha) {
    float tw = 0;
    for (const char *c = str; *c; c++) if (*c >= '0' && *c <= '9') tw += set[*c - '0'].w * sc;
    if (center) x -= tw / 2;
    for (const char *c = str; *c; c++) if (*c >= '0' && *c <= '9') {
        Tex *t = &set[*c - '0'];
        draw_tex(ren, t, x + t->w * sc / 2, y, t->w * sc, t->h * sc, alpha);
        x += t->w * sc;
    }
}

int game_play(SDL_Window *win, SDL_Renderer *ren, const char *map, const char *skin_dir, Settings *st) {
    Beatmap bm;
    int rc = beatmap_load(&bm, map, 0);
    if (rc) { snprintf(st->status, sizeof st->status, "failed to load beatmap (%d)", rc); return 0; }

    char ap[1024];
    snprintf(ap, sizeof ap, "%s/%s", bm.dir, bm.audio);
    Mix_Music *mus = Mix_LoadMUS(ap);
    if (!mus) fprintf(stderr, "warning: no audio (%s)\n", Mix_GetError());

    Skin sk;
    skin_load(&sk, ren, skin_dir && *skin_dir ? skin_dir : NULL);
    sfx_init(skin_dir && *skin_dir ? skin_dir : NULL, bm.dir);
    SDL_RenderSetVSync(ren, st->vsync);

    float assist = st->assist;
    float w300 = 80 - 6 * bm.od, w100 = 140 - 8 * bm.od, w50 = 200 - 10 * bm.od;
    float preempt = bm.ar < 5 ? 1200 + 600 * (5 - bm.ar) / 5 : 1200 - 750 * (bm.ar - 5) / 5;
    float rad = 54.4f - 4.48f * bm.cs;

    int first = 0, combo = 0, maxcombo = 0, hits[4] = {0}, running = 1, quit = 0, music_on = 0, W, H;
    double score = 0, fps_t = 0, last_t = 0; int fps_n = 0;
    Uint64 t0 = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();

    while (running) {
        double t = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq - LEADIN;
        if (!music_on && t >= 0 && mus) { Mix_PlayMusic(mus, 1); music_on = 1; }
        SDL_GetWindowSize(win, &W, &H);
        float s = H / 480.0f, ox = (W - 512 * s) / 2, oy = (H - 384 * s) / 2 + 8 * s;
        int mx, my; SDL_GetMouseState(&mx, &my);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int click = 0; Uint32 ts = 0;
            if (e.type == SDL_QUIT) { running = 0; quit = 1; }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                else if (e.key.keysym.sym == SDLK_z || e.key.keysym.sym == SDLK_x) { click = 1; ts = e.key.timestamp; }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) { click = 1; ts = e.button.timestamp; }

            if (click && first < bm.n) {
                /* judge at the time the input actually happened, not when the frame got to it */
                double lag = (double)(SDL_GetTicks() - ts);
                if (lag < 0 || lag > 50) lag = 0;
                double te = t - lag;
                Obj *o = &bm.objs[first];
                double diff = fabs(te - o->time);
                int near = assist <= 0 || hypot((mx - ox) / s - o->x, (my - oy) / s - o->y) <= rad * assist;
                if (diff <= w50 && near) {
                    o->state = 1; o->judge_time = (int)t;
                    o->judge = diff <= w300 ? 300 : diff <= w100 ? 100 : 50;
                    hits[o->judge == 300 ? 0 : o->judge == 100 ? 1 : 2]++;
                    score += o->judge * (1 + combo / 25.0);
                    if (++combo > maxcombo) maxcombo = combo;
                    sfx_play(o->hs, o->nset, o->aset, o->vol);
                    first++;
                }
            }
        }
        while (first < bm.n && t > bm.objs[first].time + w50) {
            Obj *o = &bm.objs[first++];
            o->state = 2; o->judge = 0; o->judge_time = (int)t; hits[3]++; combo = 0;
        }

        SDL_SetRenderDrawColor(ren, 12, 12, 16, 255);
        SDL_RenderClear(ren);

        int hi = first;
        while (hi < bm.n && bm.objs[hi].time - preempt <= t) hi++;
        for (int i = hi - 1; i >= 0 && i >= first - 24; i--) {
            Obj *o = &bm.objs[i];
            float px = ox + o->x * s, py = oy + o->y * s, d = rad * 2 * s * 1.05f;
            if (o->state) {
                float age = t - o->judge_time;
                if (age < 400) {
                    Tex *h = &sk.hit[o->judge == 300 ? 0 : o->judge == 100 ? 1 : o->judge == 50 ? 2 : 3];
                    float us = H / 768.0f;
                    draw_tex(ren, h, px, py, h->w * us, h->h * us, 255 - (int)(age / 400 * 255));
                }
                continue;
            }
            float fade = fminf(1, (t - (o->time - preempt)) / fminf(400, preempt));
            int a = (int)(fade * 255);
            Uint8 *c = sk.combo[o->color % sk.n_combo];
            SDL_SetTextureColorMod(sk.hitcircle.t, c[0], c[1], c[2]);
            draw_tex(ren, &sk.hitcircle, px, py, d, d, a);
            draw_tex(ren, &sk.overlay, px, py, d, d, a);
            char nb[16]; snprintf(nb, sizeof nb, "%d", o->num);
            draw_num(ren, sk.digit, nb, px, py, s * 0.9f, 1, a);
            float prog = (t - (o->time - preempt)) / preempt;
            if (prog < 1) {
                SDL_SetTextureColorMod(sk.approach.t, c[0], c[1], c[2]);
                float ad = d * (1 + 3 * (1 - prog));
                draw_tex(ren, &sk.approach, px, py, ad, ad, a);
            }
        }

        float us = H / 768.0f; char buf[96];
        snprintf(buf, sizeof buf, "%08d", (int)score);
        draw_num(ren, sk.score, buf, W - 180 * us, 28 * us, us, 1, 255);
        snprintf(buf, sizeof buf, "%d", combo);
        draw_num(ren, sk.score, buf, 10 * us, H - 40 * us, us, 0, 255);
        if (sk.score_x.t) draw_tex(ren, &sk.score_x, 10 * us + 40 * us * strlen(buf), H - 40 * us, sk.score_x.w * us, sk.score_x.h * us, 255);

        draw_tex(ren, &sk.cursor, mx, my, sk.cursor.w * us, sk.cursor.h * us, 255);
        SDL_RenderPresent(ren);

        fps_n++; fps_t += t - last_t; last_t = t;
        if (fps_t >= 500) {
            snprintf(buf, sizeof buf, "aimless - %.0f fps", fps_n * 1000.0 / fps_t);
            SDL_SetWindowTitle(win, buf); fps_n = 0; fps_t = 0;
        }
        if (first >= bm.n && t > bm.objs[bm.n - 1].time + 2500) running = 0;
    }

    int tot = hits[0] + hits[1] + hits[2] + hits[3];
    double acc = tot ? (300.0 * hits[0] + 100 * hits[1] + 50 * hits[2]) / (300.0 * tot) * 100 : 100;
    snprintf(st->status, sizeof st->status, "last: %d pts | combo %d | %d/%d/%d/%dx | %.2f%%",
             (int)score, maxcombo, hits[0], hits[1], hits[2], hits[3], acc);
    printf("%s\n", st->status);

    Mix_HaltMusic();
    sfx_free(); skin_free(&sk); beatmap_free(&bm);
    if (mus) Mix_FreeMusic(mus);
    SDL_SetWindowTitle(win, "aimless");
    return quit;
}
