#ifndef LIBRARY_H
#define LIBRARY_H
#include <stddef.h>
#include "mapinfo.h"

typedef struct { char path[1024]; MapInfo mi; long mtime; } Diff;

typedef struct {
    char dir[1024];
    Diff *d; int nd;            /* difficulties, ascending by estimated stars */
    int *vd; int nvd;           /* indices of the difficulties that pass the current search */
    char bg[1100], audio[1100]; /* absolute paths, may be empty */
    int preview_ms;
    float smin, smax;
    int bpm, len;
    long mtime;                 /* "date added": folder modification time */
} MapSet;

typedef struct { MapSet *s; int n, ndiff; } Library;

enum { SORT_ARTIST, SORT_TITLE, SORT_DATE, SORT_DIFF, SORT_LENGTH, SORT_BPM, SORT_N };
enum { GROUP_NONE, GROUP_ARTIST, GROUP_TITLE, GROUP_DIFF, GROUP_N };

typedef void (*ProgressFn)(int done, void *ud);
void lib_scan(Library *l, const char *extra_root, ProgressFn cb, void *ud);
void lib_free(Library *l);
void lib_sort(Library *l, int sort, int group);
int  lib_group_key(const MapSet *s, int group);              /* sortable key */
void lib_group_label(const MapSet *s, int group, char *out, size_t n);

/* search: words match title/artist/creator/difficulty/source/tags; key>value filters like
 * stars>5 ar=9 cs<4 od>=8 hp<5 bpm>=180 length<120 (seconds) */
typedef struct {
    char terms[8][64]; int nt;
    struct { int key; int op; float v; } f[8]; int nf;
} Query;
void query_parse(Query *q, const char *text);
void lib_apply_query(Library *l, const Query *q);

#endif
