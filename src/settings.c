/* settings.ini: simple `key = value` file in the data dir. Unknown keys are ignored. */
#include "settings.h"
#include "keybind.h"
#include "paths.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *settings_path(void) {
    static char p[600];
    snprintf(p, sizeof p, "%s/settings.ini", data_dir());
    return p;
}

void settings_defaults(Settings *s) {
    char songs[512], skins[512], st[256];
    memcpy(songs, s->songs_extra, sizeof songs); memcpy(skins, s->skins_extra, sizeof skins); memcpy(st, s->status, sizeof st);
    memset(s, 0, sizeof *s);
    s->assist = 2.5f;
    s->key1 = KB_SC_Z; s->key2 = KB_SC_X;
    s->bg_dim = 70; s->show_bg = 1;
    s->cursor_size = 1.0f;
    s->master_vol = 80; s->music_vol = 80; s->sfx_vol = 100;
    s->cursor_trail = 1; s->hit_lighting = 1; s->show_error_bar = 1;
    s->show_combo = 1; s->show_score = 1; s->show_acc = 1;
    memcpy(s->songs_extra, songs, sizeof songs); memcpy(s->skins_extra, skins, sizeof skins); memcpy(s->status, st, sizeof st);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void settings_clamp(Settings *s) {
    s->bg_dim = clampi(s->bg_dim, 0, 100);
    s->sort_mode = clampi(s->sort_mode, 0, 5);
    s->group_mode = clampi(s->group_mode, 0, 3);
    s->master_vol = clampi(s->master_vol, 0, 100);
    s->bg_blur = clampi(s->bg_blur, 0, 100);
    s->border = clampi(s->border, 0, 2);
    if (s->fps_limit != 60 && s->fps_limit != 120 && s->fps_limit != 240 && s->fps_limit != 480) s->fps_limit = 0;
    s->music_vol = clampi(s->music_vol, 0, 100);
    s->sfx_vol = clampi(s->sfx_vol, 0, 100);
    s->input_delay = clampi(s->input_delay, -100, 100);
    if (s->cursor_size < 0.3f) s->cursor_size = 0.3f;
    if (s->cursor_size > 3.0f) s->cursor_size = 3.0f;
    if (s->assist < 0) s->assist = 0;
    keybind_sanitize(&s->key1, &s->key2);
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

int settings_load(Settings *s) {
    FILE *f = plat_fopen(settings_path(), "r");
    if (!f) return 0;
    char line[1100];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcmp(k, "assist")) s->assist = (float)atof(v);
        else if (!strcmp(k, "input_delay")) s->input_delay = atoi(v);
        else if (!strcmp(k, "key1")) s->key1 = atoi(v);
        else if (!strcmp(k, "key2")) s->key2 = atoi(v);
        else if (!strcmp(k, "vsync")) s->vsync = atoi(v) != 0;
        else if (!strcmp(k, "bg_dim")) s->bg_dim = atoi(v);
        else if (!strcmp(k, "show_bg")) s->show_bg = atoi(v) != 0;
        else if (!strcmp(k, "cursor_size")) s->cursor_size = (float)atof(v);
        else if (!strcmp(k, "fps_limit")) s->fps_limit = atoi(v);
        else if (!strcmp(k, "fullscreen")) s->fullscreen = atoi(v) != 0;
        else if (!strcmp(k, "master_vol")) s->master_vol = atoi(v);
        else if (!strcmp(k, "bg_blur")) s->bg_blur = atoi(v);
        else if (!strcmp(k, "cursor_trail")) s->cursor_trail = atoi(v) != 0;
        else if (!strcmp(k, "hit_lighting")) s->hit_lighting = atoi(v) != 0;
        else if (!strcmp(k, "border")) s->border = atoi(v);
        else if (!strcmp(k, "show_error_bar")) s->show_error_bar = atoi(v) != 0;
        else if (!strcmp(k, "music_vol")) s->music_vol = atoi(v);
        else if (!strcmp(k, "sfx_vol")) s->sfx_vol = atoi(v);
        else if (!strcmp(k, "show_combo")) s->show_combo = atoi(v) != 0;
        else if (!strcmp(k, "show_score")) s->show_score = atoi(v) != 0;
        else if (!strcmp(k, "show_acc")) s->show_acc = atoi(v) != 0;
        else if (!strcmp(k, "show_fps")) s->show_fps = atoi(v) != 0;
        else if (!strcmp(k, "show_input_delay")) s->show_input_delay = atoi(v) != 0;
        else if (!strcmp(k, "sort_mode")) s->sort_mode = atoi(v);
        else if (!strcmp(k, "group_mode")) s->group_mode = atoi(v);
        else if (!strcmp(k, "last_map")) snprintf(s->last_map, sizeof s->last_map, "%s", v);
        else if (!strcmp(k, "skin")) snprintf(s->skin, sizeof s->skin, "%s", v);
        else if (!strcmp(k, "songs_dir")) snprintf(s->songs_extra, sizeof s->songs_extra, "%s", v);
        else if (!strcmp(k, "skins_dir")) snprintf(s->skins_extra, sizeof s->skins_extra, "%s", v);
    }
    fclose(f);
    settings_clamp(s);
    return 1;
}

void settings_save(const Settings *s) {
    FILE *f = plat_fopen(settings_path(), "w");
    if (!f) return;
    fprintf(f,
        "# aimless settings (edited by the game, safe to edit by hand)\n"
        "assist = %g            # 2.5 soft silent aim, 0 anywhere, 1 normal aim\n"
        "input_delay = %d       # ms; positive = judge your hits earlier (compensates lag)\n"
        "key1 = %d             # gameplay key K1: SDL scancode (physical key, same on every layout); 29 = Z\n"
        "key2 = %d             # gameplay key K2: SDL scancode; 27 = X. Missing or invalid values fall back to Z / X\n"
        "vsync = %d\n"
        "fps_limit = %d         # used when vsync = 0: 0 unlimited, 60, 120, 240, 480\n"
        "fullscreen = %d\n"
        "master_vol = %d\n"
        "bg_blur = %d           # 0-100\n"
        "cursor_trail = %d\n"
        "hit_lighting = %d\n"
        "border = %d            # playfield border: 0 none, 1 corners, 2 full\n"
        "show_error_bar = %d    # hit error meter\n"
        "bg_dim = %d            # 0-100, darkness over the beatmap background\n"
        "show_bg = %d\n"
        "cursor_size = %g\n"
        "music_vol = %d\n"
        "sfx_vol = %d\n"
        "show_combo = %d\n"
        "show_score = %d\n"
        "show_acc = %d\n"
        "show_fps = %d\n"
        "show_input_delay = %d  # measured input lag + compensation on screen\n"
        "sort_mode = %d         # song select: 0 artist, 1 title, 2 date added, 3 difficulty, 4 length, 5 bpm\n"
        "group_mode = %d        # 0 none, 1 artist, 2 title, 3 difficulty\n"
        "last_map = %s\n"
        "skin = %s\n"
        "songs_dir = %s\n"
        "skins_dir = %s\n",
        s->assist, s->input_delay, s->key1, s->key2, s->vsync, s->fps_limit, s->fullscreen, s->master_vol, s->bg_blur, s->cursor_trail, s->hit_lighting, s->border, s->show_error_bar, s->bg_dim, s->show_bg, s->cursor_size, s->music_vol, s->sfx_vol,
        s->show_combo, s->show_score, s->show_acc, s->show_fps, s->show_input_delay, s->sort_mode, s->group_mode, s->last_map, s->skin, s->songs_extra, s->skins_extra);
    fclose(f);
}
