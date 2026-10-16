/* Song select + settings, drawn in an iOS ("Cupertino") dark style with the helpers from gfx.c. */
#include "ui.h"
#include "beatmap.h"
#include "gfx.h"
#include "paths.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- immediate-mode input ---------- */
typedef struct { int mx, my, down, pressed, wheel, active, idc; float clip0, clip1; } In;
static In in;

static int over(float x, float y, float w, float h) {
    return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h && in.my >= in.clip0 && in.my < in.clip1;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static const SDL_Color C_OFF = {57, 57, 61, 255}, C_SEG = {58, 58, 60, 255}, C_SEGSEL = {99, 99, 102, 255};

/* ---------- widgets (rows live inside a C_CELL card) ---------- */
#define RH 48   /* switch / stepper row */
#define SH 70   /* slider row */
#define GH 80   /* segmented row */

static void sep(float x, float y, float w) { gfx_line_h(x + 16, y, w - 16, C_SEP); }

static int w_switch(float x, float y, float w, const char *label, int *v) {
    int ch = 0;
    gfx_text(label, x + 16, y + 14, 16, 0, C_LABEL, w - 100, A_LEFT);
    float sx = x + w - 16 - 51, sy = y + 8.5f;
    gfx_rrect(sx, sy, 51, 31, 15.5f, *v ? C_GREEN : C_OFF);
    gfx_circle(sx + (*v ? 51 - 15.5f : 15.5f), sy + 15.5f, 13.5f, C_WHITE);
    if (in.pressed && over(x, y, w, RH)) { *v = !*v; ch = 1; }
    return ch;
}

static int w_slider(float x, float y, float w, const char *label, float *v, float lo, float hi, float step, const char *fmt) {
    int id = ++in.idc, ch = 0;
    char b[48]; snprintf(b, sizeof b, fmt, *v);
    gfx_text(label, x + 16, y + 10, 16, 0, C_LABEL, w * 0.6f, A_LEFT);
    gfx_text(b, x + w - 16, y + 10, 16, 0, C_LABEL2, 0, A_RIGHT);
    float tx = x + 16, tw = w - 32, ty = y + 50;
    if (in.pressed && over(tx - 14, ty - 18, tw + 28, 36)) in.active = id;
    if (in.active == id && in.down) {
        float nv = lo + clampf((in.mx - tx) / tw, 0, 1) * (hi - lo);
        nv = roundf(nv / step) * step;
        if (nv != *v) { *v = nv; ch = 1; }
    }
    float f = clampf((*v - lo) / (hi - lo), 0, 1);
    gfx_rrect(tx, ty - 2, tw, 4, 2, C_SEG);
    gfx_rrect(tx, ty - 2, tw * f, 4, 2, C_ACCENT);
    gfx_circle(tx + tw * f, ty + 1.5f, 15, gfx_alpha((SDL_Color){0, 0, 0, 255}, 70));
    gfx_circle(tx + tw * f, ty, 14, C_WHITE);
    return ch;
}

static int w_segment(float x, float y, float w, const char *label, const char **opts, int n, int *sel) {
    int ch = 0;
    gfx_text(label, x + 16, y + 10, 16, 0, C_LABEL, w - 32, A_LEFT);
    float sx = x + 16, sw = w - 32, sy = y + 38, sh = 32, seg = sw / n;
    gfx_rrect(sx, sy, sw, sh, 9, C_SEG);
    gfx_rrect(sx + *sel * seg + 2, sy + 2, seg - 4, sh - 4, 7, C_SEGSEL);
    for (int i = 0; i < n; i++) {
        gfx_text(opts[i], sx + seg * (i + 0.5f), sy + 7, 14, i == *sel, C_LABEL, seg - 8, A_CENTER);
        if (in.pressed && over(sx + i * seg, sy, seg, sh) && *sel != i) { *sel = i; ch = 1; }
    }
    return ch;
}

static int w_stepper(float x, float y, float w, const char *label, const char *value, int *idx, int lo, int hi) {
    int ch = 0;
    gfx_text(label, x + 16, y + 14, 16, 0, C_LABEL, w * 0.3f, A_LEFT);
    float rx = x + w - 16;
    SDL_Color a = *idx > lo ? C_ACCENT : C_SEP, b = *idx < hi ? C_ACCENT : C_SEP;
    gfx_text("\xe2\x80\xba", rx - 6, y + 11, 22, 1, b, 0, A_RIGHT);
    gfx_text(value, rx - 30, y + 14, 16, 0, C_LABEL2, w * 0.5f, A_RIGHT);
    float vw = gfx_text_w(value, 16, 0);
    if (vw > w * 0.5f) vw = w * 0.5f;
    gfx_text("\xe2\x80\xb9", rx - 30 - vw - 12, y + 11, 22, 1, a, 0, A_RIGHT);
    if (in.pressed && over(rx - 44 - vw, y, vw + 52, RH)) {
        if (in.mx > rx - 30 - vw / 2 && *idx < hi) { (*idx)++; ch = 1; }
        else if (in.mx <= rx - 30 - vw / 2 && *idx > lo) { (*idx)--; ch = 1; }
    }
    return ch;
}

static void header(const char *t, float x, float y) { gfx_text(t, x + 16, y, 13, 0, C_LABEL2, 0, A_LEFT); }
static void card(float x, float y, float w, float h) { gfx_rrect(x, y, w, h, 12, C_CELL); }

/* ---------- state ---------- */
typedef struct {
    SDL_Window *win; SDL_Renderer *ren; Settings *st;
    List songs, skins;
    int *vis, nvis;
    char q[128];
    int sel, follow; float scroll;        /* follow: scroll to the selection once (keyboard moves) */
    float list_x1;
    int skin_sel;                         /* -1 = built-in */
    char cur[1024];                       /* path of the map the detail panel shows */
    SDL_Texture *cover; int cw, ch;
    char title[128], artist[128], version[128];
    float cs, ar, od; int nobj, len_ms;
    int screen;                           /* 0 select, 1 settings */
    float sscroll, scontent;
    float soft_r;
    int result, running;
} Ctx;

static void rescan(Ctx *c) {
    list_free(&c->songs); list_free(&c->skins);
    scan_songs(&c->songs, c->st->songs_extra); scan_skins(&c->skins, c->st->skins_extra);
    free(c->vis); c->vis = malloc((c->songs.n + 1) * sizeof(int)); c->nvis = 0;
    for (int i = 0; i < c->songs.n; i++) if (!c->q[0] || strcasestr(c->songs.e[i].label, c->q)) c->vis[c->nvis++] = i;
    if (c->sel >= c->nvis) c->sel = c->nvis - 1;
    if (c->sel < 0) c->sel = 0;
    c->cur[0] = 0;
    c->skin_sel = -1;
    for (int i = 0; i < c->skins.n; i++) if (!strcmp(c->skins.e[i].label, c->st->skin)) c->skin_sel = i;
}

static void load_sel(Ctx *c) {
    if (!c->nvis) { if (c->cover) SDL_DestroyTexture(c->cover); c->cover = NULL; c->cur[0] = 0; c->title[0] = 0; return; }
    const char *p = c->songs.e[c->vis[c->sel]].path;
    if (!strcmp(p, c->cur)) return;
    snprintf(c->cur, sizeof c->cur, "%s", p);
    if (c->cover) SDL_DestroyTexture(c->cover);
    c->cover = NULL;
    Beatmap bm;
    if (beatmap_load(&bm, p, 0)) { c->title[0] = 0; beatmap_free(&bm); return; }
    snprintf(c->title, sizeof c->title, "%s", bm.title); snprintf(c->artist, sizeof c->artist, "%s", bm.artist);
    snprintf(c->version, sizeof c->version, "%s", bm.version);
    c->cs = bm.cs; c->ar = bm.ar; c->od = bm.od; c->nobj = bm.n; c->len_ms = bm.n ? bm.objs[bm.n - 1].time : 0;
    if (bm.bg[0]) { char ip[1100]; snprintf(ip, sizeof ip, "%s/%s", bm.dir, bm.bg); c->cover = gfx_load_image(ip, &c->cw, &c->ch); }
    beatmap_free(&bm);
}

static void commit_play(Ctx *c) { if (c->nvis) { c->running = 0; c->result = 1; } }

/* ---------- select screen ---------- */
static void draw_select(Ctx *c, int W, int H) {
    Settings *st = c->st;
    float lw = clampf(W * 0.46f, 340, 560), lx = 24, rx = lx + lw + 20, rw = W - 24 - rx;
    c->list_x1 = lx + lw;
    gfx_text("aimless", lx, 18, 34, 1, C_LABEL, 0, A_LEFT);
    char b[160]; snprintf(b, sizeof b, "%d of %d maps", c->nvis, c->songs.n);
    gfx_text(b, lx + gfx_text_w("aimless", 34, 1) + 14, 32, 15, 0, C_LABEL2, 0, A_LEFT);

    float bx = W - 24 - 104, by = 24; /* settings pill */
    gfx_rrect(bx, by, 104, 34, 17, gfx_alpha(C_ACCENT, 50));
    gfx_text("Settings", bx + 52, by + 7, 16, 1, C_ACCENT, 0, A_CENTER);
    if (in.pressed && over(bx, by, 104, 34)) c->screen = 1;

    float sy = 74; /* search field */
    gfx_rrect(lx, sy, lw, 38, 11, C_CELL);
    if (c->q[0]) gfx_text(c->q, lx + 14, sy + 9, 16, 0, C_LABEL, lw - 28, A_LEFT);
    else gfx_text("Search (type to filter)", lx + 14, sy + 9, 16, 0, C_LABEL2, lw - 28, A_LEFT);

    /* list card */
    float ly = 124, lh = H - ly - 24; const float ROW = 60;
    card(lx, ly, lw, lh);
    SDL_Rect clip = { (int)lx, (int)ly, (int)lw, (int)lh };
    SDL_RenderSetClipRect(c->ren, &clip);
    float c0 = in.clip0, c1 = in.clip1; in.clip0 = ly; in.clip1 = ly + lh;
    float total = c->nvis * ROW + 12;
    if (in.wheel) c->scroll -= in.wheel * 54;
    if (c->follow) {
        float selY = 6 + c->sel * ROW;
        if (c->scroll > selY - 6) c->scroll = selY - 6;
        if (c->scroll + lh < selY + ROW + 6) c->scroll = selY + ROW + 6 - lh;
        c->follow = 0;
    }
    c->scroll = clampf(c->scroll, 0, fmaxf(0, total - lh));
    for (int i = 0; i < c->nvis; i++) {
        float y = ly + 6 + i * ROW - c->scroll;
        if (y + ROW < ly || y > ly + lh) continue;
        Entry *e = &c->songs.e[c->vis[i]];
        int sel = i == c->sel;
        if (sel) gfx_rrect(lx + 6, y + 2, lw - 12, ROW - 4, 10, C_ACCENT);
        char main_[400], diff[128] = ""; /* label is "Artist - Title [Diff]": 2-line cell */
        snprintf(main_, sizeof main_, "%s", e->label);
        char *br = strrchr(main_, '[');
        if (br && br > main_) { snprintf(diff, sizeof diff, "%s", br + 1); br[-1] = 0; size_t n = strlen(diff); if (n && diff[n-1] == ']') diff[n-1] = 0; }
        gfx_text(main_, lx + 20, y + 9, 15, sel, C_LABEL, lw - 40, A_LEFT);
        gfx_text(diff, lx + 20, y + 32, 13, 0, sel ? gfx_alpha(C_WHITE, 200) : C_LABEL2, lw - 40, A_LEFT);
        if (i < c->nvis - 1 && !sel && i + 1 != c->sel) sep(lx + 4, y + ROW - 1, lw - 8);
        if (in.pressed && over(lx, y, lw, ROW)) c->sel = i;
    }
    if (!c->nvis) gfx_text(c->songs.n ? "No match" : "No maps yet: drop a .osz here", lx + lw / 2, ly + 24, 16, 0, C_LABEL2, lw - 40, A_CENTER);
    SDL_RenderSetClipRect(c->ren, NULL);
    in.clip0 = c0; in.clip1 = c1;

    /* detail card */
    float dy = 74, dh = H - dy - 24;
    card(rx, dy, rw, dh);
    load_sel(c);
    float ix = rx + 16, iw = rw - 32, ih = fminf(iw * 9 / 16, dh * 0.38f), y = dy + 16;
    if (c->cover) {
        SDL_Rect cr = { (int)ix, (int)y, (int)iw, (int)ih };
        SDL_RenderSetClipRect(c->ren, &cr);
        SDL_SetTextureColorMod(c->cover, 255, 255, 255);
        gfx_cover(c->cover, c->cw, c->ch, ix, y, iw, ih);
        SDL_RenderSetClipRect(c->ren, NULL);
        gfx_round_mask(ix, y, iw, ih, 12, C_CELL);
    } else {
        gfx_rrect(ix, y, iw, ih, 12, C_CELL2);
        gfx_text("No cover", ix + iw / 2, y + ih / 2 - 9, 16, 0, C_LABEL2, 0, A_CENTER);
    }
    y += ih + 14;
    if (c->nvis && c->title[0]) {
        gfx_text(c->title, ix, y, 22, 1, C_LABEL, iw, A_LEFT); y += 30;
        gfx_text(c->artist, ix, y, 16, 0, C_LABEL2, iw, A_LEFT); y += 26;
        float vw = gfx_text_w(c->version, 13, 1) + 20;
        gfx_rrect(ix, y, vw, 24, 12, gfx_alpha(C_ACCENT, 50));
        gfx_text(c->version, ix + vw / 2, y + 4, 13, 1, C_ACCENT, vw, A_CENTER); y += 34;
        snprintf(b, sizeof b, "CS %.1f   AR %.1f   OD %.1f", c->cs, c->ar, c->od);
        gfx_text(b, ix, y, 14, 0, C_LABEL, iw, A_LEFT); y += 22;
        snprintf(b, sizeof b, "%d objects   %d:%02d", c->nobj, c->len_ms / 60000, c->len_ms / 1000 % 60);
        gfx_text(b, ix, y, 14, 0, C_LABEL2, iw, A_LEFT); y += 26;
    }

    /* quick options (skin + aim), then Play pinned to the bottom */
    float playH = 50, by2 = dy + dh - 16 - playH;
    float qy = by2 - 14 - (RH + GH) - (st->status[0] ? 24 : 0);
    if (qy > y) {
        gfx_rrect(ix - 4, qy, iw + 8, RH + GH, 12, C_CELL2);
        char sk[160]; snprintf(sk, sizeof sk, "%s", c->skin_sel < 0 ? "Built-in" : c->skins.e[c->skin_sel].label);
        int si = c->skin_sel;
        if (w_stepper(ix - 4, qy, iw + 8, "Skin", sk, &si, -1, c->skins.n - 1)) {
            c->skin_sel = si; snprintf(st->skin, sizeof st->skin, "%s", si < 0 ? "" : c->skins.e[si].label);
        }
        sep(ix - 4, qy + RH, iw + 8);
        static const char *aim[] = { "Off", "Soft", "Anywhere" };
        int mode = st->assist == 1 ? 0 : st->assist == 0 ? 2 : 1;
        if (w_segment(ix - 4, qy + RH, iw + 8, "Silent aim", aim, 3, &mode))
            st->assist = mode == 0 ? 1 : mode == 2 ? 0 : c->soft_r;
    }
    if (st->status[0]) gfx_text(st->status, ix, by2 - 30, 13, 0, C_LABEL2, iw, A_LEFT);
    gfx_rrect(ix, by2, iw, playH, 14, c->nvis ? C_ACCENT : C_OFF);
    gfx_text("Play", ix + iw / 2, by2 + 14, 18, 1, C_WHITE, 0, A_CENTER);
    if (in.pressed && c->nvis && over(ix, by2, iw, playH)) commit_play(c);
}

/* ---------- settings screen ---------- */
static void fi(float x, float *y, float w, const char *label, int *v, float lo, float hi, float step, const char *fmt) {
    float f = (float)*v;
    if (w_slider(x, *y, w, label, &f, lo, hi, step, fmt)) *v = (int)lroundf(f);
    *y += SH;
}

static void draw_settings(Ctx *c, int W, int H) {
    Settings *st = c->st;
    float cw = fminf(W - 40, 640), x = (W - cw) / 2;

    float vy0 = 56;
    SDL_Rect clip = { 0, (int)vy0, W, H - (int)vy0 };
    SDL_RenderSetClipRect(c->ren, &clip);
    float c0 = in.clip0, c1 = in.clip1; in.clip0 = vy0; in.clip1 = H;
    if (in.wheel) c->sscroll -= in.wheel * 60;
    c->sscroll = clampf(c->sscroll, 0, fmaxf(0, c->scontent - (H - vy0)));
    float y = vy0 + 16 - c->sscroll, top = y, yy;

    header("AUDIO", x, y); y += 24;
    card(x, y, cw, SH * 3); yy = y;
    fi(x, &yy, cw, "Master volume", &st->master_vol, 0, 100, 1, "%.0f%%"); sep(x, yy, cw);
    fi(x, &yy, cw, "Music volume", &st->music_vol, 0, 100, 1, "%.0f%%"); sep(x, yy, cw);
    fi(x, &yy, cw, "Effect volume (hitsounds)", &st->sfx_vol, 0, 100, 1, "%.0f%%");
    y += SH * 3 + 24;

    int soft = st->assist != 1 && st->assist != 0;
    float gh = SH * 3 + RH * 3 + GH * 2 + (soft ? SH : 0);
    header("GAMEPLAY", x, y); y += 24;
    card(x, y, cw, gh); yy = y;
    fi(x, &yy, cw, "Background dim", &st->bg_dim, 0, 100, 1, "%.0f%%"); sep(x, yy, cw);
    fi(x, &yy, cw, "Background blur", &st->bg_blur, 0, 100, 20, "%.0f%%"); sep(x, yy, cw);
    w_switch(x, yy, cw, "Show beatmap background", &st->show_bg); yy += RH; sep(x, yy, cw);
    { float f = st->cursor_size; if (w_slider(x, yy, cw, "Cursor size", &f, 0.3f, 3.0f, 0.05f, "%.2fx")) st->cursor_size = f; yy += SH; sep(x, yy, cw); }
    w_switch(x, yy, cw, "Cursor trail", &st->cursor_trail); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Hit lighting", &st->hit_lighting); yy += RH; sep(x, yy, cw);
    { static const char *bo[] = { "None", "Corners", "Full" };
      w_segment(x, yy, cw, "Playfield border", bo, 3, &st->border); yy += GH; sep(x, yy, cw); }
    { static const char *aim[] = { "Off", "Soft", "Anywhere" };
      int mode = st->assist == 1 ? 0 : st->assist == 0 ? 2 : 1;
      if (w_segment(x, yy, cw, "Silent aim", aim, 3, &mode)) st->assist = mode == 0 ? 1 : mode == 2 ? 0 : c->soft_r;
      yy += GH; }
    if (soft) {
        sep(x, yy, cw);
        float f = st->assist;
        if (w_slider(x, yy, cw, "Soft aim radius (circle radii)", &f, 1.2f, 4.0f, 0.1f, "%.1fx")) { st->assist = f; c->soft_r = f; }
        yy += SH;
    }
    sep(x, yy, cw);
    fi(x, &yy, cw, "Input delay compensation", &st->input_delay, -100, 100, 1, "%+.0f ms");
    y += gh;
    gfx_text("Positive values judge your hits earlier, to compensate for input/display lag.", x + 16, y + 6, 12, 0, C_LABEL2, cw - 32, A_LEFT);
    y += 36;

    header("INTERFACE", x, y); y += 24;
    card(x, y, cw, RH * 6); yy = y;
    w_switch(x, yy, cw, "Show score", &st->show_score); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Show accuracy", &st->show_acc); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Show combo", &st->show_combo); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Show hit error meter", &st->show_error_bar); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Show FPS counter", &st->show_fps); yy += RH; sep(x, yy, cw);
    w_switch(x, yy, cw, "Show input delay", &st->show_input_delay);
    y += RH * 6 + 24;

    header("DISPLAY", x, y); y += 24;
    card(x, y, cw, GH + RH); yy = y;
    { static const char *fl[] = { "VSync", "60", "120", "240", "480", "Unlimited" };
      static const int lim[] = { 0, 60, 120, 240, 480, 0 };
      int i = st->vsync ? 0 : st->fps_limit == 60 ? 1 : st->fps_limit == 120 ? 2 : st->fps_limit == 240 ? 3 : st->fps_limit == 480 ? 4 : 5;
      if (w_segment(x, yy, cw, "Frame limiter", fl, 6, &i)) { st->vsync = i == 0; st->fps_limit = lim[i]; }
      yy += GH; sep(x, yy, cw); }
    if (w_switch(x, yy, cw, "Fullscreen", &st->fullscreen))
        SDL_SetWindowFullscreen(c->win, st->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    y += GH + RH + 16;

    gfx_text("Saved automatically to ~/.local/share/aimless/settings.ini", x + 16, y, 12, 0, C_LABEL2, cw - 32, A_LEFT);
    y += 40;
    c->scontent = y - top + 32;
    SDL_RenderSetClipRect(c->ren, NULL);
    in.clip0 = c0; in.clip1 = c1;

    /* nav bar on top of the scrolled content */
    gfx_rrect(0, 0, W, 56, 0, C_BG);
    gfx_text("Settings", W / 2.0f, 16, 18, 1, C_LABEL, 0, A_CENTER);
    gfx_text("\xe2\x80\xb9 Songs", x, 16, 18, 0, C_ACCENT, 0, A_LEFT);
    if (in.pressed && over(x - 10, 6, 110, 44)) c->screen = 0;
}

