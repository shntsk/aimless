#ifndef KEYBIND_H
#define KEYBIND_H

/* Gameplay key bindings (K1 / K2). Pure C, no SDL, unit-tested.
 *
 * A binding is stored as an SDL_Scancode value (the physical key position, USB HID usage ids; SDL2 keeps these numbers
 * stable), not as an SDL_Keycode. With keycodes "Z" would move to another physical key on AZERTY / QWERTZ / Dvorak and
 * the pair would no longer sit under the left hand; with scancodes the default pair stays on the same two keys of the
 * keyboard on every layout (the on-screen name still follows the layout, see ui.c). The numbers below are asserted
 * against SDL_SCANCODE_* in game.c and ui.c. */
enum {
    KB_SC_Z = 29, KB_SC_X = 27,
    KB_SC_ESCAPE = 41, KB_SC_CAPSLOCK = 57, KB_SC_PRINTSCREEN = 70, KB_SC_SCROLLLOCK = 71, KB_SC_PAUSE = 72,
    KB_SC_NUMLOCK = 83, KB_SC_APPLICATION = 101, KB_SC_POWER = 102, KB_SC_LGUI = 227, KB_SC_RGUI = 231,
    KB_SC_FIRST = 4,       /* lowest real key (A) */
    KB_SC_LAST  = 231      /* highest keyboard key (right GUI); media / consumer keys above it are not offered */
};

enum {
    KB_OK,        /* usable and different from the other key */
    KB_CANCEL,    /* Escape: leaves the capture, nothing changes */
    KB_REJECT,    /* not a key that can be used in gameplay (lock keys, OS keys, unknown ...) */
    KB_CONFLICT   /* already assigned to the other gameplay key */
};

/* 1 if `sc` may be used as a gameplay key (Escape is not: it is the cancel / quit key). */
int  keybind_usable(int sc);
/* Result of trying to assign `sc` to one key while the other gameplay key is `other`. */
int  keybind_check(int sc, int other);
/* Makes a loaded pair valid: an unusable key falls back to its default, and if both are equal K2 goes back to its default
 * (or to K1's default when K2's default is the taken one). Idempotent. */
void keybind_sanitize(int *k1, int *k2);
/* 1 if the pressed scancode is one of the two gameplay keys. */
int  keybind_is_hit(int sc, int k1, int k2);

#endif
