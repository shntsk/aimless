#include "paths.h"
#include "beatmap.h"
#include "plat.h"
#include "skinfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* XDG_DATA_HOME/aimless if set (any OS), else ~/.local/share/aimless on Linux and %APPDATA%/aimless on Windows.
 * Always UTF-8 with '/' separators; computed once. */
const char *data_dir(void) {
    static char d[512];
    if (d[0]) return d;
    char b[480];
    if (plat_getenv("XDG_DATA_HOME", b, sizeof b)) snprintf(d, sizeof d, "%s/aimless", b);
#ifdef _WIN32
    else if (plat_getenv("APPDATA", b, sizeof b)) snprintf(d, sizeof d, "%s/aimless", b);
    else if (plat_getenv("USERPROFILE", b, sizeof b)) snprintf(d, sizeof d, "%s/AppData/Roaming/aimless", b);
    else snprintf(d, sizeof d, "./aimless-data");
    for (char *p = d; *p; p++) if (*p == '\\') *p = '/';
#else
    else if (plat_getenv("HOME", b, sizeof b)) snprintf(d, sizeof d, "%s/.local/share/aimless", b);
    else snprintf(d, sizeof d, "./.local/share/aimless");
#endif
    return d;
}

static void mkdir_p(const char *path) { plat_mkdir_p(path); }

void songs_dir(char *o, size_t n) { snprintf(o, n, "%s/Songs", data_dir()); }
void skins_dir(char *o, size_t n) { snprintf(o, n, "%s/Skins", data_dir()); }

void ensure_dirs(void) {
    char p[600];
    songs_dir(p, sizeof p); mkdir_p(p);
    skins_dir(p, sizeof p); mkdir_p(p);
}

static int ends_with(const char *s, const char *ext) {
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcasecmp(s + a - b, ext);
}

static int dir_has_osu(const char *dir) {
    PlatDir *d = plat_opendir(dir); if (!d) return 0;
    const char *e; int found = 0;
    while ((e = plat_readdir(d))) if (ends_with(e, ".osu")) { found = 1; break; }
    plat_closedir(d);
    return found;
}

static void rm_rf(const char *path) { plat_remove_tree(path); }

/* directory (not recursive beyond one wrapper folder) that actually holds the skin files */
static int find_skin_root(const char *tmp, char *out, size_t n) {
    if (skinfs_looks_like_skin(tmp)) { snprintf(out, n, "%s", tmp); return 1; }
    PlatDir *d = plat_opendir(tmp); if (!d) return 0;
    const char *e; char only[1100] = ""; int dirs = 0;
    while ((e = plat_readdir(d))) {
        if (e[0] == '.' || !strcmp(e, "__MACOSX")) continue;
        char p[1100]; snprintf(p, sizeof p, "%s/%s", tmp, e);
        if (skinfs_isdir(p)) { dirs++; snprintf(only, sizeof only, "%s", p); }
    }
    plat_closedir(d);
    if (dirs == 1 && skinfs_looks_like_skin(only)) { snprintf(out, n, "%s", only); return 1; }
    return 0;
}

/* A skin archive is extracted to a hidden temporary folder inside Skins/ (never listed), checked, then
 * renamed into place, so a broken archive leaves nothing behind and an installed skin is never overwritten:
 * a name clash gets " (2)", " (3)", ... The .osk itself is only read. */
static int import_osk(const char *src, const char *name, char *msg, size_t n, char *dest_out, size_t dn) {
    char root[600], tmp[700], skin_root[1100], dest[900], clean[256];
    skins_dir(root, sizeof root); mkdir_p(root);
    snprintf(tmp, sizeof tmp, "%s/.import-XXXXXX", root);
    if (!plat_mkdtemp(tmp)) { snprintf(msg, n, "import failed: cannot write to the skins folder"); return -1; }
    if (plat_extract_zip(src, tmp)) {
        rm_rf(tmp);
        snprintf(msg, n, "import failed: cannot extract .osk (" PLAT_UNZIP_HINT ")");
        return -1;
    }
    if (!find_skin_root(tmp, skin_root, sizeof skin_root)) {
        rm_rf(tmp);
        snprintf(msg, n, "import failed: no skin files found in .osk");
        return -1;
    }
    snprintf(clean, sizeof clean, "%s", name);
    for (char *p = clean; *p; p++) if (*p == '/' || *p == '\\') *p = '_';
    if (!clean[0] || clean[0] == '.') { char t[256]; snprintf(t, sizeof t, "%s", clean); snprintf(clean, sizeof clean, "skin_%.240s", t); }
    int k = 1;
    snprintf(dest, sizeof dest, "%s/%s", root, clean);
    while (plat_exists(dest) && k < 100) snprintf(dest, sizeof dest, "%s/%s (%d)", root, clean, ++k);
    if (k >= 100 || plat_rename(skin_root, dest)) {
        rm_rf(tmp);
        snprintf(msg, n, "import failed: cannot install skin");
        return -1;
    }
    if (strcmp(skin_root, tmp)) rm_rf(tmp); /* leftover wrapper (e.g. __MACOSX) */
    if (k > 1) snprintf(msg, n, "imported skin: %s (%d) - a skin with that name already exists", clean, k);
    else snprintf(msg, n, "imported skin: %s", clean);
    if (dest_out) snprintf(dest_out, dn, "%s", dest);
    return 0;
}

