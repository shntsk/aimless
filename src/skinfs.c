/* Case-insensitive, sandboxed file lookup for skin folders. See skinfs.h. */
#include "skinfs.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

int skinfs_isdir(const char *path) {
    struct stat st;
    return path && *path && !stat(path, &st) && S_ISDIR(st.st_mode);
}

static int is_file(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode);
}

/* Find an entry of dir whose name equals comp ignoring case; copies the real name into out. */
static int find_ci(const char *dir, const char *comp, char *out, size_t n) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d))) {
        if (!strcasecmp(e->d_name, comp)) {
            /* when two names differ only by case, keep scanning only if this one is not an exact match */
            snprintf(out, n, "%s", e->d_name);
            found = 1;
            if (!strcmp(e->d_name, comp)) break;
        }
    }
    closedir(d);
    return found;
}

int skinfs_resolve(const char *dir, const char *base, const char *suffix, char *out, size_t n) {
    char rel[512];
    if (!dir || !*dir || !base || !*base) return 0;
    if (snprintf(rel, sizeof rel, "%s%s", base, suffix ? suffix : "") >= (int)sizeof rel) return 0;
    for (char *p = rel; *p; p++) if (*p == '\\') *p = '/';
    if (rel[0] == '/') return 0;

    /* fast path: exact spelling */
    char p[1600];
    if (snprintf(p, sizeof p, "%s/%s", dir, rel) < (int)sizeof p) {
        int ok = 1;
        for (const char *c = rel; *c; ) {            /* reject ".." components before touching the disk */
            const char *e = strchr(c, '/'); size_t l = e ? (size_t)(e - c) : strlen(c);
            if (l == 2 && c[0] == '.' && c[1] == '.') ok = 0;
            c += l; if (*c == '/') c++;
        }
        if (!ok) return 0;
        if (is_file(p)) { snprintf(out, n, "%s", p); return 1; }
    } else return 0;

    /* slow path: walk component by component, matching names ignoring case */
    char cur[1600]; snprintf(cur, sizeof cur, "%s", dir);
    char *s = rel;
    while (*s) {
        char *e = strchr(s, '/');
        if (e) *e = 0;
        if (*s && strcmp(s, ".")) {
            char real[300];
            if (!find_ci(cur, s, real, sizeof real)) return 0;
            size_t l = strlen(cur);
            if (snprintf(cur + l, sizeof cur - l, "/%s", real) >= (int)(sizeof cur - l)) return 0;
        }
        if (!e) break;
        s = e + 1;
    }
    if (!is_file(cur)) return 0;
    snprintf(out, n, "%s", cur);
    return 1;
}

int skinfs_image_candidate(const char *dir, const char *name, int idx, int *scale, char *out, size_t n) {
    static const char *SUF[SKINFS_N_IMG] = { "@2x.png", "@2x.jpg", ".png", ".jpg" };
    if (idx < 0 || idx >= SKINFS_N_IMG) return 0;
    if (!skinfs_resolve(dir, name, SUF[idx], out, n)) return 0;
    if (scale) *scale = idx < 2 ? 2 : 1;
    return 1;
}

static int has_ext(const char *name, const char *ext) {
    size_t a = strlen(name), b = strlen(ext);
    return a > b && !strcasecmp(name + a - b, ext);
}

int skinfs_looks_like_skin(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int ok = 0;
    while (!ok && (e = readdir(d))) {
        const char *nm = e->d_name;
        if (!strcasecmp(nm, "skin.ini") || has_ext(nm, ".png") || has_ext(nm, ".jpg") || has_ext(nm, ".jpeg") ||
            has_ext(nm, ".wav") || has_ext(nm, ".ogg") || has_ext(nm, ".mp3")) ok = 1;
    }
    closedir(d);
    return ok;
}
