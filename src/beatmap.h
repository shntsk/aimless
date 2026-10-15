#ifndef BEATMAP_H
#define BEATMAP_H

typedef struct {
    float x, y;
    int time;
    int num;        /* combo number shown on the circle */
    int color;      /* combo colour index */
    int state;      /* 0 = pending, 1 = hit, 2 = missed */
    int judge;      /* 300 / 100 / 50 / 0 */
    int judge_time; /* song time of the judgement */
    int hs;         /* hitsound bitmask: 2 whistle, 4 finish, 8 clap */
    int nset, aset; /* resolved sample sets: 1 normal, 2 soft, 3 drum */
    int vol;        /* resolved volume 0-100 */
} Obj;

typedef struct { int time, set, vol; } Timing;

typedef struct {
    char path[1024];
    char dir[512];
    char audio[256];
    char title[128], artist[128], version[128];
    int default_set;
    float cs, ar, od;
    Obj *objs;
    int n;
    Timing *tp;
    int ntp;
} Beatmap;

/* meta_only: skip hit objects (fast, for song lists) */
int  beatmap_load(Beatmap *b, const char *path, int meta_only);
void beatmap_free(Beatmap *b);

#endif
