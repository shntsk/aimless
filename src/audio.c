#include "audio.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCH 32
static const char *SETS[]  = { "normal", "soft", "drum" };
static const char *KINDS[] = { "hitnormal", "hitwhistle", "hitfinish", "hitclap", "slidertick" };
#define NK 5
static char g_skin[600], g_map[600];

typedef struct { int set, kind, idx; char file[40]; Mix_Chunk *c; } Entry;
#define MAXE 512
static Entry cache[MAXE];
static int ncache, next_ch;
static Mix_Chunk *gen[NK], *miss_c, *miss_gen;
static Sint16 *gen_buf[NK], *miss_buf;

static Mix_Chunk *try_name(const char *dir, const char *base) {
    static const char *EXT[] = { "wav", "ogg", "mp3" };
    char p[1300];
    if (!dir || !*dir) return NULL;
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s.%s", dir, base, EXT[i]);
        Mix_Chunk *c = Mix_LoadWAV(p);
        if (c) { if (getenv("AIMLESS_SFXLOG")) fprintf(stderr, "sample: %s\n", p); return c; }
    }
    return NULL;
}

static Mix_Chunk *make_tone(int kind) {
    int rate = 44100, ms[NK] = { 45, 130, 260, 90, 30 };
    float hz[NK] = { 1100, 2600, 0, 0, 1800 };
    int n = rate * ms[kind] / 1000;
    Sint16 *buf = malloc(n * 2 * sizeof(Sint16));
    for (int i = 0; i < n; i++) {
        float env = expf(-6.0f * i / n), v;
        v = hz[kind] > 0 ? sinf(2 * (float)M_PI * hz[kind] * i / rate) : ((rand() & 0xffff) / 32768.0f - 1);
        buf[2*i] = buf[2*i+1] = (Sint16)(v * env * 9000);
    }
    gen_buf[kind] = buf;
    return Mix_QuickLoad_RAW((Uint8 *)buf, n * 2 * sizeof(Sint16));
}

void sfx_init(const char *skin_dir, const char *map_dir) {
    Mix_AllocateChannels(NCH);
    snprintf(g_skin, sizeof g_skin, "%s", skin_dir ? skin_dir : "");
    snprintf(g_map, sizeof g_map, "%s", map_dir ? map_dir : "");
    ncache = next_ch = 0;
    for (int k = 0; k < NK; k++) gen[k] = make_tone(k);
    miss_c = try_name(g_skin, "combobreak");
    if (!miss_c) miss_c = try_name(g_map, "combobreak");
    { /* fallback: short falling thud */
        int rate = 44100, n = rate * 220 / 1000;
        miss_buf = malloc(n * 2 * sizeof(Sint16));
        float ph = 0;
        for (int i = 0; i < n; i++) {
            float k = (float)i / n, hz = 180 - 110 * k; ph += 2 * (float)M_PI * hz / rate;
            miss_buf[2*i] = miss_buf[2*i+1] = (Sint16)(sinf(ph) * expf(-5 * k) * 14000);
        }
        miss_gen = Mix_QuickLoad_RAW((Uint8 *)miss_buf, n * 2 * sizeof(Sint16));
    }
}

static int norm_idx(int idx) { return idx > 1 ? idx : 0; }

/* lazer: for each source (beatmap, then skin) try "<name><N>" then "<name>"; first hit wins */
static Mix_Chunk *lookup(int set, int kind, int idx) {
    idx = norm_idx(idx);
    for (int i = 0; i < ncache; i++)
        if (cache[i].set == set && cache[i].kind == kind && cache[i].idx == idx && !cache[i].file[0]) return cache[i].c;
    char base[80], num[96];
    snprintf(base, sizeof base, "%s-%s", SETS[set - 1], KINDS[kind]);
    snprintf(num, sizeof num, "%s%d", base, idx);
    const char *dirs[2] = { g_map, g_skin };
    Mix_Chunk *c = NULL;
    for (int d = 0; d < 2 && !c; d++) {
        if (idx) c = try_name(dirs[d], num);
        if (!c) c = try_name(dirs[d], base);
    }
    if (ncache < MAXE) { Entry *e = &cache[ncache++]; memset(e, 0, sizeof *e); e->set = set; e->kind = kind; e->idx = idx; e->c = c; }
    else if (c) { Mix_FreeChunk(c); c = NULL; }
    return c;
}

