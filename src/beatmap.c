/* .osu parser: circles, sliders (all curve types, ticks, repeats), timing/sample resolution. Spinners skipped. */
#include "beatmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
}

static void copy_val(char *dst, size_t n, const char *v) {
    while (*v == ' ') v++;
    snprintf(dst, n, "%.*s", (int)(n - 1), v);
}

static int set_from_name(const char *v) {
    while (*v == ' ') v++;
    return !strncasecmp(v, "soft", 4) ? 2 : !strncasecmp(v, "drum", 4) ? 3 : 1;
}

/* pointer just after the n-th comma of s, or NULL */
static const char *field(const char *s, int n) {
    while (n-- > 0) { s = strchr(s, ','); if (!s) return NULL; s++; }
    return s;
}

/* stable insertion sort (qsort is not stable and equal-time points must keep file order) */
static void mergesort_tp(Beatmap *b) {
    for (int i = 1; i < b->ntp; i++) {
        Timing k = b->tp[i]; int j = i - 1;
        while (j >= 0 && b->tp[j].time > k.time) { b->tp[j + 1] = b->tp[j]; j--; }
        b->tp[j + 1] = k;
    }
}

/* ---- slider geometry ---- */
typedef struct { float *p; int n, cap; } Poly;
static void poly_add(Poly *a, float x, float y) {
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 128; a->p = realloc(a->p, a->cap * 2 * sizeof(float)); }
    a->p[a->n * 2] = x; a->p[a->n * 2 + 1] = y; a->n++;
}
static void bezier(Poly *a, const float *pts, int k) {
    if (k < 2) return;
    int steps = k * 12; if (steps > 120) steps = 120;
    float tmp[64 * 2];
    if (k > 64) k = 64;
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        memcpy(tmp, pts, k * 2 * sizeof(float));
        for (int lvl = k - 1; lvl > 0; lvl--)
            for (int i = 0; i < lvl; i++) {
                tmp[i*2] += (tmp[(i+1)*2] - tmp[i*2]) * t;
                tmp[i*2+1] += (tmp[(i+1)*2+1] - tmp[i*2+1]) * t;
            }
        poly_add(a, tmp[0], tmp[1]);
    }
}
static int arc(Poly *a, const float *c, float length) {
    float ax = c[0], ay = c[1], bx = c[2], by = c[3], cx = c[4], cy = c[5];
    float d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (fabsf(d) < 1e-3f) return 0;
    float a2 = ax*ax + ay*ay, b2 = bx*bx + by*by, c2 = cx*cx + cy*cy;
    float ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    float uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    float r = hypotf(ax - ux, ay - uy);
    float t0 = atan2f(ay - uy, ax - ux), t1 = atan2f(by - uy, bx - ux), t2 = atan2f(cy - uy, cx - ux);
    float dir = 1;
    while (t1 < t0) t1 += 2 * (float)M_PI;
    while (t2 < t0) t2 += 2 * (float)M_PI;
    if (t1 > t2) dir = -1;
    float span = length / r, step = 0.05f;
    for (float s = 0; s < span; s += step) poly_add(a, ux + r * cosf(t0 + dir * s), uy + r * sinf(t0 + dir * s));
    poly_add(a, ux + r * cosf(t0 + dir * span), uy + r * sinf(t0 + dir * span));
    return 1;
}

