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
    int idx;        /* resolved custom sample index (<=1 = no suffix, N = "-hitnormalN") */
    char sfile[40]; /* hitSample filename override (replaces hitnormal), may be empty */
    /* sliders (type 1) */
    int type;       /* 0 circle, 1 slider */
    int slides, endtime;
    float length;
    float *path; int npath; float step; /* uniformly resampled centre line in osu px */
    int *ev; int nev;                   /* (time,kind) pairs: 0 tick, 1 reverse, 2 end */
    int *nhs, *nns, *nas;               /* per node (slides+1): hitsound bits, normal set, addition set */
    float *cp; int ncp; char curve;     /* raw control points, freed after the path is built */
    int evi, ev_hit, ev_node, done, trk; /* runtime state */
} Obj;

typedef struct { int time, set, vol, idx, uninh; float beat; } Timing;

typedef struct {
    char path[1024];
    char dir[512];
    char audio[256];
    char bg[256];       /* background image file from [Events], may be empty */
    char title[128], artist[128], version[128];
    int default_set;
    float cs, ar, od, sm, tickrate;
    int last_end;       /* time the last object finishes */
    Obj *objs;
    int n;
    Timing *tp;
    int ntp;
} Beatmap;

/* meta_only: skip hit objects (fast, for song lists) */
int  beatmap_load(Beatmap *b, const char *path, int meta_only);
void beatmap_free(Beatmap *b);

#endif
