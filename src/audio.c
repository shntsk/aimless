#include "audio.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *SETS[]  = { "normal", "soft", "drum" };
static const char *KINDS[] = { "hitnormal", "hitwhistle", "hitfinish", "hitclap" };
static Mix_Chunk *snd[3][4];
static Mix_Chunk *gen[4];
static Sint16 *gen_buf[4];

static Mix_Chunk *try_dir(const char *dir, const char *set, const char *kind) {
    static const char *EXT[] = { "wav", "ogg", "mp3" };
    char p[1100];
    if (!dir) return NULL;
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s-%s.%s", dir, set, kind, EXT[i]);
        Mix_Chunk *c = Mix_LoadWAV(p);
        if (c) return c;
    }
    return NULL;
}

static Mix_Chunk *make_tone(int kind) {
    int rate = 44100, ms[4] = { 45, 130, 260, 90 };
    float hz[4] = { 1100, 2600, 0, 0 };
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
    Mix_AllocateChannels(32);
    for (int k = 0; k < 4; k++) gen[k] = make_tone(k);
    for (int s = 0; s < 3; s++) for (int k = 0; k < 4; k++) {
        Mix_Chunk *c = try_dir(map_dir, SETS[s], KINDS[k]);
        if (!c) c = try_dir(skin_dir, SETS[s], KINDS[k]);
        snd[s][k] = c; /* NULL -> generated fallback at play time */
    }
}

static void play(int set, int kind, int vol) {
    if (set < 1 || set > 3) set = 1;
    Mix_Chunk *c = snd[set - 1][kind];
    if (!c) c = gen[kind];
    if (!c) return;
    int ch = Mix_PlayChannel(-1, c, 0);
    if (ch >= 0) Mix_Volume(ch, vol * MIX_MAX_VOLUME / 100);
}

void sfx_play(int hs, int nset, int aset, int vol) {
    play(nset, 0, vol);
    if (hs & 2) play(aset, 1, vol);
    if (hs & 4) play(aset, 2, vol);
    if (hs & 8) play(aset, 3, vol);
}

void sfx_free(void) {
    Mix_HaltChannel(-1);
    for (int s = 0; s < 3; s++) for (int k = 0; k < 4; k++) if (snd[s][k]) { Mix_FreeChunk(snd[s][k]); snd[s][k] = NULL; }
    for (int k = 0; k < 4; k++) { if (gen[k]) Mix_FreeChunk(gen[k]); gen[k] = NULL; free(gen_buf[k]); gen_buf[k] = NULL; }
}
