#include "game.h"
#include "audio.h"
#include "beatmap.h"
#include "font.h"
#include <SDL_image.h>
#include "skin.h"
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEADIN 1500.0

static void draw_tex(SDL_Renderer *ren, Tex *t, float cx, float cy, float w, float h, int alpha) {
    if (!t->t) return;
    SDL_SetTextureAlphaMod(t->t, alpha);
    SDL_FRect d = { cx - w / 2, cy - h / 2, w, h };
    SDL_RenderCopyF(ren, t->t, NULL, &d);
}

static float draw_num(SDL_Renderer *ren, Tex *set, const char *str, float x, float y, float sc, int center, int alpha) {
    float tw = 0;
    for (const char *c = str; *c; c++) if (*c >= '0' && *c <= '9') tw += set[*c - '0'].w * sc;
    if (center) x -= tw / 2;
    for (const char *c = str; *c; c++) if (*c >= '0' && *c <= '9') {
        Tex *t = &set[*c - '0'];
        draw_tex(ren, t, x + t->w * sc / 2, y, t->w * sc, t->h * sc, alpha);
        x += t->w * sc;
    }
    return x;
}

static SDL_Texture *load_bg(SDL_Renderer *ren, const Beatmap *bm, int *w, int *h) {
    if (!bm->bg[0]) return NULL;
    char p[1100]; snprintf(p, sizeof p, "%s/%s", bm->dir, bm->bg);
    SDL_Surface *su = IMG_Load(p);
    if (!su) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, su);
    *w = su->w; *h = su->h;
    SDL_FreeSurface(su);
    if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

/* blur = repeated 2x downscale with linear filtering into render targets; upscaled linearly when drawn */
static SDL_Texture *blur_texture(SDL_Renderer *ren, SDL_Texture *src, int w, int h, int level) {
    int steps = (level + 19) / 20; /* 1..5 */
    SDL_Texture *cur = src;
    for (int i = 0; i < steps && w > 32 && h > 32; i++) {
        w /= 2; h /= 2;
        SDL_Texture *nt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!nt) break;
        SDL_SetTextureScaleMode(nt, SDL_ScaleModeLinear);
        SDL_SetRenderTarget(ren, nt);
        SDL_RenderCopy(ren, cur, NULL, NULL);
        SDL_SetRenderTarget(ren, NULL);
        SDL_DestroyTexture(cur);
        cur = nt;
    }
    return cur;
}

