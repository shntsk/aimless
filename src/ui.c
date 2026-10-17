/* Song select + settings, drawn in an iOS ("Cupertino") dark style with the helpers from gfx.c. */
#include "ui.h"
#include "beatmap.h"
#include "gfx.h"
#include "library.h"
#include "paths.h"
#include <SDL_image.h>
#include <SDL_mixer.h>
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
enum { ROW_HEADER, ROW_SET, ROW_DIFF };
typedef struct { int type, a, b; float y, h; } Row;
#define HDR_H 34
#define SET_H 92
#define DIFF_H 56
#define NTHUMB 96
typedef struct { char key[1100]; SDL_Texture *t; Uint32 last; } Thumb;

typedef struct {
    SDL_Window *win; SDL_Renderer *ren; Settings *st;
    Library lib; List skins;
    Query query; char qtext[128];
    Row *rows; int nrows, rowcap; float total_h; int nvis_sets, nvis_diffs;
    int sel_set, sel_diff; float sel_stars;      /* sel_set indexes lib.s, sel_diff indexes that set's d[] */
    char shown_dir[1024];
    float scroll, target; int center, animating, dbl;
    Thumb th[NTHUMB]; int th_loads;
    SDL_Texture *cover; int cw, ch;
    Mix_Music *pm; char pm_key[1100]; Uint32 pm_due; int pm_pending;
    int skin_sel;                               /* -1 = built-in */
    int screen;                                 /* 0 select, 1 settings */
    float sscroll, scontent, soft_r;
    int result, running;
} Ctx;

static const char *SORT_NAMES[] = { "Artist", "Title", "Date added", "Difficulty", "Length", "BPM" };
static const char *GROUP_NAMES[] = { "None", "Artist", "Title", "Difficulty" };

/* lazer's difficulty colour scale */
static SDL_Color star_color(float s) {
    static const struct { float s; Uint8 r, g, b; } T[] = {
        {0.1f,170,170,170}, {1.25f,66,144,251}, {2.0f,79,192,255}, {2.5f,79,255,213}, {3.3f,124,255,79}, {4.2f,246,240,92},
        {4.9f,255,128,104}, {5.8f,255,78,111}, {6.7f,198,69,184}, {7.7f,101,99,222}, {9.0f,24,21,142} };
    const int n = sizeof T / sizeof *T;
    if (s <= T[0].s) return (SDL_Color){ T[0].r, T[0].g, T[0].b, 255 };
    for (int i = 1; i < n; i++) if (s <= T[i].s) {
        float k = (s - T[i-1].s) / (T[i].s - T[i-1].s);
        return (SDL_Color){ (Uint8)(T[i-1].r + (T[i].r - T[i-1].r) * k), (Uint8)(T[i-1].g + (T[i].g - T[i-1].g) * k),
                            (Uint8)(T[i-1].b + (T[i].b - T[i-1].b) * k), 255 };
    }
    return (SDL_Color){ T[n-1].r, T[n-1].g, T[n-1].b, 255 };
}

static MapSet *cur_set(Ctx *c) { return c->sel_set >= 0 && c->sel_set < c->lib.n ? &c->lib.s[c->sel_set] : NULL; }
static Diff *cur_diff(Ctx *c) {
    MapSet *s = cur_set(c);
    return s && c->sel_diff >= 0 && c->sel_diff < s->nd ? &s->d[c->sel_diff] : NULL;
}

static void row_push(Ctx *c, int type, int a, int b, float h) {
    if (c->nrows == c->rowcap) { c->rowcap = c->rowcap ? c->rowcap * 2 : 512; c->rows = realloc(c->rows, c->rowcap * sizeof(Row)); }
    Row *r = &c->rows[c->nrows++];
    r->type = type; r->a = a; r->b = b; r->h = h; r->y = c->total_h; c->total_h += h;
}

static void rebuild(Ctx *c) {
    c->nrows = 0; c->total_h = 0; c->nvis_sets = c->nvis_diffs = 0;
    int last = -1, group = c->st->group_mode;
    for (int i = 0; i < c->lib.n; i++) {
        MapSet *s = &c->lib.s[i];
        if (!s->nvd) continue;
        c->nvis_sets++; c->nvis_diffs += s->nvd;
        if (group) { int k = lib_group_key(s, group); if (k != last) { row_push(c, ROW_HEADER, i, 0, HDR_H); last = k; } }
        row_push(c, ROW_SET, i, 0, SET_H);
        if (i == c->sel_set) for (int j = 0; j < s->nvd; j++) row_push(c, ROW_DIFF, i, s->vd[j], DIFF_H);
    }
    c->total_h += 16;
}

