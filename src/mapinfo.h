#ifndef MAPINFO_H
#define MAPINFO_H

/* Everything the song select needs to know about one .osu file, gathered in a single cheap pass
 * (no slider curves are built). Plain data, so it can be cached on disk as is. */
typedef struct {
    char title[128], artist[128], creator[96], version[96], source[96], tags[256];
    char bg[192], audio[192];
    float cs, ar, od, hp;
    int nobj, nslider, nspinner;
    int length_ms;      /* time the last object ends */
    int bpm;            /* BPM of the first timing section */
    int preview_ms;     /* PreviewTime, -1 if none */
    float stars;        /* ESTIMATE (strain model, heads only), not the official star rating */
} MapInfo;

int mapinfo_scan(const char *path, MapInfo *m); /* 0 on success */

#endif
