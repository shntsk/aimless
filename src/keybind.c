#include "keybind.h"

int keybind_usable(int sc) {
    if (sc < KB_SC_FIRST || sc > KB_SC_LAST) return 0;
    switch (sc) {
    case KB_SC_ESCAPE:                                  /* cancel / quit */
    case KB_SC_CAPSLOCK: case KB_SC_NUMLOCK: case KB_SC_SCROLLLOCK:   /* toggles: the OS reports them unreliably */
    case KB_SC_PRINTSCREEN: case KB_SC_PAUSE:
    case KB_SC_APPLICATION: case KB_SC_POWER:
    case KB_SC_LGUI: case KB_SC_RGUI:                   /* Windows / Super key opens the OS shell */
        return 0;
    }
    return 1;
}

int keybind_check(int sc, int other) {
    if (sc == KB_SC_ESCAPE) return KB_CANCEL;
    if (!keybind_usable(sc)) return KB_REJECT;
    if (sc == other) return KB_CONFLICT;
    return KB_OK;
}

void keybind_sanitize(int *k1, int *k2) {
    if (!keybind_usable(*k1)) *k1 = KB_SC_Z;
    if (!keybind_usable(*k2)) *k2 = KB_SC_X;
    if (*k1 == *k2) *k2 = *k1 == KB_SC_X ? KB_SC_Z : KB_SC_X;
}

int keybind_is_hit(int sc, int k1, int k2) { return sc == k1 || sc == k2; }