static void build_path(Obj *o) {
    Poly raw = {0};
    int done = 0;
    if (o->curve == 'P' && o->ncp == 3) done = arc(&raw, o->cp, o->length);
    if (!done) {
        raw.n = 0;
        if (o->curve == 'L' || o->curve == 'C') for (int i = 0; i < o->ncp; i++) poly_add(&raw, o->cp[i*2], o->cp[i*2+1]);
        else { /* bezier, split at repeated points */
            int st = 0;
            for (int i = 1; i <= o->ncp; i++) {
                int brk = i == o->ncp || (o->cp[i*2] == o->cp[(i-1)*2] && o->cp[i*2+1] == o->cp[(i-1)*2+1]);
                if (brk) { bezier(&raw, o->cp + st * 2, i - st); st = i; }
            }
        }
    }
    /* cumulative lengths, then resample at uniform spacing, extending the tail if too short */
    int n = raw.n; if (n < 2) { poly_add(&raw, o->x + 1, o->y); n = raw.n; }
    float *cum = malloc(n * sizeof(float)); cum[0] = 0;
    for (int i = 1; i < n; i++) cum[i] = cum[i-1] + hypotf(raw.p[i*2] - raw.p[(i-1)*2], raw.p[i*2+1] - raw.p[(i-1)*2+1]);
    int N = (int)(o->length / 3) + 2; if (N < 2) N = 2;
    o->path = malloc(N * 2 * sizeof(float)); o->npath = N; o->step = o->length / (N - 1);
    int j = 1;
    for (int k = 0; k < N; k++) {
        float d = k * o->step;
        while (j < n - 1 && cum[j] < d) j++;
        float seg = cum[j] - cum[j-1]; if (seg < 1e-4f) seg = 1e-4f;
        float u = (d - cum[j-1]) / seg; /* >1 extends past the end */
        o->path[k*2] = raw.p[(j-1)*2] + (raw.p[j*2] - raw.p[(j-1)*2]) * u;
        o->path[k*2+1] = raw.p[(j-1)*2+1] + (raw.p[j*2+1] - raw.p[(j-1)*2+1]) * u;
    }
    free(cum); free(raw.p); free(o->cp); o->cp = NULL;
}

static void build_sliders(Beatmap *b) {
    int ti = 0; float beat = 500, sv = 1;
    for (int i = 0; i < b->n; i++) {
        Obj *o = &b->objs[i];
        if (!o->type) continue;
        for (; ti < b->ntp && b->tp[ti].time <= o->time; ti++) {
            if (b->tp[ti].uninh) { if (b->tp[ti].beat > 0) beat = b->tp[ti].beat; sv = 1; }
            else { sv = b->tp[ti].beat < 0 ? -100.0f / b->tp[ti].beat : 1; if (sv < 0.1f) sv = 0.1f; if (sv > 10) sv = 10; }
        }
        build_path(o);
        float sd = o->length / (b->sm * 100 * sv) * beat;      /* one slide, ms */
        float tick = beat / (b->tickrate > 0 ? b->tickrate : 1);
        int cap = o->slides * 4 + 8, n = 0;
        int *ev = malloc(cap * 2 * sizeof(int));
        for (int s = 0; s < o->slides; s++) {
            float st = o->time + s * sd;
            for (int k = 1; k * tick < sd - 10; k++) {
                if (n == cap) { cap *= 2; ev = realloc(ev, cap * 2 * sizeof(int)); }
                ev[n*2] = (int)(st + k * tick); ev[n*2+1] = 0; n++;
            }
            if (n == cap) { cap *= 2; ev = realloc(ev, cap * 2 * sizeof(int)); }
            ev[n*2] = (int)(st + sd); ev[n*2+1] = s == o->slides - 1 ? 2 : 1; n++;
        }
        o->ev = ev; o->nev = n; o->endtime = (int)(o->time + sd * o->slides);
    }
}


