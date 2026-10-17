#ifndef AUDIO_H
#define AUDIO_H
#include "beatmap.h"
/* Hitsounds, resolved like osu!lazer's legacy sample lookup:
 *   <set>-<hitnormal|hitwhistle|hitfinish|hitclap>[N].{wav,ogg,mp3}
 * N = custom sample index (only when > 1; falls back to the unnumbered name).
 * Search order per sample: beatmap folder, then skin folder, then a generated fallback. */
void sfx_init(const char *skin_dir, const char *map_dir);
void sfx_prepare(const Beatmap *bm);   /* preload every sample the map uses (no disk I/O mid-play) */
void sfx_play(int hs, int nset, int aset, int idx, const char *file, int vol);
void sfx_tick(int set, int idx, int vol);   /* slidertick */
void sfx_miss(int vol);                      /* combobreak (skin file or generated) */
void sfx_free(void);
#endif