/* ---------- main loop ---------- */
static void utf8_backspace(char *s) {
    size_t n = strlen(s);
    if (!n) return;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
}

int menu_run(SDL_Window *win, SDL_Renderer *ren, Settings *st,
             char *map_out, size_t map_n, char *skin_out, size_t skin_n) {
    gfx_init(ren);
    Ctx c; memset(&c, 0, sizeof c);
    c.win = win; c.ren = ren; c.st = st; c.running = 1;
    c.soft_r = st->assist > 1 ? st->assist : 2.5f;
    rescan(&c);
    SDL_RenderSetVSync(ren, 1);
    SDL_StartTextInput();
#ifdef AIMLESS_DEBUG
    if (getenv("AIMLESS_SCREEN") && !strcmp(getenv("AIMLESS_SCREEN"), "settings")) c.screen = 1;
    int frames = 0;
#endif

    while (c.running) {
        in.pressed = 0; in.wheel = 0; in.idc = 0;
        SDL_Event e;
        int have = SDL_WaitEventTimeout(&e, 16);
        while (have) {
            if (e.type == SDL_QUIT) { c.running = 0; c.result = 0; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                in.pressed = 1; in.down = 1;
                if (c.screen == 0 && e.button.clicks >= 2 && e.button.x < c.list_x1 && e.button.y > 124) commit_play(&c);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) in.down = 0;
            else if (e.type == SDL_MOUSEWHEEL) in.wheel += e.wheel.y;
            else if (e.type == SDL_TEXTINPUT && c.screen == 0) {
                if (strlen(c.q) + strlen(e.text.text) < sizeof c.q - 1) { strcat(c.q, e.text.text); rescan(&c); c.sel = 0; c.scroll = 0; }
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F2) c.screen = !c.screen;
                else if (c.screen == 1) { if (k == SDLK_ESCAPE) c.screen = 0; }
                else switch (k) {
                case SDLK_ESCAPE: if (c.q[0]) { c.q[0] = 0; rescan(&c); } else { c.running = 0; c.result = 0; } break;
                case SDLK_BACKSPACE: if (c.q[0]) { utf8_backspace(c.q); rescan(&c); c.sel = 0; c.scroll = 0; } break;
                case SDLK_DOWN: if (c.sel + 1 < c.nvis) c.sel++; c.follow = 1; break;
                case SDLK_UP: if (c.sel > 0) c.sel--; c.follow = 1; break;
                case SDLK_PAGEDOWN: c.sel = c.sel + 8 < c.nvis ? c.sel + 8 : c.nvis - 1; if (c.sel < 0) c.sel = 0; c.follow = 1; break;
                case SDLK_PAGEUP: c.sel = c.sel > 8 ? c.sel - 8 : 0; c.follow = 1; break;
                case SDLK_RIGHT: if (c.skin_sel + 1 < c.skins.n) { c.skin_sel++; snprintf(st->skin, sizeof st->skin, "%s", c.skins.e[c.skin_sel].label); } break;
                case SDLK_LEFT: if (c.skin_sel > -1) { c.skin_sel--; snprintf(st->skin, sizeof st->skin, "%s", c.skin_sel < 0 ? "" : c.skins.e[c.skin_sel].label); } break;
                case SDLK_F5: rescan(&c); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: commit_play(&c); break;
                }
            } else if (e.type == SDL_DROPFILE) {
                import_path(e.drop.file, st->status, sizeof st->status);
                SDL_free(e.drop.file);
                rescan(&c);
            }
            have = SDL_PollEvent(&e);
        }
        if (!c.running) break;
        SDL_GetMouseState(&in.mx, &in.my);
        if (!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK)) { in.down = 0; in.active = 0; }
        in.clip0 = 0; in.clip1 = 1e9f;

        int W, H; SDL_GetWindowSize(win, &W, &H);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
        if (c.screen == 0) {
            if (c.cover) { /* dim backdrop from the cover */
                SDL_SetTextureColorMod(c.cover, 40, 40, 44);
                gfx_cover(c.cover, c.cw, c.ch, 0, 0, W, H);
            }
            draw_select(&c, W, H);
        } else draw_settings(&c, W, H);
        SDL_RenderPresent(ren);

#ifdef AIMLESS_DEBUG
        if (getenv("AIMLESS_SHOT") && ++frames == 6) { /* debug: dump a frame and quit */
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            IMG_SavePNG(s, getenv("AIMLESS_SHOT")); SDL_FreeSurface(s);
            c.running = 0; c.result = 0;
        }
#endif
    }

    SDL_StopTextInput();
    if (c.result && c.nvis) {
        snprintf(map_out, map_n, "%s", c.songs.e[c.vis[c.sel]].path);
        snprintf(skin_out, skin_n, "%s", c.skin_sel < 0 ? "" : c.skins.e[c.skin_sel].path);
    }
    settings_save(st);
    if (c.cover) SDL_DestroyTexture(c.cover);
    list_free(&c.songs); list_free(&c.skins); free(c.vis);
    gfx_quit();
    return c.result;
}