/* "x,y,time,type,hs,curve|x:y|...,slides,length,edgeSounds,edgeSets,hitSample" */
static void parse_slider(Obj *o, const char *line) {
    const char *p = field(line, 5);
    if (!p) return;
    o->curve = *p;
    int ncp = 1; for (const char *q = p; *q && *q != ','; q++) if (*q == '|') ncp++;
    o->cp = malloc((ncp + 1) * 2 * sizeof(float));
    o->cp[0] = o->x; o->cp[1] = o->y; o->ncp = 1;
    const char *q = strchr(p, '|'), *end = strchr(p, ',');
    while (q && end && q < end && o->ncp <= ncp) {
        float px, py;
        if (sscanf(q + 1, "%f:%f", &px, &py) != 2) break;
        o->cp[o->ncp * 2] = px; o->cp[o->ncp * 2 + 1] = py; o->ncp++;
        q = strchr(q + 1, '|');
    }
    const char *f6 = field(line, 6), *f7 = field(line, 7);
    if (f6) o->slides = atoi(f6);
    if (f7) o->length = (float)atof(f7);
    if (o->slides < 1) o->slides = 1;
    if (o->slides > 200) o->slides = 200;
    if (o->length > 1 && o->ncp >= 2) o->type = 1;
    if (!o->type) { free(o->cp); o->cp = NULL; o->ncp = 0; return; }
    /* per-node samples; 0 = unresolved (filled from the object / timing point later) */
    int nn = o->slides + 1;
    o->nhs = calloc(nn, sizeof(int)); o->nns = calloc(nn, sizeof(int)); o->nas = calloc(nn, sizeof(int));
    for (int i = 0; i < nn; i++) o->nhs[i] = o->hs;
    const char *es = field(line, 8), *eb = field(line, 9);
    for (int i = 0; es && i < nn; i++) {
        if (!*es || *es == ',') break;
        o->nhs[i] = atoi(es);
        es = strchr(es, '|'); if (!es) break; es++;
    }
    for (int i = 0; eb && i < nn; i++) {
        int a = 0, c = 0;
        if (sscanf(eb, "%d:%d", &a, &c) < 1) break;
        o->nns[i] = a; o->nas[i] = c;
        eb = strchr(eb, '|');
        if (!eb) break;
        eb++;
    }
}

