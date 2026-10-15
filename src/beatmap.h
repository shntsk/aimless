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
} Obj;

typedef struct {
    char dir[512];
    char audio[256];
    float cs, ar, od;
    Obj *objs;
    int n;
} Beatmap;

int  beatmap_load(Beatmap *b, const char *path);
void beatmap_free(Beatmap *b);

#endif
