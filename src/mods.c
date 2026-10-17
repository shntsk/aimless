#include "mods.h"
#include <stdio.h>
#include <string.h>

static const ModInfo MODS[] = {
    { MOD_EASY,     "Easy",      "EZ", "Smaller playfield pressure: CS, AR, OD and HP are halved" },
    { MOD_HARDROCK, "Hard Rock", "HR", "CS x1.3, AR/OD/HP x1.4 (max 10) and the playfield is flipped vertically" },
};
#define NMODS ((int)(sizeof MODS / sizeof *MODS))

int mods_count(void) { return NMODS; }
const ModInfo *mods_info(int i) { return i >= 0 && i < NMODS ? &MODS[i] : NULL; }

unsigned mods_conflicts(unsigned mod) {
    unsigned c = 0;
    if (mod & MOD_EASY) c |= MOD_HARDROCK;
    if (mod & MOD_HARDROCK) c |= MOD_EASY;
    return c;
}

unsigned mods_sanitize(unsigned m) {
    m &= MODS_ALL;
    if ((m & MOD_EASY) && (m & MOD_HARDROCK)) m &= ~(unsigned)MOD_HARDROCK;
    return m;
}

unsigned mods_toggle(unsigned m, unsigned mod) {
    mod &= MODS_ALL;
    if (!mod) return mods_sanitize(m);
    if (m & mod) return mods_sanitize(m & ~mod);
    return mods_sanitize((m & ~mods_conflicts(mod)) | mod);
}

int mods_active_count(unsigned m) {
    int n = 0;
    for (int i = 0; i < NMODS; i++) if (m & MODS[i].bit) n++;
    return n;
}

void mods_acronyms(unsigned m, char *out, int n) {
    if (n <= 0) return;
    out[0] = 0;
    for (int i = 0; i < NMODS; i++) if (m & MODS[i].bit) {
        size_t l = strlen(out);
        snprintf(out + l, (size_t)n - l, "%s%s", l ? "+" : "", MODS[i].acronym);
    }
}

const char *mods_unsupported(void) { return "Double Time, Half Time, Hidden, Flashlight, No Fail"; }

static float cap10(float v) { return v > 10 ? 10 : v; }

void mods_adjust(unsigned m, float *cs, float *ar, float *od, float *hp) {
    m = mods_sanitize(m);
    if (m & MOD_EASY) { *cs *= 0.5f; *ar *= 0.5f; *od *= 0.5f; *hp *= 0.5f; }
    else if (m & MOD_HARDROCK) { *cs = cap10(*cs * 1.3f); *ar = cap10(*ar * 1.4f); *od = cap10(*od * 1.4f); *hp = cap10(*hp * 1.4f); }
}

void mods_apply_beatmap(Beatmap *b, unsigned m) {
    m = mods_sanitize(m);
    if (!m) return;
    float hp = 0;   /* the Beatmap struct carries no HP (there is no health bar in this game) */
    mods_adjust(m, &b->cs, &b->ar, &b->od, &hp);
    if (m & MOD_HARDROCK) {
        for (int i = 0; i < b->n; i++) {
            Obj *o = &b->objs[i];
            o->y = 384.0f - o->y;
            for (int k = 0; k < o->npath; k++) o->path[k * 2 + 1] = 384.0f - o->path[k * 2 + 1];
        }
    }
}
