#include "mapinfo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double t, beat; int uninh; } TP;

static void cp(char *dst, size_t n, const char *v) {
    while (*v == ' ') v++;
    snprintf(dst, n, "%.*s", (int)(n - 1), v);
    size_t l = strlen(dst);
    while (l && (dst[l-1] == '\r' || dst[l-1] == '\n' || dst[l-1] == ' ')) dst[--l] = 0;
}

static double speed_w(double d) { /* spacing weight of the classic strain model */
    if (d > 125) return 2.5;
    if (d > 110) return 1.6 + 0.9 * (d - 110) / 15;
    if (d > 90) return 1.2 + 0.4 * (d - 90) / 20;
    if (d > 45) return 0.95 + 0.25 * (d - 45) / 45;
    return 0.95;
}

typedef struct { double *v; int n, cap; } Peaks;
static void push(Peaks *p, double x) {
    if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 256; p->v = realloc(p->v, p->cap * sizeof(double)); }
    p->v[p->n++] = x;
}
static int dcmp(const void *a, const void *b) { double d = *(double *)b - *(double *)a; return d < 0 ? -1 : d > 0; }
static double weighted(Peaks *p) {
    qsort(p->v, p->n, sizeof(double), dcmp);
    double sum = 0, w = 1;
    for (int i = 0; i < p->n; i++) { sum += p->v[i] * w; w *= 0.9; }
    return sum;
}

