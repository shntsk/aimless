#define _GNU_SOURCE
#include "library.h"
#include "paths.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"

/* ---------- on-disk cache: path + mtime + size -> MapInfo ---------- */
typedef struct { char path[1024]; long mtime, size; MapInfo mi; } CEnt;
typedef struct { CEnt *e; int n; int *tab; int tsz; } Cache;
static const char MAGIC[8] = "AMCACHE1";

static unsigned hash(const char *s) { unsigned h = 5381; while (*s) h = h * 33 + (unsigned char)*s++; return h; }

static void cache_path(char *o, size_t n) { snprintf(o, n, "%s/library.cache", data_dir()); }

static void cache_index(Cache *c) {
    c->tsz = 1; while (c->tsz < c->n * 2 + 8) c->tsz <<= 1;
    c->tab = malloc(c->tsz * sizeof(int));
    for (int i = 0; i < c->tsz; i++) c->tab[i] = -1;
    for (int i = 0; i < c->n; i++) { unsigned h = hash(c->e[i].path) & (c->tsz - 1); while (c->tab[h] >= 0) h = (h + 1) & (c->tsz - 1); c->tab[h] = i; }
}

static void cache_load(Cache *c) {
    memset(c, 0, sizeof *c);
    char p[600]; cache_path(p, sizeof p);
    FILE *f = plat_fopen(p, "rb");
    if (f) {
        char mg[8]; int n = 0, sz = 0;
        if (fread(mg, 8, 1, f) == 1 && !memcmp(mg, MAGIC, 8) && fread(&sz, sizeof sz, 1, f) == 1 && sz == (int)sizeof(CEnt) &&
            fread(&n, sizeof n, 1, f) == 1 && n >= 0 && n < 5000000) {
            c->e = malloc((n + 1) * sizeof(CEnt));
            c->n = (int)fread(c->e, sizeof(CEnt), n, f);
        }
        fclose(f);
    }
    cache_index(c);
}

static CEnt *cache_find(Cache *c, const char *path, long mtime, long size) {
    unsigned h = hash(path) & (c->tsz - 1);
    while (c->tab[h] >= 0) {
        CEnt *e = &c->e[c->tab[h]];
        if (!strcmp(e->path, path)) return (e->mtime == mtime && e->size == size) ? e : NULL;
        h = (h + 1) & (c->tsz - 1);
    }
    return NULL;
}

static void cache_save(CEnt *e, int n) {
    char p[600], tmp[620]; cache_path(p, sizeof p); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = plat_fopen(tmp, "wb"); if (!f) return;
    int sz = (int)sizeof(CEnt);
    fwrite(MAGIC, 8, 1, f); fwrite(&sz, sizeof sz, 1, f); fwrite(&n, sizeof n, 1, f);
    fwrite(e, sizeof(CEnt), n, f);
    fclose(f);
    plat_rename(tmp, p);
}

/* ---------- scanning ---------- */
static int ends_osu(const char *s) { size_t n = strlen(s); return n > 4 && !strcasecmp(s + n - 4, ".osu"); }
static int dcmp_stars(const void *a, const void *b) {
    float d = ((const Diff *)a)->mi.stars - ((const Diff *)b)->mi.stars;
    return d < 0 ? -1 : d > 0 ? 1 : strcasecmp(((const Diff *)a)->mi.version, ((const Diff *)b)->mi.version);
}

typedef struct { CEnt *out; int n, cap; int fresh; int done; ProgressFn cb; void *ud; Cache *cache; } ScanCtx;

