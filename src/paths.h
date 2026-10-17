#ifndef PATHS_H
#define PATHS_H
#include <stddef.h>

typedef struct { char label[400]; char path[1024]; } Entry;
typedef struct { Entry *e; int n; } List;

const char *data_dir(void);            /* ~/.local/share/aimless */
void ensure_dirs(void);
void songs_dir(char *out, size_t n);
void skins_dir(char *out, size_t n);

/* Import a .osz (song), .osk (skin), or a folder. Writes a human message to msg. */
int  import_path(const char *src, char *msg, size_t n);

/* Same, and reports where it was installed (dest_out is "" when nothing was installed). An .osk is extracted
 * into Skins/<name> via a hidden temp folder; an existing skin of that name is kept and the new one gets " (2)". */
int  import_path_ex(const char *src, char *msg, size_t n, char *dest_out, size_t dn);

/* Turn the skin argument of `aimless map.osu <skin>` into a folder: an existing folder is used as is, an
 * .osk file is imported first, a bare name is looked up in Skins/ (and extra). Returns 0 and fills out
 * (out may be "" for no skin), or -1 with msg set and out "". */
int  resolve_skin_arg(const char *arg, const char *extra, char *out, size_t n, char *msg, size_t mn);

/* extra = optional additional root (e.g. your osu!stable Songs/Skins folder), may be "" */
void scan_songs(List *l, const char *extra);
void scan_skins(List *l, const char *extra);
void list_free(List *l);

#endif
