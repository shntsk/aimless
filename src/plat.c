/* Platform layer, see plat.h. The first half is the OS-specific primitives (POSIX / Windows), the second
 * half is shared code built on top of them, so the Linux test run exercises the same logic Windows uses. */
#include "plat.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* ======================================================================== Windows */
#include <windows.h>

static wchar_t *wide_ex(const char *u, int as_path) {   /* UTF-8 -> UTF-16; as_path: '/' -> '\', trailing separators dropped (not on roots) */
    if (!u) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (!w) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u, -1, w, n)) { free(w); return NULL; }
    if (as_path) {
        size_t l = wcslen(w);
        for (size_t i = 0; i < l; i++) if (w[i] == L'/') w[i] = L'\\';
        while (l > 1 && w[l - 1] == L'\\' && !(l == 3 && w[1] == L':')) w[--l] = 0;
    }
    return w;
}

static wchar_t *wide(const char *u) { return wide_ex(u, 1); }

static int utf8_from_wide(const wchar_t *w, char *out, size_t n) {
    return n && WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) > 0;
}

static long long ft_to_unix(FILETIME ft) {
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t < 116444736000000000ULL ? 0 : (long long)((t - 116444736000000000ULL) / 10000000ULL);
}

static void fill_stat(PlatStat *st, DWORD attr, DWORD tag, DWORD hi, DWORD lo, FILETIME mt) {
    memset(st, 0, sizeof *st);
    st->is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st->is_reg = !st->is_dir;
    /* only real links count: OneDrive placeholders etc. also carry the reparse flag */
    st->is_link = (attr & FILE_ATTRIBUTE_REPARSE_POINT) && (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT);
    st->size = (long long)(((unsigned long long)hi << 32) | lo);
    st->mtime = ft_to_unix(mt);
}

static int wlstat(const wchar_t *w, PlatStat *st) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        fill_stat(st, fd.dwFileAttributes, fd.dwReserved0, fd.nFileSizeHigh, fd.nFileSizeLow, fd.ftLastWriteTime);
        return 0;
    }
    WIN32_FILE_ATTRIBUTE_DATA ad;                    /* roots such as "C:\" cannot be found with FindFirstFile */
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &ad)) return -1;
    fill_stat(st, ad.dwFileAttributes, 0, ad.nFileSizeHigh, ad.nFileSizeLow, ad.ftLastWriteTime);
    return 0;
}

int plat_lstat(const char *path, PlatStat *st) {
    wchar_t *w = wide(path); if (!w) return -1;
    int rc = wlstat(w, st);
    free(w);
    return rc;
}

int plat_stat(const char *path, PlatStat *st) {
    wchar_t *w = wide(path); if (!w) return -1;
    int rc = wlstat(w, st);
    if (!rc && st->is_link) {                         /* follow the link: look at the target through a handle */
        HANDLE h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        BY_HANDLE_FILE_INFORMATION bi;
        if (h == INVALID_HANDLE_VALUE) rc = -1;
        else {
            if (GetFileInformationByHandle(h, &bi)) fill_stat(st, bi.dwFileAttributes, 0, bi.nFileSizeHigh, bi.nFileSizeLow, bi.ftLastWriteTime);
            else rc = -1;
            CloseHandle(h);
        }
    }
    free(w);
    return rc;
}

FILE *plat_fopen(const char *path, const char *mode) {
    wchar_t *w = wide(path), wm[16]; if (!w) return NULL;
    size_t i = 0; for (; mode[i] && i < 15; i++) wm[i] = (unsigned char)mode[i]; wm[i] = 0;
    FILE *f = _wfopen(w, wm);
    free(w);
    return f;
}

static int p_mkdir(const char *path) {                /* 0 if created or already a directory */
    wchar_t *w = wide(path); if (!w) return -1;
    int rc = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
    free(w);
    return rc;
}

static void clear_readonly(const wchar_t *w) {
    DWORD a = GetFileAttributesW(w);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(w, a & ~FILE_ATTRIBUTE_READONLY);
}