typedef struct { float x, y; double t; } Flash;
#define N_FL 32
#define N_ERR 40
#define N_TR 96

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
    TTF_Font *hud_font = font_open(18), *num_font = font_open(44);
    skin_fallback_digits(&sk, ren, num_font);
    int bgw = 0, bgh = 0;
    SDL_Texture *bg = st->show_bg ? load_bg(ren, &bm, &bgw, &bgh) : NULL;
    if (bg && st->bg_blur > 0) bg = blur_texture(ren, bg, bgw, bgh, st->bg_blur);
    TextCache t_fps = {0}, t_acc = {0}, t_lag = {0};
    SDL_Color hud_col = { 235, 235, 240, 255 };
    Mix_VolumeMusic((int)(MIX_MAX_VOLUME * st->music_vol / 100.0 * st->master_vol / 100.0));
    sfx_init(skin_dir && *skin_dir ? skin_dir : NULL, bm.dir);
    SDL_RenderSetVSync(ren, st->vsync);

    float assist = st->assist;
    float w300 = 80 - 6 * bm.od, w100 = 140 - 8 * bm.od, w50 = 200 - 10 * bm.od;
    float preempt = bm.ar < 5 ? 1200 + 600 * (5 - bm.ar) / 5 : 1200 - 750 * (bm.ar - 5) / 5;
    float rad = 54.4f - 4.48f * bm.cs;

    int first = 0, combo = 0, maxcombo = 0, hits[4] = {0}, running = 1, quit = 0, music_on = 0, W, H;
    double score = 0, fps_t = 0, last_t = 0, pop_t = -1e9, lag_sum = 0; int fps_n = 0, lag_n = 0, lag_last = 0;
    float fps_show = 0;
    Flash fl[N_FL] = {{0}}; int fl_i = 0;
    float err_v[N_ERR] = {0}; double err_t[N_ERR] = {0}; int err_j[N_ERR] = {0}, err_i = 0;
    Flash tr[N_TR] = {{0}}; int tr_i = 0; double tr_last = -1e9;
    Uint64 next_frame = SDL_GetPerformanceCounter();
    SDL_Color col300 = {80, 200, 255, 255}, col100 = {120, 230, 110, 255}, col50 = {255, 210, 80, 255};
    Uint64 t0 = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();

    while (running) {
        double t = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq - LEADIN;
        if (!music_on && t >= 0 && mus) { Mix_PlayMusic(mus, 1); music_on = 1; }
        SDL_GetWindowSize(win, &W, &H);
        float s = H / 480.0f, ox = (W - 512 * s) / 2, oy = (H - 384 * s) / 2 + 8 * s;
        int mx, my; SDL_GetMouseState(&mx, &my);

#ifdef AIMLESS_DEBUG /* test hook: perfect-ish autoplay clicks + screenshot (not in release builds) */
        if (getenv("AIMLESS_AUTOHIT") && first < bm.n && t >= bm.objs[first].time + ((first * 37) % 60) - 30) {
            SDL_Event ev; memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEBUTTONDOWN; ev.button.timestamp = SDL_GetTicks(); ev.button.button = SDL_BUTTON_LEFT;
            SDL_PushEvent(&ev);
        }
#endif
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
                lag_last = (int)lag; lag_sum += lag; lag_n++;
                double te = t - lag - st->input_delay;
                Obj *o = &bm.objs[first];
                double diff = fabs(te - o->time);
                int near = assist <= 0 || hypot((mx - ox) / s - o->x, (my - oy) / s - o->y) <= rad * assist;
                if (diff <= w50 && near) {
                    o->state = 1; o->judge_time = (int)t;
                    o->judge = diff <= w300 ? 300 : diff <= w100 ? 100 : 50;
                    hits[o->judge == 300 ? 0 : o->judge == 100 ? 1 : 2]++;
                    score += o->judge * (1 + combo / 25.0);
                    if (++combo > maxcombo) maxcombo = combo;
                    pop_t = t;
                    err_v[err_i % N_ERR] = (float)(te - o->time); err_t[err_i % N_ERR] = t; err_j[err_i % N_ERR] = o->judge; err_i++;
                    if (st->hit_lighting) { fl[fl_i % N_FL] = (Flash){ ox + o->x * s, oy + o->y * s, t }; fl_i++; }
                    sfx_play(o->hs, o->nset, o->aset, o->vol * st->sfx_vol / 100 * st->master_vol / 100);
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
        if (bg) { /* cover-fit, darkened with a colour mod (no extra blend pass) */
            float k = fmaxf((float)W / bgw, (float)H / bgh), dw = bgw * k, dh = bgh * k;
            SDL_FRect d = { (W - dw) / 2, (H - dh) / 2, dw, dh };
            Uint8 br = (Uint8)(255 * (100 - st->bg_dim) / 100);
            SDL_SetTextureColorMod(bg, br, br, br);
            SDL_RenderCopyF(ren, bg, NULL, &d);
        }

        if (st->border) { /* osu!standard playfield border: corners or full */
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 80);
            SDL_FRect pf = { ox, oy, 512 * s, 384 * s };
            if (st->border == 2) SDL_RenderDrawRectF(ren, &pf);
            else {
                float L = 28 * s / 2;
                float xs[2] = { pf.x, pf.x + pf.w }, ys[2] = { pf.y, pf.y + pf.h };
                for (int a = 0; a < 2; a++) for (int b2 = 0; b2 < 2; b2++) {
                    float dx = a ? -L : L, dy = b2 ? -L : L;
                    SDL_RenderDrawLineF(ren, xs[a], ys[b2], xs[a] + dx, ys[b2]);
                    SDL_RenderDrawLineF(ren, xs[a], ys[b2], xs[a], ys[b2] + dy);
                }
            }
        }

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

        if (st->hit_lighting) { /* additive flash, grows and fades */
            SDL_SetTextureBlendMode(sk.hitcircle.t, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(sk.hitcircle.t, 255, 255, 255);
            for (int i = 0; i < N_FL; i++) {
                double age = t - fl[i].t;
                if (age < 0 || age > 260 || fl[i].t == 0) continue;
                float k = (float)(age / 260), d = rad * 2 * s * (1.0f + 0.6f * k);
                draw_tex(ren, &sk.hitcircle, fl[i].x, fl[i].y, d, d, (int)(110 * (1 - k)));
            }
            SDL_SetTextureBlendMode(sk.hitcircle.t, SDL_BLENDMODE_BLEND);
        }
        if (st->show_error_bar) { /* hit error meter: early left, late right */
            float us2 = H / 768.0f, bw = 280 * us2, bx = W / 2.0f, by = H - 26 * us2;
            int w50h = (int)w50;
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 40);
            SDL_FRect bar = { bx - bw / 2, by - 1, bw, 2 }; SDL_RenderFillRectF(ren, &bar);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 140);
            SDL_FRect mid = { bx - 1, by - 8 * us2, 2, 16 * us2 }; SDL_RenderFillRectF(ren, &mid);
            for (int i = 0; i < N_ERR; i++) {
                double age = t - err_t[i];
                if (err_t[i] == 0 || age > 6000) continue;
                SDL_Color c = err_j[i] == 300 ? col300 : err_j[i] == 100 ? col100 : col50;
                float px = bx + err_v[i] / w50h * (bw / 2);
                SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, (Uint8)(220 * (1 - age / 6000)));
                SDL_FRect tk = { px - 1, by - 6 * us2, 2, 12 * us2 }; SDL_RenderFillRectF(ren, &tk);
            }
        }

        float us = H / 768.0f; char buf[96];
        if (st->show_score) {
            snprintf(buf, sizeof buf, "%08d", (int)score);
            draw_num(ren, sk.score, buf, W - 180 * us, 28 * us, us, 1, 255);
        }
        if (st->show_acc) {
            int tot = hits[0] + hits[1] + hits[2] + hits[3];
            double a = tot ? (300.0 * hits[0] + 100 * hits[1] + 50 * hits[2]) / (300.0 * tot) * 100 : 100;
            snprintf(buf, sizeof buf, "%.2f%%", a);
            text_set(ren, &t_acc, hud_font, buf, hud_col);
            text_blit(ren, &t_acc, W - t_acc.w - 12, (int)(28 * us + 30 * us), 230);
        }
        if (st->show_combo && combo > 0) {
            float pop = 1 + 0.3f * fmaxf(0, 1 - (float)(t - pop_t) / 160);
            snprintf(buf, sizeof buf, "%d", combo);
            float bx = 10 * us, by = H - 40 * us * pop;
            float ex = draw_num(ren, sk.score, buf, bx, by, us * pop, 0, 255);
            if (sk.score_x.t) draw_tex(ren, &sk.score_x, ex + sk.score_x.w * us / 2, by, sk.score_x.w * us * pop, sk.score_x.h * us * pop, 255);
        }
        int hy = 8;
        if (st->show_fps) {
            snprintf(buf, sizeof buf, "%.0f fps", fps_show);
            text_set(ren, &t_fps, hud_font, buf, hud_col);
            text_blit(ren, &t_fps, 10, hy, 200); hy += t_fps.h + 2;
        }
        if (st->show_input_delay) {
            snprintf(buf, sizeof buf, "input %d ms (avg %.1f) | comp %+d ms", lag_last, lag_n ? lag_sum / lag_n : 0.0, st->input_delay);
            text_set(ren, &t_lag, hud_font, buf, hud_col);
            text_blit(ren, &t_lag, 10, hy, 200);
        }

        if (st->cursor_trail) { /* short fading trail, sampled by time so it is fps independent */
            if (t - tr_last >= 2.5) { tr[tr_i % N_TR] = (Flash){ (float)mx, (float)my, t }; tr_i++; tr_last = t; }
            for (int i = 0; i < N_TR; i++) {
                double age = t - tr[i].t;
                if (tr[i].t == 0 || age < 0 || age > 140) continue;
                float k = (float)(age / 140), tw = sk.cursor.w * us * st->cursor_size * (1 - 0.35f * k);
                draw_tex(ren, &sk.cursor, tr[i].x, tr[i].y, tw, tw * sk.cursor.h / sk.cursor.w, (int)(90 * (1 - k)));
            }
        }
        float cs = sk.cursor.w * us * st->cursor_size, ch = sk.cursor.h * us * st->cursor_size;
        draw_tex(ren, &sk.cursor, mx, my, cs, ch, 255);
