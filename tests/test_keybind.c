/* src/keybind.c + the K1 / K2 part of src/settings.c: defaults, validation, conflict rules, settings.ini round trip.
 * No SDL, no window. The capture flow itself (ui.c) and the key handling in gameplay need SDL; see README "Tests". */
#include "../src/keybind.h"
#include "../src/settings.h"
#include "../src/paths.h"
#include "../src/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void write_ini(const char *text) {
    FILE *f = plat_fopen(settings_path(), "w");
    if (!f) { fails++; printf("FAIL cannot write %s\n", settings_path()); return; }
    fputs(text, f); fclose(f);
}

static Settings fresh(void) { Settings s; memset(&s, 0, sizeof s); settings_defaults(&s); return s; }

int main(void) {
    /* ---- validation ---- */
    CHECK(keybind_usable(KB_SC_Z) && keybind_usable(KB_SC_X) && keybind_usable(4) && keybind_usable(44) /* space */ &&
          keybind_usable(225) /* left shift */ && keybind_usable(79) /* right arrow */, "ordinary keys must be usable");
    CHECK(!keybind_usable(0) && !keybind_usable(-5) && !keybind_usable(3) && !keybind_usable(232) && !keybind_usable(512) && !keybind_usable(100000), "range");
    CHECK(!keybind_usable(KB_SC_ESCAPE) && !keybind_usable(KB_SC_LGUI) && !keybind_usable(KB_SC_RGUI) && !keybind_usable(KB_SC_CAPSLOCK) &&
          !keybind_usable(KB_SC_NUMLOCK) && !keybind_usable(KB_SC_SCROLLLOCK) && !keybind_usable(KB_SC_PRINTSCREEN) && !keybind_usable(KB_SC_PAUSE) &&
          !keybind_usable(KB_SC_APPLICATION) && !keybind_usable(KB_SC_POWER), "reserved keys must be refused");

    CHECK(keybind_check(KB_SC_ESCAPE, KB_SC_X) == KB_CANCEL, "Esc cancels");
    CHECK(keybind_check(KB_SC_ESCAPE, KB_SC_ESCAPE) == KB_CANCEL, "Esc cancels even against a bogus other key");
    CHECK(keybind_check(20 /* Q */, KB_SC_X) == KB_OK, "free key accepted");
    CHECK(keybind_check(KB_SC_X, KB_SC_X) == KB_CONFLICT, "same key as the other one is a conflict");
    CHECK(keybind_check(KB_SC_Z, KB_SC_X) == KB_OK, "re-assigning your own current key is fine (other differs)");
    CHECK(keybind_check(0, KB_SC_X) == KB_REJECT && keybind_check(KB_SC_LGUI, KB_SC_X) == KB_REJECT, "unusable key rejected");

    CHECK(keybind_is_hit(KB_SC_Z, KB_SC_Z, KB_SC_X) && keybind_is_hit(KB_SC_X, KB_SC_Z, KB_SC_X), "default keys hit");
    CHECK(!keybind_is_hit(20, KB_SC_Z, KB_SC_X), "other key does not hit");
    CHECK(keybind_is_hit(20, 20, 7) && keybind_is_hit(7, 20, 7) && !keybind_is_hit(KB_SC_Z, 20, 7) && !keybind_is_hit(KB_SC_X, 20, 7), "rebinding replaces Z / X");

    /* ---- sanitize ---- */
    int a, b;
    a = 20; b = 7;                   keybind_sanitize(&a, &b); CHECK(a == 20 && b == 7, "valid pair kept: %d %d", a, b);
    a = 0; b = 7;                    keybind_sanitize(&a, &b); CHECK(a == KB_SC_Z && b == 7, "bad K1 -> Z: %d %d", a, b);
    a = 20; b = 999;                 keybind_sanitize(&a, &b); CHECK(a == 20 && b == KB_SC_X, "bad K2 -> X: %d %d", a, b);
    a = 20; b = 20;                  keybind_sanitize(&a, &b); CHECK(a == 20 && b == KB_SC_X, "equal pair: K2 -> X: %d %d", a, b);
    a = KB_SC_X; b = KB_SC_X;        keybind_sanitize(&a, &b); CHECK(a == KB_SC_X && b == KB_SC_Z, "equal pair on X: K2 -> Z: %d %d", a, b);
    a = KB_SC_ESCAPE; b = KB_SC_ESCAPE; keybind_sanitize(&a, &b); CHECK(a == KB_SC_Z && b == KB_SC_X, "Esc pair -> defaults: %d %d", a, b);
    a = KB_SC_X; b = KB_SC_Z;        keybind_sanitize(&a, &b); CHECK(a == KB_SC_X && b == KB_SC_Z, "swapped defaults are valid: %d %d", a, b);

    /* ---- settings.ini ---- */
    char dd[600];
    plat_setenv("XDG_DATA_HOME", "keybind_test_data");        /* data_dir() reads this once */
    snprintf(dd, sizeof dd, "%s", data_dir());
    CHECK(strstr(dd, "keybind_test_data") != NULL, "data_dir not redirected: %s", dd);
    plat_remove_tree("keybind_test_data");
    CHECK(plat_mkdir_p(dd) == 0, "mkdir %s", dd);

    Settings s = fresh();
    CHECK(s.key1 == KB_SC_Z && s.key2 == KB_SC_X, "fresh defaults are Z / X: %d %d", s.key1, s.key2);

    CHECK(settings_load(&s) == 0, "no file yet");
    CHECK(s.key1 == KB_SC_Z && s.key2 == KB_SC_X, "no settings.ini: Z / X");

    write_ini("assist = 2.5\nvsync = 1\n");                                  /* an older file without key1 / key2 */
    s = fresh(); CHECK(settings_load(&s) == 1, "load old file");
    CHECK(s.key1 == KB_SC_Z && s.key2 == KB_SC_X && s.vsync == 1, "old file keeps Z / X and its other values: %d %d vsync %d", s.key1, s.key2, s.vsync);

    s = fresh(); s.key1 = 20; s.key2 = 7; s.bg_dim = 33;
    settings_save(&s);
    Settings r = fresh(); CHECK(settings_load(&r) == 1, "reload");
    CHECK(r.key1 == 20 && r.key2 == 7 && r.bg_dim == 33, "round trip: %d %d dim %d", r.key1, r.key2, r.bg_dim);

    write_ini("key1 = 20\n");                                                /* only K1 stored: K2 keeps its default */
    s = fresh(); settings_load(&s); CHECK(s.key1 == 20 && s.key2 == KB_SC_X, "only key1: %d %d", s.key1, s.key2);
    write_ini("key2 = 29\n");                                                /* K2 = Z while K1 defaults to Z: conflict -> K2 back to X */
    s = fresh(); settings_load(&s); CHECK(s.key1 == KB_SC_Z && s.key2 != s.key1 && keybind_usable(s.key2), "key2 = Z clash: %d %d", s.key1, s.key2);
    write_ini("key1 = 5\nkey2 = 5\n");
    s = fresh(); settings_load(&s); CHECK(s.key1 == 5 && s.key2 == KB_SC_X, "hand-edited duplicate: %d %d", s.key1, s.key2);
    write_ini("key1 = abc\nkey2 = 41\n");                                    /* garbage and Esc */
    s = fresh(); settings_load(&s); CHECK(s.key1 == KB_SC_Z && s.key2 == KB_SC_X, "garbage / Esc fall back: %d %d", s.key1, s.key2);
    write_ini("key1 = -3\nkey2 = 99999\n");
    s = fresh(); settings_load(&s); CHECK(s.key1 == KB_SC_Z && s.key2 == KB_SC_X, "out of range fall back: %d %d", s.key1, s.key2);

    /* a saved file always loads back to a valid, distinct pair, and writes both keys */
    s = fresh(); s.key1 = 20; s.key2 = 20; settings_clamp(&s);
    CHECK(s.key1 != s.key2, "clamp must separate equal keys");
    settings_save(&s);
    { FILE *f = plat_fopen(settings_path(), "r"); char buf[4096] = ""; size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; buf[n] = 0; if (f) fclose(f);
      CHECK(strstr(buf, "\nkey1 = ") && strstr(buf, "\nkey2 = "), "settings.ini lacks key1 / key2"); }

    plat_remove_tree("keybind_test_data");
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_keybind: ok\n");
    return 0;
}
