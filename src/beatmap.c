/* Minimal .osu parser. Sliders are treated as circles (head only), spinners skipped. */
#include "beatmap.h"
#include <stdio.h>
#include <stdlib.h>
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

int beatmap_load(Beatmap *b, const char *path, int meta_only) {
    memset(b, 0, sizeof *b);
    b->cs = 4; b->ar = 5; b->od = 5; b->default_set = 1;
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
            float t; int set = 0, idx = 0, vol = 100;
            if (sscanf(line, "%f,%*f,%*d,%d,%d,%d", &t, &set, &idx, &vol) < 2) continue;
            if (b->ntp == tcap) { tcap *= 2; b->tp = realloc(b->tp, tcap * sizeof(Timing)); }
            b->tp[b->ntp++] = (Timing){ (int)t, set, vol };
        } else if (sec == 3 && line[0]) {
            float x, y; int t, type, hs = 0;
            if (sscanf(line, "%f,%f,%d,%d,%d", &x, &y, &t, &type, &hs) < 4) continue;
            if (type & 8) continue; /* spinner */
            if (type & 4 || color < 0) { num = 0; color++; }
            num++;
            int ns = 0, as = 0, ov = 0;
            if (type & 1) { /* circle: hitSample is the 6th field */
                const char *p = line; int commas = 0;
                while (*p && commas < 5) if (*p++ == ',') commas++;
                if (commas == 5) sscanf(p, "%d:%d:%*d:%d", &ns, &as, &ov);
            }
            if (b->n == cap) { cap *= 2; b->objs = realloc(b->objs, cap * sizeof(Obj)); }
            b->objs[b->n++] = (Obj){ x, y, t, num, color, 0, 0, 0, hs, ns, as, ov };
        }
    }
    fclose(f);

    /* resolve sample sets/volumes from timing points */
    int ti = 0;
    for (int i = 0; i < b->n; i++) {
        Obj *o = &b->objs[i];
        while (ti + 1 < b->ntp && b->tp[ti + 1].time <= o->time) ti++;
        int tset = b->ntp ? b->tp[ti].set : 0, tvol = b->ntp ? b->tp[ti].vol : 100;
        if (!o->nset) o->nset = tset ? tset : b->default_set;
        if (!o->aset) o->aset = o->nset;
        if (!o->vol) o->vol = tvol ? tvol : 100;
    }
    return (meta_only || b->n) ? 0 : -2;
}

void beatmap_free(Beatmap *b) { free(b->objs); free(b->tp); b->objs = NULL; b->tp = NULL; b->n = b->ntp = 0; }
