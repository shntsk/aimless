#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    /* input / aim */
    float assist;           /* hit radius in circle radii; 0 = anywhere; 1 = no assist */
    int   input_delay;      /* ms compensation: judge your hits this many ms earlier (-100..100) */
    /* video */
    int   vsync;
    int   fps_limit;        /* when vsync is off: 0 = unlimited, else 60/120/240/480 */
    int   fullscreen;       /* borderless fullscreen */
    int   bg_dim;           /* 0..100 % of black over the beatmap background */
    int   show_bg;          /* draw the beatmap background in gameplay */
    int   bg_blur;          /* 0..100, blur of the gameplay background */
    float cursor_size;      /* 0.3..3 */
    int   cursor_trail;     /* osu!standard: cursor trail */
    int   hit_lighting;     /* flash on every hit */
    int   border;           /* osu!standard playfield border: 0 none, 1 corners, 2 full */
    /* audio */
    int   master_vol;       /* 0..100 */
    int   music_vol;        /* 0..100 */
    int   sfx_vol;          /* 0..100 */
    /* HUD */
    int   show_combo, show_score, show_acc, show_fps, show_input_delay, show_error_bar;
    /* not persisted by the menu, filled by --songs/--skins or settings.ini */
    int   sort_mode, group_mode; /* song select: SORT_*, GROUP_* */
    char  last_map[600];    /* last played .osu, reselected on start */
    char  skin[128];        /* selected skin folder name, empty = built-in */
    char  songs_extra[512];
    char  skins_extra[512];
    char  status[256];      /* last result / import message shown in the menu */
    unsigned mods;          /* active gameplay mods (MOD_* in mods.h); chosen in the song select, kept for the session, not saved */
} Settings;

void settings_defaults(Settings *s);
/* returns 1 if a settings file existed and was read, 0 if defaults are in use */
int  settings_load(Settings *s);
void settings_save(const Settings *s);
void settings_clamp(Settings *s);
const char *settings_path(void);

#endif