#ifdef AIMLESS_DEBUG
        if (getenv("AIMLESS_SHOT") && t >= atof(getenv("AIMLESS_SHOT_T") ? getenv("AIMLESS_SHOT_T") : "3300")) {
            SDL_Surface *ss = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch);
            IMG_SavePNG(ss, getenv("AIMLESS_SHOT")); SDL_FreeSurface(ss); running = 0;
        }
#endif
        SDL_RenderPresent(ren);
        if (!st->vsync && st->fps_limit > 0) { /* frame limiter: sleep most of the wait, spin the last bit */
            Uint64 period = freq / st->fps_limit; next_frame += period;
            Uint64 now = SDL_GetPerformanceCounter();
            if (next_frame < now) next_frame = now;
            while ((now = SDL_GetPerformanceCounter()) < next_frame) if ((next_frame - now) * 1000 > 2 * freq) SDL_Delay(1);
        }

        fps_n++; fps_t += t - last_t; last_t = t;
        if (fps_t >= 500) { fps_show = (float)(fps_n * 1000.0 / fps_t); fps_n = 0; fps_t = 0; }
        if (first >= bm.n && t > bm.objs[bm.n - 1].time + 2500) running = 0;
    }

    int tot = hits[0] + hits[1] + hits[2] + hits[3];
    double acc = tot ? (300.0 * hits[0] + 100 * hits[1] + 50 * hits[2]) / (300.0 * tot) * 100 : 100;
    snprintf(st->status, sizeof st->status, "last: %d pts | combo %d | %d/%d/%d/%dx | %.2f%%",
             (int)score, maxcombo, hits[0], hits[1], hits[2], hits[3], acc);
    printf("%s\n", st->status);

    Mix_HaltMusic();
    sfx_free(); skin_free(&sk); beatmap_free(&bm);
    if (bg) SDL_DestroyTexture(bg);
    text_free(&t_fps); text_free(&t_acc); text_free(&t_lag);
    if (hud_font) TTF_CloseFont(hud_font);
    if (num_font) TTF_CloseFont(num_font);
    if (mus) Mix_FreeMusic(mus);
    return quit;
}
