#ifndef MODS_H
#define MODS_H
#include "beatmap.h"

/* Gameplay modifiers. Only mods that this build can really apply are listed here; anything else (Double Time,
 * Half Time, Hidden, Flashlight, ...) is deliberately absent, see mods_unsupported(). Pure C, no SDL, unit-tested. */
enum { MOD_EASY = 1 << 0, MOD_HARDROCK = 1 << 1 };
#define MODS_ALL (MOD_EASY | MOD_HARDROCK)

typedef struct { int bit; const char *name, *acronym, *desc; } ModInfo;

int  mods_count(void);
const ModInfo *mods_info(int i);
/* Mods that cannot be combined with `mod` (Easy <-> Hard Rock). */
unsigned mods_conflicts(unsigned mod);
/* Turns `mod` on or off in `mods`; turning one on switches off the mods it conflicts with. Unknown bits are ignored. */
unsigned mods_toggle(unsigned mods, unsigned mod);
/* Drops unknown bits and, if conflicting mods are set together, keeps only the lowest one. */
unsigned mods_sanitize(unsigned mods);
/* number of active mods */
int mods_active_count(unsigned mods);
/* "EZ+HR"-style summary into out; "" when no mod is active. */
void mods_acronyms(unsigned mods, char *out, int n);
/* Names of the lazer mods this build cannot apply (shown as a note, never as controls). */
const char *mods_unsupported(void);

/* lazer's difficulty adjustments: Easy halves CS/AR/OD/HP; Hard Rock multiplies CS by 1.3 and AR/OD/HP by 1.4, all capped at 10. */
void mods_adjust(unsigned mods, float *cs, float *ar, float *od, float *hp);
/* Applies the mods to a loaded beatmap: difficulty values, and Hard Rock's vertical flip of every object and slider path
 * (osu! playfield height 384). Call once per load; it is not idempotent. */
void mods_apply_beatmap(Beatmap *b, unsigned mods);

#endif