static int p_unlink(const char *path, int is_dir_link) {   /* file or link (a directory link is removed like a directory) */
    wchar_t *w = wide(path); if (!w) return -1;
    int rc = is_dir_link ? !RemoveDirectoryW(w) : !DeleteFileW(w);
    if (rc) { clear_readonly(w); rc = is_dir_link ? !RemoveDirectoryW(w) : !DeleteFileW(w); }
    free(w);
    return rc ? -1 : 0;
}

static int p_rmdir(const char *path) {
    wchar_t *w = wide(path); if (!w) return -1;
    int rc = !RemoveDirectoryW(w);
    if (rc) { clear_readonly(w); rc = !RemoveDirectoryW(w); }
    free(w);
    return rc ? -1 : 0;
}

int plat_rename(const char *from, const char *to) {
    wchar_t *a = wide(from), *b = wide(to);
    int rc = -1;
    if (a && b) rc = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : -1;
    free(a); free(b);
    return rc;
}

struct PlatDir { HANDLE h; WIN32_FIND_DATAW fd; int first; char name[1024]; };

PlatDir *plat_opendir(const char *path) {
    wchar_t *w = wide(path); if (!w) return NULL;
    size_t l = wcslen(w);
    wchar_t *pat = malloc((l + 3) * sizeof *pat);
    if (!pat) { free(w); return NULL; }
    memcpy(pat, w, l * sizeof *pat);
    if (l && pat[l - 1] != L'\\') pat[l++] = L'\\';
    pat[l++] = L'*'; pat[l] = 0;
    free(w);
    PlatDir *d = calloc(1, sizeof *d);
    if (!d) { free(pat); return NULL; }
    d->h = FindFirstFileW(pat, &d->fd);
    free(pat);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

const char *plat_readdir(PlatDir *d) {
    for (;;) {
        if (d->first) d->first = 0;
        else if (!FindNextFileW(d->h, &d->fd)) return NULL;
        const wchar_t *n = d->fd.cFileName;
        if (!wcscmp(n, L".") || !wcscmp(n, L"..")) continue;
        if (!utf8_from_wide(n, d->name, sizeof d->name)) continue;
        return d->name;
    }
}

void plat_closedir(PlatDir *d) { if (d) { FindClose(d->h); free(d); } }

int plat_getenv(const char *name, char *out, size_t n) {
    wchar_t wn[128], wv[2048];
    size_t i = 0; for (; name[i] && i < 127; i++) wn[i] = (unsigned char)name[i]; wn[i] = 0;
    DWORD k = GetEnvironmentVariableW(wn, wv, 2048);
    if (!k || k >= 2048) return 0;
    return utf8_from_wide(wv, out, n) && out[0];
}

int plat_setenv(const char *name, const char *value) {
    wchar_t wn[128], *wv = wide_ex(value, 0);
    size_t i = 0; for (; name[i] && i < 127; i++) wn[i] = (unsigned char)name[i]; wn[i] = 0;
    if (!wv) return -1;
    int rc = SetEnvironmentVariableW(wn, wv) ? 0 : -1;
    free(wv);
    return rc;
}

char *plat_mkdtemp(char *t) {
    size_t l = strlen(t);
    if (l < 6 || strcmp(t + l - 6, "XXXXXX")) return NULL;
    static unsigned long long s;
    if (!s) { LARGE_INTEGER q; QueryPerformanceCounter(&q); s = (unsigned long long)q.QuadPart ^ ((unsigned long long)GetCurrentProcessId() << 32) ^ 0x9E3779B97F4A7C15ULL; }
    static const char cs[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int tries = 0; tries < 200; tries++) {
        for (int i = 0; i < 6; i++) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; t[l - 6 + i] = cs[s % 36]; }
        wchar_t *w = wide(t); if (!w) return NULL;
        BOOL ok = CreateDirectoryW(w, NULL);
        DWORD e = GetLastError();
        free(w);
        if (ok) return t;
        if (e != ERROR_ALREADY_EXISTS) return NULL;
    }
    return NULL;
}