int mapinfo_scan(const char *path, MapInfo *m) {
    memset(m, 0, sizeof *m);
    m->cs = 4; m->od = 5; m->hp = 5; m->ar = -1; m->preview_ms = -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[4096];
    int sec = 0;
    float sm = 1.4f;
    TP *tp = NULL; int ntp = 0, tcap = 0, ti = -1;
    double beat = 500, sv = 1, first_beat = 0;
    Peaks ps = {0}, pa = {0};
    int have_prev = 0; double px = 0, py = 0, pt = 0, sp = 0, aim = 0, peak_s = 0, peak_a = 0, sec_end = 0;
    double scale = 52.0 / 36.5;
    int objs_started = 0;

    while (fgets(line, sizeof line, f)) {
        size_t ll = strlen(line);
        while (ll && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = 0;
        if (line[0] == '[') {
            sec = !strcmp(line, "[General]") ? 1 : !strcmp(line, "[Metadata]") ? 2 : !strcmp(line, "[Difficulty]") ? 3 :
                  !strcmp(line, "[Events]") ? 4 : !strcmp(line, "[TimingPoints]") ? 5 : !strcmp(line, "[HitObjects]") ? 6 : 0;
            if (sec == 6) {
                if (m->ar < 0) m->ar = m->od;
                double r = 32.0 * (1 - 0.7 * (m->cs - 5) / 5);
                scale = 52.0 / r; if (r < 30) scale *= 1 + fmin(30 - r, 5) / 50;
            }
            continue;
        }
        if (sec == 1) {
            if (!strncmp(line, "AudioFilename:", 14)) cp(m->audio, sizeof m->audio, line + 14);
            else if (!strncmp(line, "PreviewTime:", 12)) m->preview_ms = atoi(line + 12);
        } else if (sec == 2) {
            if (!strncmp(line, "Title:", 6)) cp(m->title, sizeof m->title, line + 6);
            else if (!strncmp(line, "Artist:", 7)) cp(m->artist, sizeof m->artist, line + 7);
            else if (!strncmp(line, "Creator:", 8)) cp(m->creator, sizeof m->creator, line + 8);
            else if (!strncmp(line, "Version:", 8)) cp(m->version, sizeof m->version, line + 8);
            else if (!strncmp(line, "Source:", 7)) cp(m->source, sizeof m->source, line + 7);
            else if (!strncmp(line, "Tags:", 5)) cp(m->tags, sizeof m->tags, line + 5);
        } else if (sec == 3) {
            if (!strncmp(line, "CircleSize:", 11)) m->cs = atof(line + 11);
            else if (!strncmp(line, "ApproachRate:", 13)) m->ar = atof(line + 13);
            else if (!strncmp(line, "OverallDifficulty:", 18)) m->od = atof(line + 18);
            else if (!strncmp(line, "HPDrainRate:", 12)) m->hp = atof(line + 12);
            else if (!strncmp(line, "SliderMultiplier:", 17)) sm = atof(line + 17);
        } else if (sec == 4) {
            if (!m->bg[0] && (!strncmp(line, "0,0,\"", 5) || !strncmp(line, "0, 0, \"", 7))) {
                char *q = strchr(line, '"'), *e = q ? strchr(q + 1, '"') : NULL;
                if (e) { *e = 0; cp(m->bg, sizeof m->bg, q + 1); }
            }
        } else if (sec == 5 && line[0]) {
            double t, b; int un = -1;
            int got = sscanf(line, "%lf,%lf,%*d,%*d,%*d,%*d,%d", &t, &b, &un);
            if (got < 2) continue;
            if (ntp == tcap) { tcap = tcap ? tcap * 2 : 16; tp = realloc(tp, tcap * sizeof(TP)); }
            tp[ntp++] = (TP){ t, b, un < 0 ? b > 0 : un };
            if (!first_beat && tp[ntp-1].uninh && b > 0) first_beat = b;
        } else if (sec == 6 && line[0]) {
            if (!objs_started) { objs_started = 1; if (first_beat > 0) m->bpm = (int)lround(60000.0 / first_beat); }
            char *fl[9]; int nf = 0; fl[nf++] = line;
            for (char *p = line; *p && nf < 9; p++) if (*p == ',') { *p = 0; fl[nf++] = p + 1; }
            if (nf < 4) continue;
            double x = atof(fl[0]), y = atof(fl[1]), t = atof(fl[2]); int type = atoi(fl[3]);
            while (ti + 1 < ntp && tp[ti + 1].t <= t) {
                TP *q = &tp[++ti];
                if (q->uninh) { if (q->beat > 0) beat = q->beat; sv = 1; }
                else { sv = q->beat < 0 ? -100.0 / q->beat : 1; if (sv < 0.1) sv = 0.1; if (sv > 10) sv = 10; }
            }
            m->nobj++;
            double end = t;
            if (type & 8) { m->nspinner++; if (nf > 5) end = atof(fl[5]); }
            else {
                if (type & 2) {
                    m->nslider++;
                    if (nf > 7) { int slides = atoi(fl[6]); double len = atof(fl[7]); if (slides < 1) slides = 1;
                        end = t + len / (100.0 * sm * sv) * beat * slides; }
                }
                /* strain model on the object heads */
                if (have_prev) {
                    double dt = t - pt; if (dt < 50) dt = 50;
                    double d = hypot(x - px, y - py) * scale;
                    double decay_s = pow(0.3, dt / 1000), decay_a = pow(0.15, dt / 1000);
                    if (sec_end == 0) sec_end = pt + 400;
                    while (t > sec_end) {
                        push(&ps, peak_s); push(&pa, peak_a);
                        double gap = (sec_end - pt) / 1000.0; if (gap < 0) gap = 0;
                        peak_s = sp * pow(0.3, gap); peak_a = aim * pow(0.15, gap);
                        sec_end += 400;
                    }
                    sp = sp * decay_s + speed_w(d) * 1400.0 / dt;
                    aim = aim * decay_a + pow(d, 0.99) * 26.25 / dt;
                    if (sp > peak_s) peak_s = sp;
                    if (aim > peak_a) peak_a = aim;
                }
                px = x; py = y; pt = t; have_prev = 1;
            }
            if (end > m->length_ms) m->length_ms = (int)end;
        }
    }
    fclose(f);
    if (have_prev) {
        push(&ps, peak_s); push(&pa, peak_a);
        double s = sqrt(weighted(&ps)) * 0.0675, a = sqrt(weighted(&pa)) * 0.0675;
        m->stars = (float)(a + s + fabs(s - a) * 0.5);
    }
    free(ps.v); free(pa.v); free(tp);
    if (m->ar < 0) m->ar = m->od;
    if (!m->title[0] && !m->artist[0]) return -2;
    return 0;
}
