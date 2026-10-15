#ifndef PATHS_H
#define PATHS_H
#include <stddef.h>

typedef struct { char label[300]; char path[1024]; } Entry;
typedef struct { Entry *e; int n; } List;

const char *data_dir(void);            /* ~/.local/share/aimless */
void ensure_dirs(void);
void songs_dir(char *out, size_t n);
void skins_dir(char *out, size_t n);

/* Import a .osz (song), .osk (skin), or a folder. Writes a human message to msg. */
int  import_path(const char *src, char *msg, size_t n);

/* extra = optional additional root (e.g. your osu!stable Songs/Skins folder), may be "" */
void scan_songs(List *l, const char *extra);
void scan_skins(List *l, const char *extra);
void list_free(List *l);

#endif