static void scan_root(Library *l, const char *root, ScanCtx *sc) {
    PlatDir *d = plat_opendir(root); if (!d) return;
    const char *e;
    while ((e = plat_readdir(d))) {
        if (e[0] == '.') continue;
        char dir[1024]; snprintf(dir, sizeof dir, "%s/%s", root, e);
        PlatStat ds; if (plat_stat(dir, &ds) || !ds.is_dir) continue;
        PlatDir *sd = plat_opendir(dir); if (!sd) continue;
        MapSet set; memset(&set, 0, sizeof set);
        snprintf(set.dir, sizeof set.dir, "%s", dir);
        set.mtime = (long)ds.mtime;
        const char *f; int cap = 0;
        while ((f = plat_readdir(sd))) {
            if (!ends_osu(f)) continue;
            char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, f);
            PlatStat fs; if (plat_stat(p, &fs)) continue;
            CEnt *ce = cache_find(sc->cache, p, (long)fs.mtime, (long)fs.size);
            if (sc->n == sc->cap) { sc->cap = sc->cap ? sc->cap * 2 : 1024; sc->out = realloc(sc->out, sc->cap * sizeof(CEnt)); }
            CEnt *o = &sc->out[sc->n];
            if (ce) *o = *ce;
            else {
                memset(o, 0, sizeof *o);
                snprintf(o->path, sizeof o->path, "%s", p); o->mtime = (long)fs.mtime; o->size = (long)fs.size;
                if (mapinfo_scan(p, &o->mi)) continue;
                sc->fresh++;
            }
            sc->n++;
            if (set.nd == cap) { cap = cap ? cap * 2 : 4; set.d = realloc(set.d, cap * sizeof(Diff)); }
            Diff *df = &set.d[set.nd++];
            snprintf(df->path, sizeof df->path, "%s", o->path); df->mi = o->mi; df->mtime = o->mtime;
            if (sc->cb && ++sc->done % 25 == 0) sc->cb(sc->done, sc->ud);
        }
        plat_closedir(sd);
        if (!set.nd) { free(set.d); continue; }
        qsort(set.d, set.nd, sizeof(Diff), dcmp_stars);
        set.smin = set.d[0].mi.stars; set.smax = set.d[set.nd - 1].mi.stars;
        set.bpm = set.d[0].mi.bpm; set.len = set.d[0].mi.length_ms; set.preview_ms = set.d[0].mi.preview_ms;
        for (int i = 0; i < set.nd; i++) if (!set.bg[0] && set.d[i].mi.bg[0]) snprintf(set.bg, sizeof set.bg, "%s/%s", dir, set.d[i].mi.bg);
        if (set.d[0].mi.audio[0]) snprintf(set.audio, sizeof set.audio, "%s/%s", dir, set.d[0].mi.audio);
        set.vd = malloc(set.nd * sizeof(int));
        l->s = realloc(l->s, (l->n + 1) * sizeof(MapSet));
        l->s[l->n++] = set; l->ndiff += set.nd;
    }
    plat_closedir(d);
}

void lib_scan(Library *l, const char *extra, ProgressFn cb, void *ud) {
    memset(l, 0, sizeof *l);
    Cache c; cache_load(&c);
    ScanCtx sc = { NULL, 0, 0, 0, 0, cb, ud, &c };
    char r[600]; songs_dir(r, sizeof r);
    scan_root(l, r, &sc);
    if (extra && *extra) scan_root(l, extra, &sc);
    if (sc.fresh || sc.n != c.n) cache_save(sc.out, sc.n);
    free(sc.out); free(c.e); free(c.tab);
}

void lib_free(Library *l) {
    for (int i = 0; i < l->n; i++) { free(l->s[i].d); free(l->s[i].vd); }
    free(l->s); memset(l, 0, sizeof *l);
}

/* ---------- sorting / grouping ---------- */
static int g_sort, g_group;

int lib_group_key(const MapSet *s, int group) {
    if (group == GROUP_ARTIST || group == GROUP_TITLE) {
        const char *t = group == GROUP_ARTIST ? s->d[0].mi.artist : s->d[0].mi.title;
        return isalpha((unsigned char)t[0]) ? toupper((unsigned char)t[0]) : '#';
    }
    if (group == GROUP_DIFF) { int b = (int)s->smax; return b > 10 ? 10 : b; }
    return 0;
}

void lib_group_label(const MapSet *s, int group, char *out, size_t n) {
    int k = lib_group_key(s, group);
    if (group == GROUP_DIFF) snprintf(out, n, k >= 10 ? "10+ stars" : "%d - %d stars", k, k + 1);
    else snprintf(out, n, "%c", k);
}