/* hitSample filename override: looked up in the beatmap folder only, name may or may not carry an extension */
static Mix_Chunk *lookup_file(const char *file) {
    for (int i = 0; i < ncache; i++) if (cache[i].file[0] && !strcmp(cache[i].file, file)) return cache[i].c;
    char base[40]; snprintf(base, sizeof base, "%s", file);
    char *dot = strrchr(base, '.');
    Mix_Chunk *c = NULL;
    if (dot) { char p[1300]; snprintf(p, sizeof p, "%s/%s", g_map, file); c = Mix_LoadWAV(p); *dot = 0; }
    if (!c) c = try_name(g_map, base);
    if (ncache < MAXE) { Entry *e = &cache[ncache++]; memset(e, 0, sizeof *e); snprintf(e->file, sizeof e->file, "%s", file); e->c = c; }
    else if (c) { Mix_FreeChunk(c); c = NULL; }
    return c;
}

void sfx_prepare(const Beatmap *bm) {
    for (int i = 0; i < bm->n; i++) {
        const Obj *o = &bm->objs[i];
        if (o->sfile[0]) lookup_file(o->sfile);
        lookup(o->nset, 0, o->idx);
        if (o->type) {
            lookup(o->nset, 4, o->idx);
            for (int n = 0; n <= o->slides; n++) {
                lookup(o->nns[n], 0, o->idx);
                for (int k = 1; k < 4; k++) if (o->nhs[n] & (1 << k)) lookup(o->nas[n], k, o->idx);
            }
        }
        for (int k = 1; k < 4; k++) if (o->hs & (1 << k)) lookup(o->aset, k, o->idx);
    }
}

static void play(Mix_Chunk *c, int kind, int vol) {
    if (!c) c = gen[kind];
    if (!c) return;
    /* round-robin channel + volume set BEFORE the chunk starts: setting it after Mix_PlayChannel
     * races with the audio thread and the first buffer plays at full volume. */
    int ch = next_ch++ % NCH;
    Mix_Volume(ch, vol * MIX_MAX_VOLUME / 100);
    Mix_PlayChannel(ch, c, 0);
}

void sfx_play(int hs, int nset, int aset, int idx, const char *file, int vol) {
    if (nset < 1 || nset > 3) nset = 1;
    if (aset < 1 || aset > 3) aset = nset;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    Mix_Chunk *c = file && *file ? lookup_file(file) : NULL;
    play(c ? c : lookup(nset, 0, idx), 0, vol);          /* hitnormal always plays (lazer/stable) */
    for (int k = 1; k < 4; k++) if (hs & (1 << k)) play(lookup(aset, k, idx), k, vol);
}

void sfx_tick(int set, int idx, int vol) {
    if (set < 1 || set > 3) set = 1;
    play(lookup(set, 4, idx), 4, vol < 0 ? 0 : vol > 100 ? 100 : vol);
}

/* combo break sound: skin "combobreak" (beatmap folder as second choice), else a generated thud */
void sfx_miss(int vol) {
    if (getenv("AIMLESS_SFXLOG")) fprintf(stderr, "combobreak (%s)\n", miss_c ? "skin file" : "generated");
    Mix_Chunk *c = miss_c ? miss_c : miss_gen;
    if (!c) return;
    int ch = next_ch++ % NCH;
    Mix_Volume(ch, (vol < 0 ? 0 : vol > 100 ? 100 : vol) * MIX_MAX_VOLUME / 100);
    Mix_PlayChannel(ch, c, 0);
}

void sfx_free(void) {
    Mix_HaltChannel(-1);
    for (int i = 0; i < ncache; i++) if (cache[i].c) Mix_FreeChunk(cache[i].c);
    ncache = 0;
    for (int k = 0; k < NK; k++) { if (gen[k]) Mix_FreeChunk(gen[k]); gen[k] = NULL; free(gen_buf[k]); gen_buf[k] = NULL; }
    if (miss_c) Mix_FreeChunk(miss_c);
    if (miss_gen) Mix_FreeChunk(miss_gen);
    free(miss_buf);
    miss_c = miss_gen = NULL; miss_buf = NULL;
}