static int nearest_diff(MapSet *s, float stars) {
    int best = s->vd[0]; float bd = 1e9f;
    for (int j = 0; j < s->nvd; j++) { float d = fabsf(stars - s->d[s->vd[j]].mi.stars); if (d < bd) { bd = d; best = s->vd[j]; } }
    return best;
}

static void fix_selection(Ctx *c) {
    int ok = c->sel_set >= 0 && c->sel_set < c->lib.n && c->lib.s[c->sel_set].nvd > 0;
    if (!ok) { c->sel_set = -1; for (int i = 0; i < c->lib.n; i++) if (c->lib.s[i].nvd) { c->sel_set = i; break; } }
    if (c->sel_set < 0) { c->sel_diff = -1; return; }
    MapSet *s = &c->lib.s[c->sel_set];
    int in_view = 0;
    for (int j = 0; j < s->nvd; j++) if (s->vd[j] == c->sel_diff) in_view = 1;
    if (!in_view) c->sel_diff = nearest_diff(s, c->sel_stars);
    c->sel_stars = s->d[c->sel_diff].mi.stars;
}

static void reload_cover(Ctx *c) {
    if (c->cover) SDL_DestroyTexture(c->cover);
    c->cover = NULL;
    MapSet *s = cur_set(c);
    if (s && s->bg[0]) c->cover = gfx_load_image(s->bg, &c->cw, &c->ch);
}

/* call after any selection / filter change */
static void changed(Ctx *c) {
    rebuild(c); c->center = 1;
    MapSet *s = cur_set(c);
    if (s && strcmp(s->dir, c->shown_dir)) {
        snprintf(c->shown_dir, sizeof c->shown_dir, "%s", s->dir);
        reload_cover(c);
        c->pm_pending = 1; c->pm_due = SDL_GetTicks() + 280;
    }
}

static void select_set(Ctx *c, int i) { c->sel_set = i; c->sel_diff = -1; fix_selection(c); changed(c); }
static void select_diff(Ctx *c, int d) { c->sel_diff = d; fix_selection(c); changed(c); }

static void step_set(Ctx *c, int dir) {
    int i = c->sel_set;
    for (;;) { i += dir; if (i < 0 || i >= c->lib.n) return; if (c->lib.s[i].nvd) break; }
    select_set(c, i);
}

static void step_diff(Ctx *c, int dir) {
    MapSet *s = cur_set(c); if (!s) return;
    for (int j = 0; j < s->nvd; j++) if (s->vd[j] == c->sel_diff) {
        int n = j + dir; if (n >= 0 && n < s->nvd) select_diff(c, s->vd[n]);
        return;
    }
}

static void random_set(Ctx *c) {
    if (c->nvis_sets <= 0) return;
    static unsigned rs; if (!rs) rs = (unsigned)SDL_GetTicks() * 2654435761u + 12345;
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    int k = rs % c->nvis_sets;
    for (int i = 0; i < c->lib.n; i++) if (c->lib.s[i].nvd && k-- == 0) { select_set(c, i); return; }
}

static void refilter(Ctx *c) {
    query_parse(&c->query, c->qtext);
    lib_apply_query(&c->lib, &c->query);
    fix_selection(c); changed(c);
}

static void resort(Ctx *c) {
    char keep[1024]; MapSet *s = cur_set(c);
    snprintf(keep, sizeof keep, "%s", s ? s->dir : "");
    lib_sort(&c->lib, c->st->sort_mode, c->st->group_mode);
    lib_apply_query(&c->lib, &c->query);
    c->sel_set = -1;
    for (int i = 0; i < c->lib.n; i++) if (!strcmp(c->lib.s[i].dir, keep)) { c->sel_set = i; break; }
    fix_selection(c); c->shown_dir[0] = 0; changed(c);
}

static void scan_progress(int done, void *ud) {
    Ctx *c = ud; char b[96];
    SDL_PumpEvents();
    SDL_SetRenderDrawColor(c->ren, 0, 0, 0, 255); SDL_RenderClear(c->ren);
    int W, H; SDL_GetWindowSize(c->win, &W, &H);
    gfx_text("aimless", W / 2.0f, H / 2.0f - 50, 34, 1, C_LABEL, 0, A_CENTER);
    snprintf(b, sizeof b, "Loading beatmaps... %d", done);
    gfx_text(b, W / 2.0f, H / 2.0f, 16, 0, C_LABEL2, 0, A_CENTER);
    SDL_RenderPresent(c->ren);
}