static int cmp_set(const void *pa, const void *pb) {
    const MapSet *a = pa, *b = pb;
    if (g_group) { int ka = lib_group_key(a, g_group), kb = lib_group_key(b, g_group); if (ka != kb) return ka - kb; }
    const MapInfo *x = &a->d[0].mi, *y = &b->d[0].mi; int r = 0;
    switch (g_sort) {
    case SORT_ARTIST: r = strcasecmp(x->artist, y->artist); if (!r) r = strcasecmp(x->title, y->title); break;
    case SORT_TITLE: r = strcasecmp(x->title, y->title); if (!r) r = strcasecmp(x->artist, y->artist); break;
    case SORT_DATE: r = a->mtime < b->mtime ? 1 : a->mtime > b->mtime ? -1 : 0; break;
    case SORT_DIFF: r = a->smin < b->smin ? -1 : a->smin > b->smin; break;
    case SORT_LENGTH: r = a->len - b->len; break;
    case SORT_BPM: r = a->bpm - b->bpm; break;
    }
    return r ? r : strcmp(a->dir, b->dir);
}

void lib_sort(Library *l, int sort, int group) {
    g_sort = sort; g_group = group;
    if (l->n > 1) qsort(l->s, l->n, sizeof(MapSet), cmp_set);
}

/* ---------- search ---------- */
enum { K_STARS, K_AR, K_CS, K_OD, K_HP, K_BPM, K_LEN, K_N };
static const char *KEYS[K_N] = { "stars", "ar", "cs", "od", "hp", "bpm", "length" };
enum { O_LT, O_LE, O_GT, O_GE, O_EQ };

void query_parse(Query *q, const char *text) {
    memset(q, 0, sizeof *q);
    char buf[256]; snprintf(buf, sizeof buf, "%s", text);
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        char *op = strpbrk(tok, "<>=");
        if (op && op > tok && q->nf < 8) {
            char key[16]; size_t kl = op - tok; if (kl >= sizeof key) kl = sizeof key - 1;
            memcpy(key, tok, kl); key[kl] = 0;
            if (!strcasecmp(key, "star")) strcpy(key, "stars");
            if (!strcasecmp(key, "len")) strcpy(key, "length");
            int o = op[0] == '<' ? (op[1] == '=' ? O_LE : O_LT) : op[0] == '>' ? (op[1] == '=' ? O_GE : O_GT) : O_EQ;
            const char *num = op + 1; while (*num == '=') num++;
            char *end; float v = strtof(num, &end);
            int k = -1; for (int i = 0; i < K_N; i++) if (!strcasecmp(key, KEYS[i])) k = i;
            if (k >= 0 && end != num) { q->f[q->nf].key = k; q->f[q->nf].op = o; q->f[q->nf].v = v; q->nf++; continue; }
        }
        if (q->nt < 8) snprintf(q->terms[q->nt++], 64, "%s", tok);
    }
}

static int diff_ok(const Query *q, const MapSet *s, const Diff *d) {
    const MapInfo *m = &d->mi;
    for (int i = 0; i < q->nf; i++) {
        float v = 0;
        switch (q->f[i].key) {
        case K_STARS: v = m->stars; break; case K_AR: v = m->ar; break; case K_CS: v = m->cs; break;
        case K_OD: v = m->od; break; case K_HP: v = m->hp; break; case K_BPM: v = (float)m->bpm; break;
        case K_LEN: v = m->length_ms / 1000.0f; break;
        }
        float t = q->f[i].v; int ok = 1;
        switch (q->f[i].op) {
        case O_LT: ok = v < t; break; case O_LE: ok = v <= t + 1e-4f; break;
        case O_GT: ok = v > t; break; case O_GE: ok = v >= t - 1e-4f; break;
        case O_EQ: ok = fabsf(v - t) < (q->f[i].key == K_STARS ? 0.05f : 0.5f); break;
        }
        if (!ok) return 0;
    }
    if (q->nt) {
        char hay[1100];
        snprintf(hay, sizeof hay, "%s %s %s %s %s %s", s->d[0].mi.title, s->d[0].mi.artist, m->creator, m->version, m->source, m->tags);
        for (int i = 0; i < q->nt; i++) if (!plat_strcasestr(hay, q->terms[i])) return 0;
    }
    return 1;
}

void lib_apply_query(Library *l, const Query *q) {
    for (int i = 0; i < l->n; i++) {
        MapSet *s = &l->s[i]; s->nvd = 0;
        for (int j = 0; j < s->nd; j++) if (diff_ok(q, s, &s->d[j])) s->vd[s->nvd++] = j;
    }
}
