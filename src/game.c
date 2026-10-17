#include "game.h"
#include "audio.h"
#include "beatmap.h"
#include "font.h"
#include <SDL_image.h>
#include "skin.h"
#include "circlegeom.h"
#include "numlayout.h"
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEADIN 1500.0
#define AUDIO_LAT_MS 6.0   /* ~half an audio buffer: Mix_GetMusicPosition is the decoded, not the heard, position */

static void draw_tex(SDL_Renderer *ren, Tex *t, float cx, float cy, float w, float h, int alpha) {
    if (!t->t) return;
    SDL_SetTextureAlphaMod(t->t, alpha);
    SDL_FRect d = { cx - w / 2, cy - h / 2, w, h };
    SDL_RenderCopyF(ren, t->t, NULL, &d);
}

/* A run of digits from set[]. overlap is in the sprites' logical px and is scaled with them (see numlayout.h);
 * the HUD passes 0. Returns the x where the run ends. */
static float draw_num(SDL_Renderer *ren, Tex *set, const char *str, float x, float y, float sc, int center, int alpha, float overlap) {
    float w[64]; Tex *tx[64]; float left[64]; int n = 0;
    for (const char *c = str; *c && n < 64; c++) if (*c >= '0' && *c <= '9') { tx[n] = &set[*c - '0']; w[n] = (float)tx[n]->w; n++; }
    float tw = nl_layout(w, n, overlap, sc, left);
    if (center) x -= tw / 2;
    for (int i = 0; i < n; i++)
        draw_tex(ren, tx[i], x + left[i] + tx[i]->w * sc / 2, y, tx[i]->w * sc, tx[i]->h * sc, alpha);
    return x + tw;
}

/* Hit circle head: base, number and overlay in the order the skin asks for (cg_layers). Every sprite keeps
 * its own proportions (see circlegeom.h). The colour mod is set per draw and the overlay is never tinted. */
static void draw_circle_head(SDL_Renderer *ren, Skin *sk, const Uint8 *c, float px, float py, float d, float s, int num, int alpha) {
    float k = cg_k(d, sk->hitcircle.w);
    int order[3]; cg_layers(sk->overlay_above_number, order);
    for (int i = 0; i < 3; i++) {
        if (order[i] == CG_BASE) {
            SDL_SetTextureColorMod(sk->hitcircle.t, c[0], c[1], c[2]);
            draw_tex(ren, &sk->hitcircle, px, py, sk->hitcircle.w * k, sk->hitcircle.h * k, alpha);
        } else if (order[i] == CG_OVERLAY) {
            if (sk->overlay.t) SDL_SetTextureColorMod(sk->overlay.t, 255, 255, 255);
            draw_tex(ren, &sk->overlay, px, py, sk->overlay.w * k, sk->overlay.h * k, alpha);
        } else {
            char nb[16]; snprintf(nb, sizeof nb, "%d", num);
            /* image digits are drawn at the same factor as the circle sprites (they are authored on the same
             * canvas); digits made from the TTF fallback have no logical size and keep the legacy 0.9 * s */
            float ns = sk->digit_img ? k : s * NL_LEGACY_SCALE;
            draw_num(ren, sk->digit, nb, px, py, ns, 1, alpha, (float)sk->hitcircle_overlap);
        }
    }
}