int beatmap_load(Beatmap *b, const char *path, int meta_only) {
    memset(b, 0, sizeof *b);
    b->cs = 4; b->ar = 5; b->od = 5; b->sm = 1.4f; b->tickrate = 1; b->default_set = 1;
    snprintf(b->path, sizeof b->path, "%s", path);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    const char *slash = strrchr(path, '/');
    if (slash) { size_t n = slash - path; if (n > 500) n = 500; memcpy(b->dir, path, n); b->dir[n] = 0; }
    else strcpy(b->dir, ".");

    int cap = 256, tcap = 16, sec = 0, num = 0, color = -1;
    b->objs = malloc(cap * sizeof(Obj));
    b->tp = malloc(tcap * sizeof(Timing));
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '[') {
            sec = !strcmp(line, "[General]") ? 1 : !strcmp(line, "[Difficulty]") ? 2 :
                  !strcmp(line, "[HitObjects]") ? 3 : !strcmp(line, "[Metadata]") ? 4 :
                  !strcmp(line, "[TimingPoints]") ? 5 :
                  !strcmp(line, "[Events]") ? 6 : 0;
            if (sec == 3 && meta_only) break;
            continue;
        }
        if (sec == 1) {
            if (!strncmp(line, "AudioFilename:", 14)) copy_val(b->audio, sizeof b->audio, line + 14);
            else if (!strncmp(line, "SampleSet:", 10)) b->default_set = set_from_name(line + 10);
        } else if (sec == 2) {
            if (!strncmp(line, "CircleSize:", 11)) b->cs = atof(line + 11);
            else if (!strncmp(line, "ApproachRate:", 13)) b->ar = atof(line + 13);
            else if (!strncmp(line, "OverallDifficulty:", 18)) b->od = atof(line + 18);
            else if (!strncmp(line, "SliderMultiplier:", 17)) b->sm = atof(line + 17);
            else if (!strncmp(line, "SliderTickRate:", 15)) b->tickrate = atof(line + 15);
        } else if (sec == 4) {
            if (!strncmp(line, "Title:", 6)) copy_val(b->title, sizeof b->title, line + 6);
            else if (!strncmp(line, "Artist:", 7)) copy_val(b->artist, sizeof b->artist, line + 7);
            else if (!strncmp(line, "Version:", 8)) copy_val(b->version, sizeof b->version, line + 8);
        } else if (sec == 6) {
            /* background: 0,0,"file.jpg",x,y (videos start with 1 or "Video") */
            if (!b->bg[0] && line[0] == '0' && line[1] == ',') {
                const char *q = strchr(line, '"');
                if (q) {
                    const char *e = strchr(q + 1, '"');
                    if (e) {
                        snprintf(b->bg, sizeof b->bg, "%.*s", (int)(e - q - 1 > 255 ? 255 : e - q - 1), q + 1);
                        for (char *c = b->bg; *c; c++) if (*c == '\\') *c = '/';
                    }
                }
            }
        } else if (sec == 5 && line[0] && !meta_only) {
            float t, beat = 500; int set = 0, idx = 0, vol = 100, uninh = 1;
            if (sscanf(line, "%f,%f,%*d,%d,%d,%d,%d", &t, &beat, &set, &idx, &vol, &uninh) < 2) continue;
            if (b->ntp == tcap) { tcap *= 2; b->tp = realloc(b->tp, tcap * sizeof(Timing)); }
            b->tp[b->ntp++] = (Timing){ (int)t, set, vol, idx, uninh, beat };
        } else if (sec == 3 && line[0]) {
            float x, y; int t, type, hs = 0;
            if (sscanf(line, "%f,%f,%d,%d,%d", &x, &y, &t, &type, &hs) < 4) continue;
            if (type & 8) continue; /* spinner */
            if (type & 4 || color < 0) { num = 0; color++; }
            num++;
            int ns = 0, as = 0, ov = 0, ix = 0; char sfile[40] = "";
            /* hitSample "normalSet:additionSet:index:volume:filename" is field 5 (circles) or 10 (sliders) */
            const char *hsf = field(line, (type & 2) ? 10 : 5);
            if (hsf) sscanf(hsf, "%d:%d:%d:%d:%39[^\r\n,]", &ns, &as, &ix, &ov, sfile);
            if (b->n == cap) { cap *= 2; b->objs = realloc(b->objs, cap * sizeof(Obj)); }
            Obj *o = &b->objs[b->n++];
            memset(o, 0, sizeof *o);
            o->x = x; o->y = y; o->time = t; o->num = num; o->color = color; o->hs = hs;
            o->nset = ns; o->aset = as; o->vol = ov; o->idx = ix;
            snprintf(o->sfile, sizeof o->sfile, "%s", sfile);
            o->endtime = t; o->slides = 1;
            if (type & 2) parse_slider(o, line);
        }
    }
    fclose(f);

    /* timing points are not always sorted in the file; the lookup below needs them sorted */
    if (b->ntp > 1) { int sorted = 1; for (int i = 1; i < b->ntp; i++) if (b->tp[i].time < b->tp[i-1].time) sorted = 0;
                      if (!sorted) mergesort_tp(b); }
    /* resolve sample sets/volumes/index from timing points */
    int ti = 0;
    for (int i = 0; i < b->n; i++) {
        Obj *o = &b->objs[i];
        int raw_as = o->aset;
        while (ti + 1 < b->ntp && b->tp[ti + 1].time <= o->time) ti++;
        int tset = b->ntp ? b->tp[ti].set : 0, tvol = b->ntp ? b->tp[ti].vol : 100;
        if (!o->nset) o->nset = tset ? tset : b->default_set;
        if (!o->aset) o->aset = o->nset;
        if (!o->vol) o->vol = tvol ? tvol : 100;
        if (!o->idx) o->idx = b->ntp ? b->tp[ti].idx : 0;
        if (o->type) for (int k = 0; k <= o->slides; k++) {
            int nn = o->nns[k] ? o->nns[k] : o->nset;
            int na = o->nas[k] ? o->nas[k] : (raw_as ? o->aset : nn);
            o->nns[k] = nn; o->nas[k] = na;
        }
    }
    if (!meta_only) build_sliders(b);
    for (int i = 0; i < b->n; i++) { int e = b->objs[i].endtime > b->objs[i].time ? b->objs[i].endtime : b->objs[i].time; if (e > b->last_end) b->last_end = e; }
    return (meta_only || b->n) ? 0 : -2;
}

void beatmap_free(Beatmap *b) {
    for (int i = 0; i < b->n; i++) { Obj *o = &b->objs[i]; free(o->path); free(o->ev); free(o->cp); free(o->nhs); free(o->nns); free(o->nas); }
    free(b->objs); free(b->tp); b->objs = NULL; b->tp = NULL; b->n = b->ntp = 0; }