static void rescan(Ctx *c) {
    char keep[600] = "";
    Diff *d = cur_diff(c); if (d) snprintf(keep, sizeof keep, "%s", d->path);
    lib_free(&c->lib); list_free(&c->skins);
    lib_scan(&c->lib, c->st->songs_extra, scan_progress, c);
    scan_skins(&c->skins, c->st->skins_extra);
    lib_sort(&c->lib, c->st->sort_mode, c->st->group_mode);
    query_parse(&c->query, c->qtext); lib_apply_query(&c->lib, &c->query);
    c->sel_set = -1; c->sel_diff = -1;
    const char *want = keep[0] ? keep : c->st->last_map;
    for (int i = 0; i < c->lib.n && c->sel_set < 0; i++)
        for (int j = 0; j < c->lib.s[i].nd; j++) if (!strcmp(c->lib.s[i].d[j].path, want)) { c->sel_set = i; c->sel_diff = j; c->sel_stars = c->lib.s[i].d[j].mi.stars; break; }
    fix_selection(c);
    c->shown_dir[0] = 0; changed(c);
    c->skin_sel = -1;
    for (int i = 0; i < c->skins.n; i++) if (!strcmp(c->skins.e[i].label, c->st->skin)) c->skin_sel = i;
}

static void commit_play(Ctx *c) {
    Diff *d = cur_diff(c);
    if (!d) return;
    snprintf(c->st->last_map, sizeof c->st->last_map, "%s", d->path);
    c->running = 0; c->result = 1;
}

/* ---------- cover thumbnails (decoded + downscaled once, max 1 per frame) ---------- */
static SDL_Texture *thumb(Ctx *c, const char *path) {
    if (!path[0]) return NULL;
    for (int i = 0; i < NTHUMB; i++)
        if (c->th[i].key[0] && !strcmp(c->th[i].key, path)) { c->th[i].last = SDL_GetTicks(); return c->th[i].t; }
    if (c->th_loads >= 1) return NULL;
    c->th_loads++;
    int slot = 0; Uint32 oldest = ~0u;
    for (int i = 0; i < NTHUMB; i++) {
        if (!c->th[i].key[0]) { slot = i; break; }
        if (c->th[i].last < oldest) { oldest = c->th[i].last; slot = i; }
    }
    Thumb *t = &c->th[slot];
    if (t->t) SDL_DestroyTexture(t->t);
    t->t = NULL; snprintf(t->key, sizeof t->key, "%s", path); t->last = SDL_GetTicks();
    SDL_Surface *raw = IMG_Load(path);
    if (raw) {
        SDL_Surface *src = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(raw);
        if (src) {
            float ar = 16.0f / 9; SDL_Rect sr;
            if ((float)src->w / src->h > ar) { sr.h = src->h; sr.w = (int)(src->h * ar); sr.x = (src->w - sr.w) / 2; sr.y = 0; }
            else { sr.w = src->w; sr.h = (int)(src->w / ar); sr.x = 0; sr.y = (src->h - sr.h) / 2; }
            SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 192, 108, 32, SDL_PIXELFORMAT_ARGB8888);
            if (dst) {
                SDL_SoftStretchLinear(src, &sr, dst, NULL);
                t->t = SDL_CreateTextureFromSurface(c->ren, dst);
                if (t->t) SDL_SetTextureScaleMode(t->t, SDL_ScaleModeLinear);
                SDL_FreeSurface(dst);
            }
            SDL_FreeSurface(src);
        }
    }
    return t->t;
}

/* ---------- song preview (like lazer: plays from the map's PreviewTime) ---------- */
static void preview_update(Ctx *c) {
    Mix_VolumeMusic((int)(MIX_MAX_VOLUME * c->st->music_vol / 100.0 * c->st->master_vol / 100.0));
    if (!c->pm_pending || SDL_GetTicks() < c->pm_due) return;
    c->pm_pending = 0;
    MapSet *s = cur_set(c);
    if (!s || !s->audio[0]) { Mix_HaltMusic(); return; }
    if (!strcmp(c->pm_key, s->audio) && Mix_PlayingMusic()) return;
    Mix_HaltMusic();
    if (c->pm) { Mix_FreeMusic(c->pm); c->pm = NULL; }
    c->pm = Mix_LoadMUS(s->audio);
    if (!c->pm) { c->pm_key[0] = 0; return; }
    snprintf(c->pm_key, sizeof c->pm_key, "%s", s->audio);
    double pos = s->preview_ms > 0 ? s->preview_ms / 1000.0 : s->len * 0.4 / 1000.0;
    if (Mix_FadeInMusicPos(c->pm, -1, 400, pos) < 0) Mix_FadeInMusic(c->pm, -1, 400);
}

/* ---------- select screen ---------- */
static void pill(float x, float y, float w, const char *label, const char *value, int *click) {
    gfx_rrect(x, y, w, 38, 11, C_CELL);
    float lw = gfx_text(label, x + 12, y + 10, 14, 0, C_LABEL2, 0, A_LEFT);
    gfx_text(value, x + 12 + lw + 6, y + 10, 14, 1, C_ACCENT, w - lw - 30, A_LEFT);
    *click = in.pressed && over(x, y, w, 38);
}