/* approach circle: drawn in its own pass above every circle on screen; 4x -> 1x of its natural size */
static void draw_approach(SDL_Renderer *ren, Skin *sk, const Uint8 *c, float px, float py, float d, float prog, int alpha) {
    float k = cg_k(d, sk->hitcircle.w) * cg_approach(prog);
    SDL_SetTextureColorMod(sk->approach.t, c[0], c[1], c[2]);
    draw_tex(ren, &sk->approach, px, py, sk->approach.w * k, sk->approach.h * k, alpha);
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


static void tex_rot(SDL_Renderer *ren, Tex *t, float cx, float cy, float w, float h, double deg, int alpha) {
    if (!t->t) return;
    SDL_SetTextureAlphaMod(t->t, alpha);
    SDL_FRect d = { cx - w / 2, cy - h / 2, w, h };
    SDL_RenderCopyExF(ren, t->t, NULL, &d, deg, NULL, SDL_FLIP_NONE);
}

/* centre of the slider at song time t (osu px) */
static void slider_pos(const Obj *o, double t, float *x, float *y) {
    double sd = (double)(o->endtime - o->time) / o->slides, u = sd > 0 ? (t - o->time) / sd : 0;
    if (u < 0) u = 0;
    if (u > o->slides) u = o->slides;
    int sl = (int)u; if (sl >= o->slides) sl = o->slides - 1;
    double f = u - sl; if (sl & 1) f = 1 - f;
    double di = f * (o->npath - 1); int i0 = (int)di;
    if (i0 > o->npath - 2) i0 = o->npath - 2;
    if (i0 < 0) i0 = 0;
    float fr = (float)(di - i0);
    *x = o->path[i0*2] + (o->path[i0*2+2] - o->path[i0*2]) * fr;
    *y = o->path[i0*2+1] + (o->path[i0*2+3] - o->path[i0*2+1]) * fr;
}

/* body, ticks, end circle, reverse arrows, ball + follow circle, final judgement. The head circle and the
 * approach circle are drawn by the caller while the head is still pending. */
static void draw_slider(SDL_Renderer *ren, Skin *sk, Obj *o, double t, float s, float ox, float oy,
                        float rad, float preempt, int H) {
    float d = rad * 2 * s * 1.05f;
    float kk = sk->hitcircle.w ? d / sk->hitcircle.w : 1;       /* skin sprites are authored against hitcircle */
    if (o->done) { /* judgement flash at the tail, nothing else */
        float age = (float)(t - o->judge_time);
        if (age < 400 && age >= 0) {
            float ex = ox + o->path[(o->npath - 1) * 2] * s, ey = oy + o->path[(o->npath - 1) * 2 + 1] * s;
            Tex *h = &sk->hit[o->judge == 300 ? 0 : o->judge == 100 ? 1 : o->judge == 50 ? 2 : 3];
            float us = H / 768.0f;
            draw_tex(ren, h, ex, ey, h->w * us, h->h * us, 255 - (int)(age / 400 * 255));
        }
        return;
    }
    float fade = fminf(1, (float)(t - (o->time - preempt)) / fminf(400, preempt));
    int a = (int)(fade * 255);
    Uint8 *c = sk->combo[o->color % sk->n_combo];
    Uint8 trk[3] = { (Uint8)(c[0] * 35 / 100), (Uint8)(c[1] * 35 / 100), (Uint8)(c[2] * 35 / 100) };
    if (sk->has_track) memcpy(trk, sk->slider_track, 3);

    /* body: stamp discs along the path, border first, then the darker track on top */
    int stepi = (int)(rad * 0.3f / o->step); if (stepi < 1) stepi = 1;
    for (int pass = 0; pass < 2; pass++) {
        const Uint8 *col = pass ? trk : sk->slider_border;
        float dd = pass ? d * 0.86f : d;
        /* opaque stamps (overlapping translucent ones band): fade in by scaling the colour instead */
        SDL_SetTextureColorMod(sk->disc.t, col[0] * a / 255, col[1] * a / 255, col[2] * a / 255);
        for (int i = 0; ; i += stepi) {
            if (i > o->npath - 1) i = o->npath - 1;
            draw_tex(ren, &sk->disc, ox + o->path[i*2] * s, oy + o->path[i*2+1] * s, dd, dd, 255);
            if (i == o->npath - 1) break;
        }
    }
    SDL_SetTextureColorMod(sk->disc.t, 255, 255, 255);

    /* upcoming ticks */
    for (int e = o->evi; e < o->nev; e++) if (o->ev[e*2+1] == 0) {
        float tx, ty; slider_pos(o, o->ev[e*2], &tx, &ty);
        draw_tex(ren, &sk->scorepoint, ox + tx * s, oy + ty * s, sk->scorepoint.w * kk, sk->scorepoint.h * kk, a);
    }

    /* end circle */
    float ex = ox + o->path[(o->npath - 1) * 2] * s, ey = oy + o->path[(o->npath - 1) * 2 + 1] * s;
    if (sk->endcircle.t) {
        /* a fully transparent sliderendcircle is the skin's choice and stays invisible; otherwise tint like the head */
        SDL_SetTextureColorMod(sk->endcircle.t, c[0], c[1], c[2]);
        draw_tex(ren, &sk->endcircle, ex, ey, d, d, a);
        if (sk->endoverlay.t) draw_tex(ren, &sk->endoverlay, ex, ey, d, d, a);
        else draw_tex(ren, &sk->overlay, ex, ey, sk->overlay.w * kk, sk->overlay.h * kk, a);
    } else {
        SDL_SetTextureColorMod(sk->hitcircle.t, c[0] * 6 / 10, c[1] * 6 / 10, c[2] * 6 / 10);
        draw_tex(ren, &sk->hitcircle, ex, ey, sk->hitcircle.w * kk, sk->hitcircle.h * kk, a);
        draw_tex(ren, &sk->overlay, ex, ey, sk->overlay.w * kk, sk->overlay.h * kk, a);
    }

    /* reverse arrows at the next (up to two) turning points */
    double sd = (double)(o->endtime - o->time) / o->slides;
    int cur = sd > 0 ? (int)((t - o->time) / sd) : 0;
    if (cur < 0) cur = 0;
    for (int n = cur + 1; n <= cur + 2 && n < o->slides; n++) {
        int at_end = n & 1;
        int i0 = at_end ? o->npath - 1 : 0, i1 = at_end ? o->npath - 1 - (o->npath > 6 ? 5 : 1) : (o->npath > 6 ? 5 : 1);
        float px = ox + o->path[i0*2] * s, py = oy + o->path[i0*2+1] * s;
        double ang = atan2(o->path[i1*2+1] - o->path[i0*2+1], o->path[i1*2] - o->path[i0*2]) * 180.0 / M_PI;
        float pulse = 1 + 0.08f * sinf((float)t / 120.0f);
        tex_rot(ren, &sk->arrow, px, py, sk->arrow.w * kk * pulse, sk->arrow.h * kk * pulse, ang, a);
    }

    /* ball + follow circle */
    if (t >= o->time && t <= o->endtime) {
        float bx, by; slider_pos(o, t, &bx, &by); bx = ox + bx * s; by = oy + by * s;
        float grow = fminf(1, (float)(t - o->time) / 90);
        float fs = (o->trk ? 1.0f : 0.7f) * (0.5f + 0.5f * grow);
        draw_tex(ren, &sk->follow, bx, by, sk->follow.w * kk * fs, sk->follow.h * kk * fs, o->trk ? 255 : 130);
        Tex *bl = &sk->sliderb[((int)(t / 30)) % sk->n_sliderb];
        if (sk->ball_tint) SDL_SetTextureColorMod(bl->t, c[0], c[1], c[2]);
        else SDL_SetTextureColorMod(bl->t, 255, 255, 255);
        draw_tex(ren, bl, bx, by, bl->w * kk, bl->h * kk, 255);
    }
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
    sfx_prepare(&bm);
    SDL_RenderSetVSync(ren, st->vsync);

    float assist = st->assist;
    float w300 = 80 - 6 * bm.od, w100 = 140 - 8 * bm.od, w50 = 200 - 10 * bm.od;
    float preempt = bm.ar < 5 ? 1200 + 600 * (5 - bm.ar) / 5 : 1200 - 750 * (bm.ar - 5) / 5;
    float rad = 54.4f - 4.48f * bm.cs;

    int first = 0, combo = 0, maxcombo = 0, hits[4] = {0}, running = 1, quit = 0, music_on = 0, W, H;
    double clk = 0; int sl_first = 0;
#define BREAK_COMBO() do { if (combo >= 20) sfx_miss(st->sfx_vol * st->master_vol / 100); combo = 0; } while (0)
    double score = 0, fps_t = 0, last_t = 0, pop_t = -1e9, lag_sum = 0; int fps_n = 0, lag_n = 0, lag_last = 0;
    float fps_show = 0;
    Flash fl[N_FL] = {{0}}; int fl_i = 0;
    float err_v[N_ERR] = {0}; double err_t[N_ERR] = {0}; int err_j[N_ERR] = {0}, err_i = 0;
    Flash tr[N_TR] = {{0}}; int tr_i = 0; double tr_last = -1e9;
    Uint64 next_frame = SDL_GetPerformanceCounter();
    SDL_Color col300 = {80, 200, 255, 255}, col100 = {120, 230, 110, 255}, col50 = {255, 210, 80, 255};
    Uint64 t0 = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();

    while (running) {
        double wall = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq - LEADIN;
        if (!music_on && wall >= 0 && mus) { Mix_PlayMusic(mus, 1); music_on = 1; }
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
        /* The game clock is the wall clock, but the music starts late (decoder + device buffer) and
         * its real speed can differ a little. Slowly steer the clock toward the audio position so
         * the hit timing and the hitsounds line up with what is actually heard. */
        if (music_on && Mix_PlayingMusic()) {
            double p = Mix_GetMusicPosition(mus);
            if (p >= 0) {
                double err = p * 1000.0 - AUDIO_LAT_MS - wall;
                if (fabs(err - clk) > 150) clk = err;              /* seek / first sync */
                else clk += (err - clk) * 0.03;                    /* low-pass: position updates in buffer-sized steps */
#ifdef AIMLESS_DEBUG
                if (getenv("AIMLESS_CLK") && (fps_n % 200) == 0) fprintf(stderr, "wall %.0f pos %.1f err %.1f clk %.1f\n", wall, p * 1000, err, clk);
#endif
            }
        }
#endif
        double t = wall + clk;
        SDL_GetWindowSize(win, &W, &H);
        float s = H / 480.0f, ox = (W - 512 * s) / 2, oy = (H - 384 * s) / 2 + 8 * s;
        int mx, my; Uint32 mbtn = SDL_GetMouseState(&mx, &my);

#ifdef AIMLESS_DEBUG /* test hook: perfect-ish autoplay clicks + screenshot (not in release builds) */
        if (getenv("AIMLESS_AUTOHIT") && first < bm.n && !(getenv("AIMLESS_MISS") && first == atoi(getenv("AIMLESS_MISS"))) && t >= bm.objs[first].time + ((first * 37) % 60) - 30) {
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
                    if (!o->type) hits[o->judge == 300 ? 0 : o->judge == 100 ? 1 : 2]++; /* sliders are judged at their end */
                    score += (o->type ? 30 : o->judge) * (1 + combo / 25.0);
                    if (++combo > maxcombo) maxcombo = combo;
                    pop_t = t;
                    err_v[err_i % N_ERR] = (float)(te - o->time); err_t[err_i % N_ERR] = t; err_j[err_i % N_ERR] = o->judge; err_i++;
                    if (st->hit_lighting) { fl[fl_i % N_FL] = (Flash){ ox + o->x * s, oy + o->y * s, t }; fl_i++; }
                    if (o->type) sfx_play(o->nhs[0], o->nns[0], o->nas[0], o->idx, o->sfile, o->vol * st->sfx_vol / 100 * st->master_vol / 100);
                    else sfx_play(o->hs, o->nset, o->aset, o->idx, o->sfile, o->vol * st->sfx_vol / 100 * st->master_vol / 100);
                    first++;
                }
            }
        }
        while (first < bm.n && t > bm.objs[first].time + w50) {
            Obj *o = &bm.objs[first++];
            o->state = 2; o->judge = 0; o->judge_time = (int)t;
            if (!o->type) hits[3]++;
            BREAK_COMBO();
        }

        { /* sliders whose head is resolved: follow, ticks, repeats, tail */
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            int held = (mbtn & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_X];
#ifdef AIMLESS_DEBUG
            if (getenv("AIMLESS_AUTOHIT")) held = 1;
#endif
            for (int i = sl_first; i < first; i++) {
                Obj *o = &bm.objs[i];
                if (!o->type || o->done) continue;
                float sx, sy; slider_pos(o, t, &sx, &sy);
                float dist = (float)hypot(mx - (ox + sx * s), my - (oy + sy * s));
                int in = assist <= 0 || dist <= rad * s * fmaxf(2.4f, assist);
                o->trk = t >= o->time && held && in;
                int svol = o->vol * st->sfx_vol / 100 * st->master_vol / 100;
                while (o->evi < o->nev && o->ev[o->evi * 2] <= t) {
                    int kind = o->ev[o->evi * 2 + 1];
                    if (o->trk) {
                        o->ev_hit++;
                        if (++combo > maxcombo) maxcombo = combo;
                        pop_t = t;
                        score += (kind ? 30 : 10) * (1 + combo / 25.0);
                        if (kind == 0) sfx_tick(o->nset, o->idx, svol);
                        else { int n = ++o->ev_node; sfx_play(o->nhs[n], o->nns[n], o->nas[n], o->idx, "", svol); }
                    } else {
                        if (kind) o->ev_node++;
                        BREAK_COMBO();
                    }
                    o->evi++;
                }
                if (o->evi >= o->nev) { /* judged by how much of the slider was followed, head included */
                    int tot = o->nev + 1, got = o->ev_hit + (o->state == 1);
                    o->judge = got == tot ? 300 : got * 2 >= tot ? 100 : got > 0 ? 50 : 0;
                    hits[o->judge == 300 ? 0 : o->judge == 100 ? 1 : o->judge == 50 ? 2 : 3]++;
                    o->judge_time = (int)t; o->done = 1; o->trk = 0;
                }
            }
            while (sl_first < first && (!bm.objs[sl_first].type || bm.objs[sl_first].done)) sl_first++;
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
        int lo = first - 24; if (sl_first < lo) lo = sl_first; if (lo < 0) lo = 0;
        for (int i = hi - 1; i >= lo; i--) {
            Obj *o = &bm.objs[i];
            float px = ox + o->x * s, py = oy + o->y * s, d = rad * 2 * s * 1.05f;
            if (o->type) {
                if (o->done && t > o->judge_time + 400) continue;
                draw_slider(ren, &sk, o, t, s, ox, oy, rad, preempt, H);
                if (o->state) continue;       /* head already hit or missed: only the body is left */
            } else if (o->state) {
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
            draw_circle_head(ren, &sk, c, px, py, d, s, o->num, a);
        }
        /* approach circles last, above every circle (earlier objects on top, like the circles themselves) */
        for (int i = hi - 1; i >= first && i >= 0; i--) {
            Obj *o = &bm.objs[i];
            if (o->state) continue;                       /* head already hit or missed */
            float prog = (t - (o->time - preempt)) / preempt;
            if (prog >= 1) continue;
            float fade = fminf(1, (t - (o->time - preempt)) / fminf(400, preempt));
            draw_approach(ren, &sk, sk.combo[o->color % sk.n_combo], ox + o->x * s, oy + o->y * s, rad * 2 * s * 1.05f, prog, (int)(fade * 255));
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
            draw_num(ren, sk.score, buf, W - 180 * us, 28 * us, us, 1, 255, 0);
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
            float ex = draw_num(ren, sk.score, buf, bx, by, us * pop, 0, 255, 0);
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
        if (first >= bm.n && sl_first >= first && t > bm.last_end + 2500) running = 0;
    }

    int tot = hits[0] + hits[1] + hits[2] + hits[3];
    double acc = tot ? (300.0 * hits[0] + 100 * hits[1] + 50 * hits[2]) / (300.0 * tot) * 100 : 100;
    snprintf(st->status, sizeof st->status, "last: %d pts | combo %d | %d/%d/%d/%dx | %.2f%%",
             (int)score, maxcombo, hits[0], hits[1], hits[2], hits[3], acc);
    printf("%s\n", st->status);

#ifdef AIMLESS_DEBUG
    if (getenv("AIMLESS_CLK")) fprintf(stderr, "clock offset vs audio: %.1f ms\n", clk);
#endif
    Mix_HaltMusic();
    sfx_free(); skin_free(&sk); beatmap_free(&bm);
    if (bg) SDL_DestroyTexture(bg);
    text_free(&t_fps); text_free(&t_acc); text_free(&t_lag);
    if (hud_font) TTF_CloseFont(hud_font);
    if (num_font) TTF_CloseFont(num_font);
    if (mus) Mix_FreeMusic(mus);
    return quit;
}
