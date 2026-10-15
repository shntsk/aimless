#include "paths.h"
#include "beatmap.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

const char *data_dir(void) {
    static char d[512];
    if (d[0]) return d;
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(d, sizeof d, "%s/aimless", x);
    else snprintf(d, sizeof d, "%s/.local/share/aimless", h ? h : ".");
    return d;
}

static void mkdir_p(const char *path) {
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

void songs_dir(char *o, size_t n) { snprintf(o, n, "%s/Songs", data_dir()); }
void skins_dir(char *o, size_t n) { snprintf(o, n, "%s/Skins", data_dir()); }

void ensure_dirs(void) {
    char p[600];
    songs_dir(p, sizeof p); mkdir_p(p);
    skins_dir(p, sizeof p); mkdir_p(p);
}

static int run(char *const argv[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) { execvp(argv[0], argv); _exit(127); }
    int st;
    waitpid(p, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

static int ends_with(const char *s, const char *ext) {
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcasecmp(s + a - b, ext);
}

static int dir_has_osu(const char *dir) {
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int found = 0;
    while ((e = readdir(d))) if (ends_with(e->d_name, ".osu")) { found = 1; break; }
    closedir(d);
    return found;
}

int import_path(const char *src, char *msg, size_t n) {
    struct stat st;
    if (stat(src, &st)) { snprintf(msg, n, "import failed: cannot open file"); return -1; }
    const char *b = strrchr(src, '/'); b = b ? b + 1 : src;
    char name[256]; snprintf(name, sizeof name, "%s", b);
    size_t l = strlen(name);
    while (l && name[l-1] == '/') name[--l] = 0;
    char root[600], dest[900];
    int is_skin = 0, is_dir = S_ISDIR(st.st_mode);
    if (is_dir) is_skin = !dir_has_osu(src);
    else if (ends_with(name, ".osz")) is_skin = 0;
    else if (ends_with(name, ".osk")) is_skin = 1;
    else { snprintf(msg, n, "import failed: use .osz, .osk or a folder"); return -1; }
    char *dot = strrchr(name, '.');
    if (!is_dir && dot) *dot = 0;
    if (is_skin) skins_dir(root, sizeof root); else songs_dir(root, sizeof root);
    snprintf(dest, sizeof dest, "%s/%s", root, name);
    mkdir_p(root);
    int rc;
    if (is_dir) { char *a[] = { "cp", "-rT", (char *)src, dest, NULL }; rc = run(a); }
    else { char *a[] = { "unzip", "-oq", (char *)src, "-d", dest, NULL }; rc = run(a); }
    if (rc) snprintf(msg, n, "import failed (is unzip installed?)");
    else snprintf(msg, n, "imported %s: %s", is_skin ? "skin" : "song", name);
    return rc;
}

static void add(List *l, const char *label, const char *path) {
    l->e = realloc(l->e, (l->n + 1) * sizeof(Entry));
    snprintf(l->e[l->n].label, sizeof l->e[l->n].label, "%s", label);
    snprintf(l->e[l->n].path, sizeof l->e[l->n].path, "%s", path);
    l->n++;
}

static int cmp(const void *a, const void *b) { return strcasecmp(((Entry *)a)->label, ((Entry *)b)->label); }

static void scan_songs_root(List *l, const char *root) {
    DIR *d = opendir(root); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char sub[1100]; snprintf(sub, sizeof sub, "%s/%s", root, e->d_name);
        DIR *sd = opendir(sub); if (!sd) continue;
        struct dirent *f;
        while ((f = readdir(sd))) {
            if (!ends_with(f->d_name, ".osu")) continue;
            char p[2200]; snprintf(p, sizeof p, "%s/%s", sub, f->d_name);
            Beatmap bm;
            if (beatmap_load(&bm, p, 1)) { beatmap_free(&bm); continue; }
            char label[400];
            snprintf(label, sizeof label, "%s - %s [%s]", bm.artist, bm.title, bm.version);
            add(l, label, p);
            beatmap_free(&bm);
        }
        closedir(sd);
    }
    closedir(d);
}

void scan_songs(List *l, const char *extra) {
    char r[600]; songs_dir(r, sizeof r);
    scan_songs_root(l, r);
    if (extra && *extra) scan_songs_root(l, extra);
    qsort(l->e, l->n, sizeof(Entry), cmp);
}

static void scan_skins_root(List *l, const char *root) {
    DIR *d = opendir(root); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[1100]; snprintf(p, sizeof p, "%s/%s", root, e->d_name);
        struct stat st; if (stat(p, &st) || !S_ISDIR(st.st_mode)) continue;
        add(l, e->d_name, p);
    }
    closedir(d);
}

void scan_skins(List *l, const char *extra) {
    char r[600]; skins_dir(r, sizeof r);
    scan_skins_root(l, r);
    if (extra && *extra) scan_skins_root(l, extra);
    qsort(l->e, l->n, sizeof(Entry), cmp);
}

void list_free(List *l) { free(l->e); l->e = NULL; l->n = 0; }