int import_path_ex(const char *src, char *msg, size_t n, char *dest_out, size_t dn) {
    PlatStat st;
    if (dest_out && dn) dest_out[0] = 0;
    if (plat_stat(src, &st)) { snprintf(msg, n, "import failed: cannot open file"); return -1; }
    /* name = last path component; trailing separators ("dir/", "C:\\maps\\dir\\") are ignored */
    char trimmed[1100]; snprintf(trimmed, sizeof trimmed, "%s", src);
    size_t l = strlen(trimmed);
    while (l > 1 && plat_is_sep(trimmed[l-1])) trimmed[--l] = 0;
    const char *b = plat_last_sep(trimmed); b = b ? b + 1 : trimmed;
    char name[256]; snprintf(name, sizeof name, "%s", b);
    char root[600], dest[900];
    int is_skin = 0, is_dir = st.is_dir;
    if (is_dir) is_skin = !dir_has_osu(src);
    else if (ends_with(name, ".osz")) is_skin = 0;
    else if (ends_with(name, ".osk")) is_skin = 1;
    else { snprintf(msg, n, "import failed: use .osz, .osk or a folder"); return -1; }
    char *dot = strrchr(name, '.');
    if (!is_dir && dot) *dot = 0;
    if (is_skin && !is_dir) return import_osk(src, name, msg, n, dest_out, dn);
    if (is_dir && is_skin && !skinfs_looks_like_skin(src)) { snprintf(msg, n, "import failed: folder has no skin or beatmap files"); return -1; }
    if (is_skin) skins_dir(root, sizeof root); else songs_dir(root, sizeof root);
    snprintf(dest, sizeof dest, "%s/%s", root, name);
    mkdir_p(root);
    int rc;
    if (is_dir) rc = plat_copy_tree(src, dest);
    else rc = plat_extract_zip(src, dest);
    if (rc) snprintf(msg, n, "import failed (" PLAT_UNZIP_HINT ")");
    else snprintf(msg, n, "imported %s: %s", is_skin ? "skin" : "song", name);
    if (!rc && dest_out) snprintf(dest_out, dn, "%s", dest);
    return rc;
}

int import_path(const char *src, char *msg, size_t n) { return import_path_ex(src, msg, n, NULL, 0); }

static void add(List *l, const char *label, const char *path) {
    l->e = realloc(l->e, (l->n + 1) * sizeof(Entry));
    snprintf(l->e[l->n].label, sizeof l->e[l->n].label, "%s", label);
    snprintf(l->e[l->n].path, sizeof l->e[l->n].path, "%s", path);
    l->n++;
}

static int cmp(const void *a, const void *b) { return strcasecmp(((Entry *)a)->label, ((Entry *)b)->label); }

static void scan_songs_root(List *l, const char *root) {
    PlatDir *d = plat_opendir(root); if (!d) return;
    const char *e;
    while ((e = plat_readdir(d))) {
        if (e[0] == '.') continue;
        char sub[1100]; snprintf(sub, sizeof sub, "%s/%s", root, e);
        PlatDir *sd = plat_opendir(sub); if (!sd) continue;
        const char *f;
        while ((f = plat_readdir(sd))) {
            if (!ends_with(f, ".osu")) continue;
            char p[2200]; snprintf(p, sizeof p, "%s/%s", sub, f);
            Beatmap bm;
            if (beatmap_load(&bm, p, 1)) { beatmap_free(&bm); continue; }
            char label[400];
            snprintf(label, sizeof label, "%s - %s [%s]", bm.artist, bm.title, bm.version);
            add(l, label, p);
            beatmap_free(&bm);
        }
        plat_closedir(sd);
    }
    plat_closedir(d);
}

void scan_songs(List *l, const char *extra) {
    char r[600]; songs_dir(r, sizeof r);
    scan_songs_root(l, r);
    if (extra && *extra) scan_songs_root(l, extra);
    if (l->n) qsort(l->e, l->n, sizeof(Entry), cmp);
}

static void scan_skins_root(List *l, const char *root) {
    PlatDir *d = plat_opendir(root); if (!d) return;
    const char *e;
    while ((e = plat_readdir(d))) {
        if (e[0] == '.') continue;
        char p[1100]; snprintf(p, sizeof p, "%s/%s", root, e);
        if (!skinfs_isdir(p)) continue;
        add(l, e, p);
    }
    plat_closedir(d);
}

void scan_skins(List *l, const char *extra) {
    char r[600]; skins_dir(r, sizeof r);
    scan_skins_root(l, r);
    if (extra && *extra) scan_skins_root(l, extra);
    if (l->n) qsort(l->e, l->n, sizeof(Entry), cmp);
}

int resolve_skin_arg(const char *arg, const char *extra, char *out, size_t n, char *msg, size_t mn) {
    PlatStat st;
    out[0] = 0; msg[0] = 0;
    if (!arg || !*arg) return 0;
    if (!plat_stat(arg, &st)) {
        if (st.is_dir) { snprintf(out, n, "%s", arg); return 0; }
        if (ends_with(arg, ".osk")) return import_path_ex(arg, msg, mn, out, n);
        snprintf(msg, mn, "skin \"%s\" is neither a folder nor an .osk", arg);
        return -1;
    }
    if (!plat_has_sep(arg)) { /* bare name: look in Skins/ and the extra root */
        char r[600], p[1300];
        skins_dir(r, sizeof r); snprintf(p, sizeof p, "%s/%s", r, arg);
        if (skinfs_isdir(p)) { snprintf(out, n, "%s", p); return 0; }
        if (extra && *extra) { snprintf(p, sizeof p, "%s/%s", extra, arg); if (skinfs_isdir(p)) { snprintf(out, n, "%s", p); return 0; } }
    }
    snprintf(msg, mn, "skin \"%s\" not found", arg);
    return -1;
}

void list_free(List *l) { free(l->e); l->e = NULL; l->n = 0; }
