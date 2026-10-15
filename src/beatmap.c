/* Minimal .osu parser. Sliders are treated as circles (head only), spinners skipped. */
#include "beatmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
}

int beatmap_load(Beatmap *b, const char *path) {
    memset(b, 0, sizeof *b);
    b->cs = 4; b->ar = 5; b->od = 5;
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    const char *slash = strrchr(path, '/');
    if (slash) { size_t n = slash - path; if (n > 500) n = 500; memcpy(b->dir, path, n); b->dir[n] = 0; }
    else strcpy(b->dir, ".");

    int cap = 256, sec = 0, num = 0, color = -1;
    b->objs = malloc(cap * sizeof(Obj));
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '[') {
            sec = !strcmp(line, "[General]") ? 1 : !strcmp(line, "[Difficulty]") ? 2 :
                  !strcmp(line, "[HitObjects]") ? 3 : 0;
            continue;
        }
        if (sec == 1 && !strncmp(line, "AudioFilename:", 14)) {
            char *v = line + 14; while (*v == ' ') v++;
            snprintf(b->audio, sizeof b->audio, "%s", v);
        } else if (sec == 2) {
            if (!strncmp(line, "CircleSize:", 11)) b->cs = atof(line + 11);
            else if (!strncmp(line, "ApproachRate:", 13)) b->ar = atof(line + 13);
            else if (!strncmp(line, "OverallDifficulty:", 18)) b->od = atof(line + 18);
        } else if (sec == 3 && line[0]) {
            float x, y; int t, type;
            if (sscanf(line, "%f,%f,%d,%d", &x, &y, &t, &type) != 4) continue;
            if (type & 8) continue; /* spinner */
            if (type & 4 || color < 0) { num = 0; color++; }
            num++;
            if (b->n == cap) { cap *= 2; b->objs = realloc(b->objs, cap * sizeof(Obj)); }
            b->objs[b->n++] = (Obj){ x, y, t, num, color, 0, 0, 0 };
        }
    }
    fclose(f);
    return b->n ? 0 : -2;
}

void beatmap_free(Beatmap *b) { free(b->objs); b->objs = NULL; b->n = 0; }