static void draw_select(Ctx *c, int W, int H) {
    Settings *st = c->st;
    float lw = floorf(clampf(W * 0.36f, 330, 470)), lx = 24, rx = lx + lw + 20, rw = W - 24 - rx;
    char b[200];

    gfx_text("aimless", lx, 18, 34, 1, C_LABEL, 0, A_LEFT);
    snprintf(b, sizeof b, "%d sets, %d difficulties", c->nvis_sets, c->nvis_diffs);
    gfx_text(b, lx + gfx_text_w("aimless", 34, 1) + 14, 32, 15, 0, C_LABEL2, 0, A_LEFT);
    float bx = W - 24 - 104, by = 24; /* settings pill */
    gfx_rrect(bx, by, 104, 34, 17, gfx_alpha(C_ACCENT, 50));
    gfx_text("Settings", bx + 52, by + 7, 16, 1, C_ACCENT, 0, A_CENTER);
    if (in.pressed && over(bx, by, 104, 34)) c->screen = 1;

    /* search + sort + group */
    float pw = 168, sy = 74, sw = rw - 2 * pw - 16;
    if (sw < 120) sw = 120;
    gfx_rrect(rx, sy, sw, 38, 11, C_CELL);
    if (c->qtext[0]) gfx_text(c->qtext, rx + 14, sy + 9, 16, 0, C_LABEL, sw - 28, A_LEFT);
    else gfx_text("Search, or filter: stars>5 bpm>180", rx + 14, sy + 10, 14, 0, C_LABEL2, sw - 28, A_LEFT);
    int ck;
    pill(rx + sw + 8, sy, pw, "Sort", SORT_NAMES[st->sort_mode], &ck);
    if (ck) { st->sort_mode = (st->sort_mode + 1) % SORT_N; resort(c); }
    pill(rx + sw + 16 + pw, sy, pw, "Group", GROUP_NAMES[st->group_mode], &ck);
    if (ck) { st->group_mode = (st->group_mode + 1) % GROUP_N; resort(c); }

    /* ---- carousel ---- */
    float ly = 124, lh = H - ly - 24;
    SDL_Rect clip = { (int)rx - 4, (int)ly, (int)rw + 8, (int)lh };
    SDL_RenderSetClipRect(c->ren, &clip);
    float c0 = in.clip0, c1 = in.clip1; in.clip0 = ly; in.clip1 = ly + lh;
    float maxs = fmaxf(0, c->total_h - lh);
    if (in.wheel) { c->scroll -= in.wheel * 70; c->target = c->scroll; c->animating = 0; c->center = 0; }
    if (c->center) {
        for (int i = 0; i < c->nrows; i++) {
            Row *r = &c->rows[i];
            if ((r->type == ROW_DIFF && r->a == c->sel_set && r->b == c->sel_diff) || (r->type == ROW_SET && r->a == c->sel_set && 0)) {
                c->target = r->y + r->h / 2 - lh / 2;
            }
        }
        for (int i = 0; i < c->nrows; i++) {  /* keep the whole selected set in view if it fits */
            Row *r = &c->rows[i];
            if (r->type == ROW_SET && r->a == c->sel_set) {
                float top = r->y, bot = r->y;
                for (int j = i + 1; j < c->nrows && c->rows[j].type == ROW_DIFF; j++) bot = c->rows[j].y + c->rows[j].h;
                if (bot - top + SET_H < lh) { float mid = (top + bot) / 2; c->target = mid - lh / 2; }
                else c->target = top - 20;
            }
        }
        c->center = 0; c->animating = 1;
    }
    c->target = clampf(c->target, 0, maxs);
    if (c->animating) {
        c->scroll += (c->target - c->scroll) * 0.25f;
        if (fabsf(c->target - c->scroll) < 0.5f) { c->scroll = c->target; c->animating = 0; }
    }
    c->scroll = clampf(c->scroll, 0, maxs);

    for (int i = 0; i < c->nrows; i++) {
        Row *r = &c->rows[i];
        float y = ly + r->y - c->scroll;
        if (y + r->h < ly || y > ly + lh) continue;
        MapSet *s = &c->lib.s[r->a];
        if (r->type == ROW_HEADER) {
            char gl[64]; lib_group_label(s, st->group_mode, gl, sizeof gl);
            gfx_text(gl, rx + 6, y + 10, 15, 1, C_LABEL2, rw, A_LEFT);
            gfx_line_h(rx + 6 + gfx_text_w(gl, 15, 1) + 10, y + 19, rw - gfx_text_w(gl, 15, 1) - 20, C_SEP);
        } else if (r->type == ROW_SET) {
            int sel = r->a == c->sel_set;
            float ch = r->h - 8;
            SDL_Color bgc = sel ? C_CELL2 : C_CELL;
            if (sel) gfx_rrect(rx, y, rw, ch, 14, C_ACCENT);
            float o = sel ? 2 : 0;
            gfx_rrect(rx + o, y + o, rw - 2 * o, ch - 2 * o, 12, bgc);
            float tx = rx + 14, ty = roundf(y + (ch - 62) / 2);
            SDL_Texture *t = thumb(c, s->bg);
            if (t) {
                SDL_SetTextureColorMod(t, 255, 255, 255); SDL_SetTextureAlphaMod(t, 255);
                SDL_FRect d = { tx, ty, 110, 62 }; SDL_RenderCopyF(c->ren, t, NULL, &d);
                gfx_round_mask(tx, ty, 110, 62, 9, bgc);
            } else gfx_rrect(tx, ty, 110, 62, 9, C_SEG);
            float x0 = tx + 124, mw = rw - (x0 - rx) - 110;
            gfx_text(s->d[0].mi.title, x0, y + 12, 16, 1, C_LABEL, mw, A_LEFT);
            gfx_text(s->d[0].mi.artist, x0, y + 36, 13, 0, C_LABEL2, mw, A_LEFT);
            snprintf(b, sizeof b, "mapped by %s", s->d[0].mi.creator);
            gfx_text(b, x0, y + 56, 12, 0, C_LABEL2, mw, A_LEFT);
            /* difficulty spectrum dots + star range */
            int nd = s->nd > 10 ? 10 : s->nd;
            for (int j = 0; j < nd; j++) gfx_circle(rx + rw - 18 - j * 13, y + ch - 20, 4.5f, star_color(s->d[s->nd - 1 - j].mi.stars));
            snprintf(b, sizeof b, "%.1f - %.1f", s->smin, s->smax);
            gfx_text(b, rx + rw - 16, y + 14, 12, 0, C_LABEL2, 0, A_RIGHT);
            gfx_star(rx + rw - 22 - gfx_text_w(b, 12, 0), y + 22, 5, C_LABEL2);
            if (in.pressed && over(rx, y, rw, ch)) {
                if (r->a != c->sel_set) select_set(c, r->a);
                else if (c->dbl) commit_play(c);
            }
        } else {
            int sel = r->a == c->sel_set && r->b == c->sel_diff;
            Diff *d = &s->d[r->b];
            float dx = rx + 30, dw = rw - 30, dh = r->h - 8;
            SDL_Color col = star_color(d->mi.stars);
            if (sel) { gfx_rrect(dx, y, dw, dh, 12, col); gfx_rrect(dx + 2, y + 2, dw - 4, dh - 4, 10, C_CELL2); }
            else gfx_rrect(dx, y, dw, dh, 12, C_CELL);
            SDL_Color inner = sel ? C_CELL2 : C_CELL;
            gfx_circle(dx + 28, y + dh / 2, 13, col); gfx_circle(dx + 28, y + dh / 2, 8.5f, inner);
            float pwid = 82;
            gfx_text(d->mi.version, dx + 54, y + 8, 15, sel, C_LABEL, dw - 54 - pwid - 24, A_LEFT);
            snprintf(b, sizeof b, "CS %.1f  AR %.1f  OD %.1f  HP %.1f   %d objects", d->mi.cs, d->mi.ar, d->mi.od, d->mi.hp, d->mi.nobj);
            gfx_text(b, dx + 54, y + 29, 12, 0, C_LABEL2, dw - 54 - pwid - 24, A_LEFT);
            float px = dx + dw - 14 - pwid, py = y + (dh - 26) / 2;
            gfx_rrect(px, py, pwid, 26, 13, col);
            SDL_Color tc = d->mi.stars >= 6.5f ? C_WHITE : (SDL_Color){ 22, 22, 28, 255 };
            gfx_star(px + 17, py + 13.5f, 6, tc);
            snprintf(b, sizeof b, "%.2f", d->mi.stars);
            gfx_text(b, px + 29, py + 5, 14, 1, tc, 0, A_LEFT);
            if (in.pressed && over(dx, y, dw, dh)) {
                if (sel) commit_play(c); else select_diff(c, r->b);
            }
        }
    }
    if (!c->lib.n) gfx_text("No beatmaps yet. Drop a .osz onto the window, or set songs_dir in settings.ini", rx + rw / 2, ly + 30, 15, 0, C_LABEL2, rw - 40, A_CENTER);
    else if (!c->nvis_sets) gfx_text("No matches", rx + rw / 2, ly + 30, 15, 0, C_LABEL2, rw - 40, A_CENTER);
    SDL_RenderSetClipRect(c->ren, NULL);
    in.clip0 = c0; in.clip1 = c1;

    /* ---- details wedge ---- */
    float dy = 74, dh = H - dy - 24;
    gfx_rrect(lx, dy, lw, dh, 14, C_CELL);
    SDL_Rect dclip = { (int)lx, (int)dy, (int)lw, (int)dh };
    SDL_RenderSetClipRect(c->ren, &dclip);
    MapSet *s = cur_set(c); Diff *df = cur_diff(c);
    float ix = lx + 16, iw = lw - 32, ih = floorf(fminf(iw * 9 / 16, dh * 0.30f)), y = dy + 16;
    if (c->cover) {
        SDL_Rect cr = { (int)ix, (int)y, (int)iw, (int)ih };
        SDL_RenderSetClipRect(c->ren, &cr);
        SDL_SetTextureColorMod(c->cover, 255, 255, 255);
        gfx_cover(c->cover, c->cw, c->ch, ix, y, iw, ih);
        SDL_RenderSetClipRect(c->ren, &dclip);
        gfx_round_mask(ix, y, iw, ih, 12, C_CELL);
    } else {
        gfx_rrect(ix, y, iw, ih, 12, C_CELL2);
        gfx_text(s ? "No cover" : "", ix + iw / 2, y + ih / 2 - 9, 16, 0, C_LABEL2, 0, A_CENTER);
    }
    y += ih + 12;
    if (s && df) {
        const MapInfo *m = &df->mi; SDL_Color col = star_color(m->stars);
        gfx_text(m->title, ix, y, 21, 1, C_LABEL, iw, A_LEFT); y += 28;
        gfx_text(m->artist, ix, y, 15, 0, C_LABEL2, iw, A_LEFT); y += 22;
        snprintf(b, sizeof b, "mapped by %s", m->creator);
        gfx_text(b, ix, y, 12, 0, C_LABEL2, iw, A_LEFT); y += 24;
        /* difficulty name + stars */
        float nw = gfx_text_w(m->version, 13, 1) + 22; if (nw > iw - 100) nw = iw - 100;
        gfx_rrect(ix, y, nw, 26, 13, gfx_alpha(col, 70));
        gfx_text(m->version, ix + nw / 2, y + 5, 13, 1, C_LABEL, nw - 14, A_CENTER);
        gfx_star(ix + nw + 22, y + 13.5f, 7, col);
        snprintf(b, sizeof b, "%.2f", m->stars);
        gfx_text(b, ix + nw + 34, y + 3, 17, 1, col, 0, A_LEFT);
        y += 36;
        gfx_rrect(ix, y, iw, 6, 3, C_SEG); gfx_rrect(ix, y, fmaxf(6, iw * fminf(m->stars / 10.0f, 1)), 6, 3, col);
        gfx_text("estimated difficulty", ix, y + 10, 11, 0, C_LABEL2, iw, A_LEFT);
        y += 32;
        const char *lab[4] = { "CS", "AR", "OD", "HP" }; float val[4] = { m->cs, m->ar, m->od, m->hp };
        for (int i = 0; i < 4; i++) {
            gfx_text(lab[i], ix, y, 13, 0, C_LABEL2, 0, A_LEFT);
            float bx2 = ix + 34, bw = iw - 34 - 44;
            gfx_rrect(bx2, y + 6, bw, 5, 2.5f, C_SEG); gfx_rrect(bx2, y + 6, fmaxf(5, bw * clampf(val[i] / 10.0f, 0, 1)), 5, 2.5f, C_ACCENT);
            snprintf(b, sizeof b, "%.1f", val[i]); gfx_text(b, ix + iw, y, 13, 1, C_LABEL, 0, A_RIGHT);
            y += 22;
        }
        y += 6;
        int len = m->length_ms / 1000;
        snprintf(b, sizeof b, "BPM %d     %d:%02d     %d objects", m->bpm, len / 60, len % 60, m->nobj);
        gfx_text(b, ix, y, 13, 0, C_LABEL, iw, A_LEFT); y += 20;
        snprintf(b, sizeof b, "%d circles   %d sliders   %d spinners", m->nobj - m->nslider - m->nspinner, m->nslider, m->nspinner);
        gfx_text(b, ix, y, 12, 0, C_LABEL2, iw, A_LEFT); y += 22;
    }

    /* quick options, then Random + Play pinned to the bottom */
    float playH = 50, by2 = dy + dh - 16 - playH;
    float qy = by2 - 14 - (RH + GH) - (st->status[0] ? 22 : 0);
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
    if (st->status[0]) gfx_text(st->status, ix, by2 - 28, 12, 0, C_LABEL2, iw, A_LEFT);
    gfx_rrect(ix, by2, 96, playH, 14, C_CELL2);
    gfx_text("Random", ix + 48, by2 + 15, 15, 1, C_ACCENT, 0, A_CENTER);
    if (in.pressed && over(ix, by2, 96, playH)) random_set(c);
    gfx_rrect(ix + 106, by2, iw - 106, playH, 14, df ? C_ACCENT : C_OFF);
    gfx_text("Play", ix + 106 + (iw - 106) / 2.0f, by2 + 14, 18, 1, C_WHITE, 0, A_CENTER);
    if (in.pressed && df && over(ix + 106, by2, iw - 106, playH)) commit_play(c);
    SDL_RenderSetClipRect(c->ren, NULL);
    gfx_text("Up/Down sets   Left/Right difficulties   F2 random   F3 settings", W - 24, H - 20, 11, 0, gfx_alpha(C_LABEL2, 160), 0, A_RIGHT);
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
    float gh = SH * 4 + RH * 3 + GH * 2 + (soft ? SH : 0);   /* dim, blur, cursor size, input delay (+ soft radius) */
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
    c.win = win; c.ren = ren; c.st = st; c.running = 1; c.sel_set = c.sel_diff = -1;
    c.soft_r = st->assist > 1 ? st->assist : 2.5f;
    SDL_RenderSetVSync(ren, 1);
#ifdef AIMLESS_DEBUG
    if (getenv("AIMLESS_SORT")) st->sort_mode = atoi(getenv("AIMLESS_SORT"));
    if (getenv("AIMLESS_GROUP")) st->group_mode = atoi(getenv("AIMLESS_GROUP"));
    if (getenv("AIMLESS_QUERY")) snprintf(c.qtext, sizeof c.qtext, "%s", getenv("AIMLESS_QUERY"));
#endif
    rescan(&c);
#ifdef AIMLESS_DEBUG
    if (getenv("AIMLESS_SELECT")) { int k = atoi(getenv("AIMLESS_SELECT")); int n = 0;
        for (int i = 0; i < c.lib.n; i++) if (c.lib.s[i].nvd && n++ == k) { select_set(&c, i); if (getenv("AIMLESS_DIFF")) select_diff(&c, c.lib.s[i].vd[atoi(getenv("AIMLESS_DIFF")) % c.lib.s[i].nvd]); break; } }
    c.scroll = c.target; c.animating = 0;
#endif
    SDL_StartTextInput();
#ifdef AIMLESS_DEBUG
    if (getenv("AIMLESS_SCREEN") && !strcmp(getenv("AIMLESS_SCREEN"), "settings")) c.screen = 1;
    if (getenv("AIMLESS_SSCROLL")) { c.sscroll = (float)atoi(getenv("AIMLESS_SSCROLL")); c.scontent = 5000; }
    int frames = 0;
#endif

    while (c.running) {
        #ifdef AIMLESS_DEBUG /* scripted input for headless tests: AIMLESS_KEYS="d;r;t:kaifin;click:700:300;..." */
        { static const char *sc; static int init, wait;
          if (!init) { init = 1; sc = getenv("AIMLESS_KEYS"); }
          if (sc && *sc && ++wait > 3) {
              wait = 0;
              char tok[128]; size_t n = strcspn(sc, ";"); if (n >= sizeof tok) n = sizeof tok - 1;
              memcpy(tok, sc, n); tok[n] = 0; sc += n; if (*sc == ';') sc++;
              SDL_Event ev; memset(&ev, 0, sizeof ev);
              SDL_Keycode kc = 0;
              if (!strcmp(tok, "d")) kc = SDLK_DOWN; else if (!strcmp(tok, "u")) kc = SDLK_UP;
              else if (!strcmp(tok, "l")) kc = SDLK_LEFT; else if (!strcmp(tok, "r")) kc = SDLK_RIGHT;
              else if (!strcmp(tok, "f2")) kc = SDLK_F2; else if (!strcmp(tok, "bs")) kc = SDLK_BACKSPACE;
              else if (!strcmp(tok, "enter")) kc = SDLK_RETURN; else if (!strcmp(tok, "home")) kc = SDLK_HOME;
              if (kc) { ev.type = SDL_KEYDOWN; ev.key.keysym.sym = kc; SDL_PushEvent(&ev); }
              else if (!strncmp(tok, "t:", 2)) { ev.type = SDL_TEXTINPUT; snprintf(ev.text.text, sizeof ev.text.text, "%s", tok + 2); SDL_PushEvent(&ev); }
              else if (!strncmp(tok, "click:", 6)) {
                  int cx = 0, cy = 0; sscanf(tok + 6, "%d:%d", &cx, &cy);
                  SDL_WarpMouseInWindow(win, cx, cy); SDL_PumpEvents();
                  ev.type = SDL_MOUSEBUTTONDOWN; ev.button.button = SDL_BUTTON_LEFT; ev.button.clicks = 1; ev.button.x = cx; ev.button.y = cy; SDL_PushEvent(&ev);
                  SDL_Event up; memset(&up, 0, sizeof up); up.type = SDL_MOUSEBUTTONUP; up.button.button = SDL_BUTTON_LEFT; SDL_PushEvent(&up);
              }
              MapSet *cs = cur_set(&c); Diff *cd = cur_diff(&c);
              fprintf(stderr, "[%s] -> %s [%s] visible=%d/%d q='%s'\n", tok, cs ? cs->d[0].mi.title : "-", cd ? cd->mi.version : "-", c.nvis_sets, c.nvis_diffs, c.qtext);
          }
        }
#endif
in.pressed = 0; in.wheel = 0; in.idc = 0; c.dbl = 0; c.th_loads = 0;
        SDL_Event e;
        int have = SDL_WaitEventTimeout(&e, 16);
        while (have) {
            if (e.type == SDL_QUIT) { c.running = 0; c.result = 0; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                in.pressed = 1; in.down = 1; if (e.button.clicks >= 2) c.dbl = 1;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) in.down = 0;
            else if (e.type == SDL_MOUSEWHEEL) in.wheel += e.wheel.y;
            else if (e.type == SDL_TEXTINPUT && c.screen == 0) {
                if (strlen(c.qtext) + strlen(e.text.text) < sizeof c.qtext - 1) { strcat(c.qtext, e.text.text); refilter(&c); }
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F3) c.screen = !c.screen;
                else if (c.screen == 1) { if (k == SDLK_ESCAPE) c.screen = 0; }
                else switch (k) {
                case SDLK_ESCAPE: if (c.qtext[0]) { c.qtext[0] = 0; refilter(&c); } else { c.running = 0; c.result = 0; } break;
                case SDLK_BACKSPACE: if (c.qtext[0]) { utf8_backspace(c.qtext); refilter(&c); } break;
                case SDLK_DOWN: step_set(&c, 1); break;
                case SDLK_UP: step_set(&c, -1); break;
                case SDLK_PAGEDOWN: for (int i = 0; i < 6; i++) step_set(&c, 1); break;
                case SDLK_PAGEUP: for (int i = 0; i < 6; i++) step_set(&c, -1); break;
                case SDLK_RIGHT: step_diff(&c, 1); break;
                case SDLK_LEFT: step_diff(&c, -1); break;
                case SDLK_HOME: for (int i = 0; i < c.lib.n; i++) if (c.lib.s[i].nvd) { select_set(&c, i); break; } break;
                case SDLK_END: for (int i = c.lib.n - 1; i >= 0; i--) if (c.lib.s[i].nvd) { select_set(&c, i); break; } break;
                case SDLK_F2: random_set(&c); break;
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
        preview_update(&c);

        int W, H; SDL_GetWindowSize(win, &W, &H);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
        if (c.screen == 0) {
            if (c.cover) { /* dim backdrop from the cover */
                SDL_SetTextureColorMod(c.cover, 38, 38, 42);
                gfx_cover(c.cover, c.cw, c.ch, 0, 0, W, H);
            }
            draw_select(&c, W, H);
        } else draw_settings(&c, W, H);
        SDL_RenderPresent(ren);

#ifdef AIMLESS_DEBUG
        if (getenv("AIMLESS_SHOT") && ++frames == (getenv("AIMLESS_FRAMES") ? atoi(getenv("AIMLESS_FRAMES")) : 20)) { /* debug: dump a frame and quit */
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            IMG_SavePNG(s, getenv("AIMLESS_SHOT")); SDL_FreeSurface(s);
            c.running = 0; c.result = 0;
        }
#endif
    }

    SDL_StopTextInput();
    Mix_HaltMusic();
    if (c.pm) Mix_FreeMusic(c.pm);
    Diff *d = cur_diff(&c);
    if (c.result && d) {
        snprintf(map_out, map_n, "%s", d->path);
        snprintf(skin_out, skin_n, "%s", c.skin_sel < 0 ? "" : c.skins.e[c.skin_sel].path);
    } else c.result = 0;
    settings_save(st);
    if (c.cover) SDL_DestroyTexture(c.cover);
    for (int i = 0; i < NTHUMB; i++) if (c.th[i].t) SDL_DestroyTexture(c.th[i].t);
    lib_free(&c.lib); list_free(&c.skins); free(c.rows);
    gfx_quit();
    return c.result;
}
