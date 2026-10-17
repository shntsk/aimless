/* Thin platform layer: the only place that knows about POSIX vs Windows (no SDL).
 *
 * All paths are UTF-8 and use '/' internally. On POSIX every function is a direct wrapper over the usual
 * libc/POSIX call. On Windows paths are converted to UTF-16 and the wide-char APIs are used, so names that
 * are not in the ANSI code page ("音楽", "música") work. The rest of the code never includes <dirent.h>,
 * <unistd.h>, <sys/wait.h> or <strings.h> directly. */
#ifndef PLAT_H
#define PLAT_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  define strcasecmp  _stricmp
#  define strncasecmp _strnicmp
#  define PLAT_UNZIP_HINT "is tar.exe available?"
#else
#  include <strings.h>
#  define PLAT_UNZIP_HINT "is unzip installed?"
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* ---- paths ---- */
/* last path separator ('/' everywhere, also '\\' on Windows); NULL if none */
const char *plat_last_sep(const char *path);
int plat_has_sep(const char *path);
int plat_is_sep(char c);                                /* '/' everywhere, also '\\' on Windows */
/* value of an environment variable as UTF-8; returns 1 and fills out if it is set and not empty */
int plat_getenv(const char *name, char *out, size_t n);
int plat_setenv(const char *name, const char *value);   /* mostly for tests */

/* ---- files ---- */
typedef struct { int is_dir, is_reg, is_link; long long size, mtime; } PlatStat;
int   plat_stat(const char *path, PlatStat *st);        /* follows links; 0 on success */
int   plat_lstat(const char *path, PlatStat *st);       /* does not follow links */
int   plat_exists(const char *path);
FILE *plat_fopen(const char *path, const char *mode);
int   plat_mkdir_p(const char *path);                   /* like mkdir -p; 0 if the directory exists afterwards */
int   plat_rename(const char *from, const char *to);    /* replaces an existing file; 0 on success */
int   plat_remove_tree(const char *path);               /* like rm -rf (links are removed, never followed) */
int   plat_copy_tree(const char *src, const char *dst); /* like cp -rT; links in src are skipped; 0 on success */
char *plat_mkdtemp(char *tmpl);                         /* tmpl ends in "XXXXXX"; NULL on failure */

typedef struct PlatDir PlatDir;
PlatDir    *plat_opendir(const char *path);
const char *plat_readdir(PlatDir *d);                   /* next entry name (UTF-8), never "." or ".."; NULL at the end */
void        plat_closedir(PlatDir *d);

/* ---- processes ---- */
/* run argv[0] with arguments, wait; returns the exit status (0..255), 127 if it could not be found/started,
 * or -1 if it was killed */
int plat_run(char *const argv[]);
/* Extract a .zip into dest (created if needed); the archive is only read. 0 on success. POSIX: unzip, then
 * bsdtar. Windows: the system tar.exe (System32), then bsdtar/unzip on PATH. */
int plat_extract_zip(const char *zip, const char *dest);

/* ---- fonts ---- */
/* Path of a usable sans font (bold or regular). POSIX: fc-match with `pattern` (fontconfig syntax), then known
 * DejaVu locations (pattern NULL: only those). Windows: Segoe UI / Arial from the Fonts folder. out[0] == 0 if nothing was found. */
void plat_find_font(int bold, const char *pattern, char *out, size_t n);

/* ---- misc ---- */
const char *plat_strcasestr(const char *hay, const char *needle);

#endif