/* CreateProcess takes one command line: quote every argument the way CommandLineToArgvW / the CRT undo it */
static int append_arg(wchar_t **buf, size_t *len, size_t *cap, const wchar_t *a) {
    size_t need = *len + wcslen(a) * 2 + 4;
    if (need > *cap) { size_t nc = need * 2; wchar_t *nb = realloc(*buf, nc * sizeof **buf); if (!nb) return -1; *buf = nb; *cap = nc; }
    wchar_t *o = *buf + *len;
    if (*len) *o++ = L' ';
    int plain = *a != 0;
    for (const wchar_t *p = a; *p; p++) if (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\v' || *p == L'"') plain = 0;
    if (plain) { while (*a) *o++ = *a++; }
    else {
        *o++ = L'"';
        for (const wchar_t *p = a; ; p++) {
            size_t bs = 0;
            while (*p == L'\\') { bs++; p++; }
            if (!*p) { for (size_t i = 0; i < bs * 2; i++) *o++ = L'\\'; break; }   /* backslashes before the closing quote */
            if (*p == L'"') { for (size_t i = 0; i < bs * 2 + 1; i++) *o++ = L'\\'; *o++ = L'"'; }
            else { for (size_t i = 0; i < bs; i++) *o++ = L'\\'; *o++ = *p; }
        }
        *o++ = L'"';
    }
    *o = 0;
    *len = (size_t)(o - *buf);
    return 0;
}

int plat_run(char *const argv[]) {
    wchar_t *cmd = NULL; size_t len = 0, cap = 0;
    for (int i = 0; argv[i]; i++) {
        wchar_t *w = wide_ex(argv[i], 0);   /* arguments are passed through untouched; tar.exe and unzip accept '/' */
        if (!w || append_arg(&cmd, &len, &cap, w)) { free(w); free(cmd); return -1; }
        free(w);
    }
    if (!cmd) return -1;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    DWORD err = GetLastError();
    free(cmd);
    if (!ok) return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) ? 127 : -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    BOOL got = GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return got && code <= 255 ? (int)code : -1;
}

static int system_tar(char *out, size_t n) {   /* %SystemRoot%\System32\tar.exe: bsdtar, which reads zip. A "tar" on PATH may be GNU tar (Git, MSYS) */
    char root[600];
    if (!plat_getenv("SystemRoot", root, sizeof root) && !plat_getenv("WINDIR", root, sizeof root)) snprintf(root, sizeof root, "C:/Windows");
    snprintf(out, n, "%s/System32/tar.exe", root);
    return plat_exists(out);
}

void plat_find_font(int bold, const char *pattern, char *out, size_t n) {
    (void)pattern;
    static const char *reg[] = { "segoeui.ttf", "arial.ttf", "tahoma.ttf", NULL };
    static const char *bol[] = { "segoeuib.ttf", "arialbd.ttf", "tahomabd.ttf", NULL };
    const char **l = bold ? bol : reg;
    char root[600];
    out[0] = 0;
    if (!plat_getenv("SystemRoot", root, sizeof root) && !plat_getenv("WINDIR", root, sizeof root)) snprintf(root, sizeof root, "C:/Windows");
    for (int i = 0; !out[0] && l[i]; i++) {
        char p[700]; snprintf(p, sizeof p, "%s/Fonts/%s", root, l[i]);
        if (plat_exists(p)) snprintf(out, n, "%s", p);
    }
}

#else
/* ======================================================================== POSIX */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void fill_stat(PlatStat *s, const struct stat *st) {
    memset(s, 0, sizeof *s);
    s->is_dir = S_ISDIR(st->st_mode); s->is_reg = S_ISREG(st->st_mode); s->is_link = S_ISLNK(st->st_mode);
    s->size = (long long)st->st_size; s->mtime = (long long)st->st_mtime;
}

int plat_stat(const char *path, PlatStat *s) { struct stat st; if (stat(path, &st)) return -1; fill_stat(s, &st); return 0; }
int plat_lstat(const char *path, PlatStat *s) { struct stat st; if (lstat(path, &st)) return -1; fill_stat(s, &st); return 0; }
FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }

static int p_mkdir(const char *path) { return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1; }
static int p_unlink(const char *path, int is_dir_link) { (void)is_dir_link; return unlink(path); }
static int p_rmdir(const char *path) { return rmdir(path); }
int plat_rename(const char *from, const char *to) { return rename(from, to); }

struct PlatDir { DIR *d; };

PlatDir *plat_opendir(const char *path) {
    DIR *d = opendir(path); if (!d) return NULL;
    PlatDir *p = malloc(sizeof *p);
    if (!p) { closedir(d); return NULL; }
    p->d = d;
    return p;
}

const char *plat_readdir(PlatDir *p) {
    struct dirent *e;
    while ((e = readdir(p->d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) return e->d_name;
    return NULL;
}

void plat_closedir(PlatDir *p) { if (p) { closedir(p->d); free(p); } }

int plat_getenv(const char *name, char *out, size_t n) {
    const char *v = getenv(name);
    if (!v || !*v) return 0;
    snprintf(out, n, "%s", v);
    return 1;
}

int plat_setenv(const char *name, const char *value) { return setenv(name, value, 1); }
char *plat_mkdtemp(char *t) { return mkdtemp(t); }

int plat_run(char *const argv[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) { execvp(argv[0], argv); _exit(127); }
    int st;
    while (waitpid(p, &st, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

void plat_find_font(int bold, const char *pattern, char *out, size_t n) {
    static const char *fb[] = { "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
    static const char *fbb[] = { "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
                                 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL };
    char q[512];
    out[0] = 0;
    if (pattern) {                                   /* NULL: skip fontconfig, known locations only */
        snprintf(q, sizeof q, "fc-match -f '%%{file}' '%s' 2>/dev/null", pattern);
        FILE *p = popen(q, "r");
        if (p) { if (!fgets(out, (int)n, p)) out[0] = 0; pclose(p); }
    }
    if (out[0]) { FILE *t = fopen(out, "r"); if (t) fclose(t); else out[0] = 0; }
    const char **l = bold ? fbb : fb;
    for (int i = 0; !out[0] && l[i]; i++) { FILE *t = fopen(l[i], "r"); if (t) { fclose(t); snprintf(out, n, "%s", l[i]); } }
}
#endif

/* ======================================================================== shared */
const char *plat_last_sep(const char *path) {
    const char *s = strrchr(path, '/');
#ifdef _WIN32
    const char *b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
#endif
    return s;
}

int plat_has_sep(const char *path) { return plat_last_sep(path) != NULL; }

int plat_is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

int plat_exists(const char *path) { PlatStat st; return plat_stat(path, &st) == 0; }

int plat_mkdir_p(const char *path) {
    char tmp[1024];
    if (snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp || !tmp[0]) return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
#ifndef _WIN32
            if (*p == '\\') continue;                /* a normal file-name character on POSIX */
#endif
            char c = *p; *p = 0; p_mkdir(tmp); *p = c;
        }
    }
    p_mkdir(tmp);
    PlatStat st;
    return plat_stat(tmp, &st) == 0 && st.is_dir ? 0 : -1;
}

/* names of a directory, copied so the directory can be modified while they are processed */
static int list_names(const char *dir, char ***out, int *n) {
    PlatDir *d = plat_opendir(dir);
    char **v = NULL; int cnt = 0, cap = 0;
    *out = NULL; *n = 0;
    if (!d) return -1;
    const char *e;
    while ((e = plat_readdir(d))) {
        if (cnt == cap) { cap = cap ? cap * 2 : 16; char **nv = realloc(v, (size_t)cap * sizeof *v); if (!nv) goto fail; v = nv; }
        if (!(v[cnt] = strdup(e))) goto fail;
        cnt++;
    }
    plat_closedir(d);
    *out = v; *n = cnt;
    return 0;
fail:
    plat_closedir(d);
    for (int i = 0; i < cnt; i++) free(v[i]);
    free(v);
    return -1;
}

static void free_names(char **v, int n) { for (int i = 0; i < n; i++) free(v[i]); free(v); }

#define MAX_DEPTH 48

static int rm_tree(const char *path, int depth) {
    PlatStat st;
    if (plat_lstat(path, &st)) return 0;             /* already gone */
    if (st.is_link || !st.is_dir) return p_unlink(path, st.is_link && st.is_dir);
    if (depth > MAX_DEPTH) return -1;
    char **names; int n, rc = 0;
    if (list_names(path, &names, &n)) return -1;
    for (int i = 0; i < n; i++) {
        char c[2048];
        if (snprintf(c, sizeof c, "%s/%s", path, names[i]) >= (int)sizeof c || rm_tree(c, depth + 1)) rc = -1;
    }
    free_names(names, n);
    return p_rmdir(path) ? -1 : rc;
}

int plat_remove_tree(const char *path) {
    char p[2048];
    size_t l = (size_t)snprintf(p, sizeof p, "%s", path);
    if (!path[0] || l >= sizeof p) return -1;
    while (l > 1 && (p[l - 1] == '/' || p[l - 1] == '\\')) p[--l] = 0;   /* "dir/" must remove dir, and a link as a link */
    return rm_tree(p, 0);
}

static int copy_file(const char *src, const char *dst) {
    FILE *a = plat_fopen(src, "rb"), *b;
    if (!a) return -1;
    if (!(b = plat_fopen(dst, "wb"))) { fclose(a); return -1; }
    char buf[16384]; size_t k; int rc = 0;
    while ((k = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, k, b) != k) { rc = -1; break; }
    if (ferror(a)) rc = -1;
    fclose(a);
    if (fclose(b)) rc = -1;
    return rc;
}

static int cp_tree(const char *src, const char *dst, int depth) {
    PlatStat st;
    if (plat_stat(src, &st)) return -1;
    if (!st.is_dir) return copy_file(src, dst);
    if (depth > MAX_DEPTH || plat_mkdir_p(dst)) return -1;
    char **names; int n, rc = 0;
    if (list_names(src, &names, &n)) return -1;
    for (int i = 0; i < n; i++) {
        char s[2048], d[2048]; PlatStat cs;
        if (snprintf(s, sizeof s, "%s/%s", src, names[i]) >= (int)sizeof s || snprintf(d, sizeof d, "%s/%s", dst, names[i]) >= (int)sizeof d) { rc = -1; continue; }
        if (plat_lstat(s, &cs) || cs.is_link) continue;   /* links are not copied: no loops, nothing outside the tree */
        if (cp_tree(s, d, depth + 1)) rc = -1;
    }
    free_names(names, n);
    return rc;
}

int plat_copy_tree(const char *src, const char *dst) { return cp_tree(src, dst, 0); }

/* Info-ZIP unzip exits with 1 for "completed with warnings" (typical for Windows-made archives with backslash
 * paths), which is not a failure. bsdtar/tar.exe refuse ".." and absolute member paths by default, as unzip does. */
static int run_unzip(const char *zip, const char *dest) {
    char *u[] = { "unzip", "-qq", "-o", (char *)zip, "-d", (char *)dest, NULL };
    int rc = plat_run(u);
    return rc == 1 ? 0 : rc;
}

static int run_tar(const char *exe, const char *zip, const char *dest) {
    char *t[] = { (char *)exe, "-xf", (char *)zip, "-C", (char *)dest, NULL };
    return plat_run(t);
}

int plat_extract_zip(const char *zip, const char *dest) {
    if (plat_mkdir_p(dest)) return -1;
    int rc;
#ifdef _WIN32
    char st[700];
    if (system_tar(st, sizeof st)) return run_tar(st, zip, dest) == 0 ? 0 : -1;
    rc = run_tar("bsdtar", zip, dest);
    if (rc == 127) rc = run_unzip(zip, dest);
#else
    rc = run_unzip(zip, dest);
    if (rc == 127) rc = run_tar("bsdtar", zip, dest);
#endif
    return rc == 0 ? 0 : -1;
}

const char *plat_strcasestr(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) return hay;
    for (; *hay; hay++) if (!strncasecmp(hay, needle, nl)) return hay;
    return NULL;
}
