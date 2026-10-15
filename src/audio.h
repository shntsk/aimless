#ifndef AUDIO_H
#define AUDIO_H
/* Hitsounds: loads <set>-hit{normal,whistle,finish,clap}.{wav,ogg,mp3} from the beatmap
 * folder, then the skin folder, then falls back to generated sounds. */
void sfx_init(const char *skin_dir, const char *map_dir);
void sfx_play(int hs, int nset, int aset, int vol);
void sfx_free(void);
#endif
