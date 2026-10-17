/* Tests for the platform layer (src/plat.c) and the code that sits on it: file/dir primitives, tree copy and
 * removal, process launching and argument quoting, zip extraction, .osz/.osk/folder import, song scan and
 * library cache. No shell, no zip/rm/cp/mkdir commands, no SDL: the same test runs on Linux and on Windows
 * (MinGW). Every path lives under a directory whose name contains non-ASCII characters (UTF-8 on purpose).
 *
 * Zip extraction uses the built-in reader (src/plat.c); the system tools (unzip/bsdtar, System32\tar.exe) are only
 * the fallback for archives it does not handle (the bzip2 case below). If that fallback cannot run, that case is
 * reported as SKIPPED, unless AIMLESS_TEST_REQUIRE_ZIP=1 is set (then it is a failure).
 * Hidden mode: `test_plat --child ...` is the helper process used by the plat_run tests. */
#include "../src/library.h"
#include "../src/paths.h"
#include "../src/plat.h"
#include "../src/skinfs.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zipdata.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

static int checks, fails, skipped;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* UTF-8 test strings, spelled with hex escapes so the source file encoding does not matter */
#define U_MUSICA "M\xc3\xba" "sica"            /* Música */
#define U_CHECK  "\xe2\x9c\x93"                /* ✓ */
#define U_JP     "\xe9\x9f\xb3\xe6\xa5\xbd"    /* 音楽 */
#define U_NTILDE "\xc3\xb1"                    /* ñ */

static char root[1024], data[1100];
static const char *self;

/* formatted strings live in an arena that is only reset between tests, so a path built early in a test stays valid */
static char arena[1 << 18]; static size_t apos;
static const char *fmt(const char *f, ...) {
    if (apos + 1400 > sizeof arena) { puts("  test arena exhausted"); exit(2); }
    char *b = arena + apos;
    va_list ap; va_start(ap, f); int n = vsnprintf(b, 1400, f, ap); va_end(ap);
    apos += (size_t)(n < 0 ? 1 : n >= 1400 ? 1400 : n + 1);
    return b;
}

static int write_file(const char *p, const void *d, size_t n) {
    FILE *f = plat_fopen(p, "wb"); if (!f) return -1;
    int rc = fwrite(d, 1, n, f) == n ? 0 : -1;
    return (fclose(f) || rc) ? -1 : 0;
}

static long read_file(const char *p, char *buf, size_t cap) {
    FILE *f = plat_fopen(p, "rb"); if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f); fclose(f); buf[n] = 0;
    return (long)n;
}

static int file_is(const char *p, const char *want) { char b[4096]; long n = read_file(p, b, sizeof b); return n == (long)strlen(want) && !memcmp(b, want, (size_t)n); }

static int file_eq(const char *p, const void *want, size_t n) {   /* exact bytes, any size */
    FILE *f = plat_fopen(p, "rb"); if (!f) return 0;
    unsigned char *b = malloc(n + 2); if (!b) { fclose(f); return 0; }
    size_t got = fread(b, 1, n + 1, f); fclose(f);
    int ok = got == n && !memcmp(b, want, n);
    free(b);
    return ok;
}

static void lcg_bytes(unsigned char *o, size_t n, uint32_t seed) {   /* same generator as lcg() in tests/gen_zipdata.py */
    uint32_t x = seed;
    for (size_t i = 0; i < n; i++) { x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu; o[i] = (unsigned char)('a' + (x >> 16) % 12); }
}

static int count_dir(const char *p, const char *want) {   /* number of entries; *want (if given) must be among them */
    PlatDir *d = plat_opendir(p); if (!d) return -1;
    int n = 0, seen = want ? 0 : 1; const char *e;
    while ((e = plat_readdir(d))) { n++; if (want && !strcmp(e, want)) seen = 1; }
    plat_closedir(d);
    return seen ? n : -2;
}

/* ---- minimal zip writer: stored (no compression), UTF-8 names (flag bit 11), what osu! exports look like ---- */
static uint32_t crc32_(const unsigned char *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
static void le(FILE *f, uint32_t v, int bytes) { for (int i = 0; i < bytes; i++) fputc((int)((v >> (8 * i)) & 255), f); }
typedef struct { const char *name, *data; } ZEnt;
static int make_zip(const char *path, const ZEnt *e, int n) {
    FILE *f = plat_fopen(path, "wb"); if (!f) return -1;
    uint32_t off[64], pos = 0;
    for (int i = 0; i < n; i++) {
        size_t dl = strlen(e[i].data), nl = strlen(e[i].name);
        uint32_t crc = crc32_((const unsigned char *)e[i].data, dl);
        off[i] = pos;
        le(f, 0x04034b50, 4); le(f, 20, 2); le(f, 0x0800, 2); le(f, 0, 2); le(f, 0, 2); le(f, 0x21, 2);
        le(f, crc, 4); le(f, (uint32_t)dl, 4); le(f, (uint32_t)dl, 4); le(f, (uint32_t)nl, 2); le(f, 0, 2);
        fwrite(e[i].name, 1, nl, f); fwrite(e[i].data, 1, dl, f);
        pos += (uint32_t)(30 + nl + dl);
    }
    uint32_t cd = pos;
    for (int i = 0; i < n; i++) {
        size_t dl = strlen(e[i].data), nl = strlen(e[i].name);
        uint32_t crc = crc32_((const unsigned char *)e[i].data, dl);
        le(f, 0x02014b50, 4); le(f, 20, 2); le(f, 20, 2); le(f, 0x0800, 2); le(f, 0, 2); le(f, 0, 2); le(f, 0x21, 2);
        le(f, crc, 4); le(f, (uint32_t)dl, 4); le(f, (uint32_t)dl, 4); le(f, (uint32_t)nl, 2);
        le(f, 0, 2); le(f, 0, 2); le(f, 0, 2); le(f, 0, 2); le(f, 0, 4); le(f, off[i], 4);
        fwrite(e[i].name, 1, nl, f);
        pos += (uint32_t)(46 + nl);
    }
    le(f, 0x06054b50, 4); le(f, 0, 2); le(f, 0, 2); le(f, (uint32_t)n, 2); le(f, (uint32_t)n, 2);
    le(f, pos - cd, 4); le(f, cd, 4); le(f, 0, 2);
    return fclose(f) ? -1 : 0;
}

static const char *OSU =
    "osu file format v14\r\n\r\n[General]\r\nAudioFilename: audio " U_JP ".mp3\r\nPreviewTime: 1000\r\n\r\n"
    "[Metadata]\r\nTitle:T" U_MUSICA "\r\nArtist:Artist\r\nCreator:me\r\nVersion:Easy\r\n\r\n"
    "[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:4\r\nOverallDifficulty:5\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\n\r\n"
    "[TimingPoints]\r\n0,500,4,1,0,100,1,0\r\n\r\n"
    "[HitObjects]\r\n100,100,1000,1,0\r\n200,150,1500,1,0\r\n300,200,2000,1,0\r\n";

/* ---------------------------------------------------------------- tests */
static void test_paths(void) {
    puts("paths: separators, case-insensitive search");
    CHECK(!strcmp(plat_last_sep("a/b/c.osu"), "/c.osu"));
    CHECK(plat_last_sep("c.osu") == NULL);
    CHECK(plat_is_sep('/'));
#ifdef _WIN32
    CHECK(plat_is_sep('\\') && !strcmp(plat_last_sep("C:\\maps\\c.osu"), "\\c.osu") && plat_has_sep("C:\\x"));
    CHECK(!strcmp(plat_last_sep("C:\\maps/sub\\c.osu"), "\\c.osu") && !strcmp(plat_last_sep("C:\\maps\\sub/c.osu"), "/c.osu"));
#else
    CHECK(!plat_is_sep('\\') && plat_last_sep("a\\b") == NULL);   /* a normal file-name character on POSIX */
#endif
    CHECK(plat_strcasestr("Hello World", "WORLD") && !strcmp(plat_strcasestr("Hello World", "o w"), "o World"));
    CHECK(plat_strcasestr("abc", "") != NULL && plat_strcasestr("abc", "abcd") == NULL && plat_strcasestr("", "a") == NULL);
    CHECK(!strcasecmp("SKIN.ini", "skin.INI") && !strncasecmp("DrumX", "drumy", 4));
    char b[64];
    CHECK(plat_setenv("AIMLESS_T_ENV", "v" U_NTILDE) == 0 && plat_getenv("AIMLESS_T_ENV", b, sizeof b) && !strcmp(b, "v" U_NTILDE));
    CHECK(!plat_getenv("AIMLESS_T_NOT_SET", b, sizeof b));
}

static void test_files(void) {
    puts("files: UTF-8 names, stat, readdir, mkdir -p, rename");
    const char *d = fmt("%s/f " U_JP " " U_CHECK "/n/e/s/t", root);
    CHECK(plat_mkdir_p(d) == 0 && plat_mkdir_p(d) == 0);            /* creates, and is idempotent */
    PlatStat st;
    CHECK(plat_stat(d, &st) == 0 && st.is_dir && !st.is_reg);
    CHECK(plat_stat(fmt("%s/", d), &st) == 0 && st.is_dir);          /* trailing separator */
    CHECK(plat_stat(fmt("%s/nope", d), &st) != 0 && !plat_exists(fmt("%s/nope", d)));

    const char *f = fmt("%s/" U_MUSICA " " U_CHECK ".txt", d);
    CHECK(write_file(f, "hello", 5) == 0 && file_is(f, "hello"));
    CHECK(plat_stat(f, &st) == 0 && st.is_reg && !st.is_dir && st.size == 5 && st.mtime > 1500000000LL);
    CHECK(plat_lstat(f, &st) == 0 && !st.is_link);
    CHECK(plat_mkdir_p(f) != 0);                                     /* a file is in the way */
    CHECK(count_dir(d, U_MUSICA " " U_CHECK ".txt") == 1);           /* UTF-8 name comes back byte for byte, no "." / ".." */
    CHECK(count_dir(fmt("%s/missing", d), NULL) == -1);
    CHECK(plat_opendir(f) == NULL);                                  /* not a directory */

    const char *g = fmt("%s/other.txt", d);
    CHECK(write_file(g, "NEW", 3) == 0 && plat_rename(g, f) == 0);   /* replaces an existing file (the library cache needs it) */
    CHECK(file_is(f, "NEW") && !plat_exists(g));
    CHECK(plat_fopen(fmt("%s/nodir/x", d), "wb") == NULL);

    char t1[1100], t2[1100];
    snprintf(t1, sizeof t1, "%s/tmp-XXXXXX", root); snprintf(t2, sizeof t2, "%s/tmp-XXXXXX", root);
    CHECK(plat_mkdtemp(t1) && plat_mkdtemp(t2) && strcmp(t1, t2) && !strstr(t1, "XXXXXX"));
    CHECK(plat_stat(t1, &st) == 0 && st.is_dir);
    char bad[1100]; snprintf(bad, sizeof bad, "%s/tmp-XXXX", root);
    CHECK(plat_mkdtemp(bad) == NULL);
    plat_remove_tree(t1); plat_remove_tree(t2);
}

static void test_trees(void) {
    puts("trees: copy and remove");
    const char *s = fmt("%s/tree " U_CHECK, root), *c = fmt("%s/copy " U_NTILDE, root);
    CHECK(plat_mkdir_p(fmt("%s/a/b", s)) == 0 && plat_mkdir_p(fmt("%s/empty", s)) == 0);
    CHECK(write_file(fmt("%s/top.txt", s), "T", 1) == 0 && write_file(fmt("%s/a/b/" U_JP ".dat", s), "deep\r\n\x01\x02", 8) == 0);
    CHECK(plat_copy_tree(s, c) == 0);
    CHECK(file_is(fmt("%s/top.txt", c), "T") && file_is(fmt("%s/a/b/" U_JP ".dat", c), "deep\r\n\x01\x02"));
    CHECK(plat_exists(fmt("%s/empty", c)) && count_dir(c, NULL) == 3);
    CHECK(plat_copy_tree(s, c) == 0);                                 /* into an existing folder: like cp -rT */
    CHECK(plat_copy_tree(fmt("%s/missing", s), fmt("%s/x", c)) != 0);
#ifndef _WIN32
    const char *out = fmt("%s/outside", root);
    CHECK(plat_mkdir_p(out) == 0 && write_file(fmt("%s/keep.txt", out), "K", 1) == 0);
    CHECK(symlink(out, fmt("%s/link", s)) == 0 && symlink(fmt("%s/top.txt", s), fmt("%s/flink", s)) == 0);
    PlatStat ls; CHECK(plat_lstat(fmt("%s/link", s), &ls) == 0 && ls.is_link && plat_stat(fmt("%s/link", s), &ls) == 0 && ls.is_dir);
    const char *c2 = fmt("%s/copy2", root);
    CHECK(plat_copy_tree(s, c2) == 0 && !plat_exists(fmt("%s/link", c2)) && !plat_exists(fmt("%s/flink", c2)));   /* links are skipped */
    CHECK(plat_remove_tree(c2) == 0 && !plat_exists(c2));
#endif
    CHECK(plat_remove_tree(fmt("%s/", c)) == 0 && !plat_exists(c)); /* trailing slash accepted */
    CHECK(plat_remove_tree(s) == 0 && !plat_exists(s));
#ifndef _WIN32
    CHECK(file_is(fmt("%s/keep.txt", out), "K"));                     /* remove_tree never followed the link */
#endif
    CHECK(plat_remove_tree(s) == 0);                                   /* already gone is fine */
}

#ifdef _WIN32
/* main()'s argv is in the ANSI code page with MinGW ("\xe2\x9c\x93" or "\xe9\x9f\xb3\xe6\xa5\xbd" arrive as '?'), so the helper reads the real
 * UTF-16 command line instead, the same way the parent wrote it */
static char **utf8_args(int *argc) {
    int n = 0; wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return NULL;
    char **a = calloc((size_t)n + 1, sizeof *a);
    for (int i = 0; a && i < n; i++) {
        int k = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
        a[i] = malloc((size_t)(k > 0 ? k : 1));
        if (!a[i] || k <= 0 || !WideCharToMultiByte(CP_UTF8, 0, w[i], -1, a[i], k, NULL, NULL)) return NULL;
    }
    LocalFree(w);
    *argc = n;
    return a;
}
#endif

/* helper process: `test_plat --child exit N` | `--child args A B C D` (checks the quoting round trip) */
static int child_main(int argc, char **argv) {
#ifdef _WIN32
    int wc = 0; char **wa = utf8_args(&wc);
    if (!wa) return 98;
    argc = wc; argv = wa;
#endif
    if (argc >= 4 && !strcmp(argv[2], "exit")) return atoi(argv[3]);
    if (argc >= 3 && !strcmp(argv[2], "args")) {
        const char *want[4] = { "plain", "two words", "q\"uote\\", U_MUSICA " " U_CHECK };
        if (argc != 7) { fprintf(stderr, "child: got %d arguments, wanted 7\n", argc); return 9; }
        int bad = 0;
        for (int i = 0; i < 4; i++) if (strcmp(argv[3 + i], want[i])) {
            bad = 1; fprintf(stderr, "child: argument %d differs, got %zu bytes:", i, strlen(argv[3 + i]));
            for (const char *c = argv[3 + i]; *c; c++) fprintf(stderr, " %02x", (unsigned char)*c);
            fputc('\n', stderr);
        }
        return bad ? 9 : 0;
    }
    return 99;
}

static void test_run(void) {
    puts("process: exit status, argument quoting, missing program");
    char *a7[] = { (char *)self, "--child", "exit", "7", NULL };
    CHECK(plat_run(a7) == 7);
    char *a0[] = { (char *)self, "--child", "exit", "0", NULL };
    CHECK(plat_run(a0) == 0);
    char *aq[] = { (char *)self, "--child", "args", "plain", "two words", "q\"uote\\", U_MUSICA " " U_CHECK, NULL };
    CHECK(plat_run(aq) == 0);
    char *nf[] = { "aimless-no-such-program-xyz", NULL };
    CHECK(plat_run(nf) == 127);
}

static int zip_works(void) {                 /* is there a working extractor at all? */
    static int known = -1;
    if (known >= 0) return known;
    const char *z = fmt("%s/probe.zip", root), *o = fmt("%s/probe-out", root);
    ZEnt e[] = { { "p.txt", "probe" } };
    known = make_zip(z, e, 1) == 0 && plat_extract_zip(z, o) == 0 && file_is(fmt("%s/p.txt", o), "probe");
    plat_remove_tree(o);
    if (!known) {
        const char *req = getenv("AIMLESS_TEST_REQUIRE_ZIP");
        if (req && *req == '1') { checks++; fails++; puts("  FAIL: no working zip extractor (AIMLESS_TEST_REQUIRE_ZIP=1)"); }
        else puts("  SKIPPED: no working zip extractor found (unzip/bsdtar, or tar.exe on Windows)");
    }
    return known;
}

static void test_extract(void) {
    puts("zip: extraction with UTF-8 member names");
    if (!zip_works()) { skipped++; return; }
    const char *z = fmt("%s/x " U_JP ".zip", root), *o = fmt("%s/x-out " U_CHECK, root);
    ZEnt e[] = { { "Artist - T" U_MUSICA " [Easy].osu", "OSU" }, { "audio " U_JP ".mp3", "MP3\r\n" }, { "sub/inner " U_NTILDE ".txt", "in" } };
    CHECK(make_zip(z, e, 3) == 0 && plat_extract_zip(z, o) == 0);
    CHECK(file_is(fmt("%s/Artist - T" U_MUSICA " [Easy].osu", o), "OSU") && file_is(fmt("%s/audio " U_JP ".mp3", o), "MP3\r\n"));
    CHECK(file_is(fmt("%s/sub/inner " U_NTILDE ".txt", o), "in"));
    CHECK(plat_extract_zip(fmt("%s/missing.zip", root), fmt("%s/x-out2", root)) != 0);
    write_file(fmt("%s/bad.zip", root), "not a zip", 9);
    CHECK(plat_extract_zip(fmt("%s/bad.zip", root), fmt("%s/x-out3", root)) != 0);
    CHECK(plat_remove_tree(o) == 0);
}

static int flip_bit(const char *path, long off) {
    FILE *f = plat_fopen(path, "r+b"); if (!f) return -1;
    int c = fseek(f, off, SEEK_SET) ? EOF : fgetc(f);
    int rc = c == EOF || fseek(f, off, SEEK_SET) || fputc(c ^ 0x10, f) == EOF ? -1 : 0;
    return (fclose(f) || rc) ? -1 : 0;
}

/* the built-in reader on archives made by Python/zlib (deflate: fixed + dynamic blocks, stored, directory entries,
 * subfolders, backslash separators, UTF-8 names), then hostile and damaged archives, then the system-tool fallback */
static void test_builtin_zip(void) {
    puts("zip: built-in reader (deflate, subfolders, UTF-8 names, hostile and damaged archives, tool fallback)");
    unsigned char mp3[6000]; lcg_bytes(mp3, sizeof mp3, 1);
    const char *z = fmt("%s/defl " U_JP ".zip", root), *o = fmt("%s/defl-out " U_CHECK " " U_NTILDE, root);
    CHECK(write_file(z, ZIP_OSZ, sizeof ZIP_OSZ) == 0);
    CHECK(plat_extract_zip_builtin(z, o) == 0);
    CHECK(file_is(fmt("%s/Artist - T" U_MUSICA " [Easy].osu", o), OSU));
    CHECK(file_eq(fmt("%s/audio " U_JP ".mp3", o), mp3, sizeof mp3));
    CHECK(file_is(fmt("%s/sub/inner " U_NTILDE ".txt", o), "hi") && file_is(fmt("%s/sub/dir " U_CHECK "/deep.txt", o), "deep"));
    CHECK(file_is(fmt("%s/Back/slash.txt", o), "bs"));                       /* "Back\slash.txt" is a file in a folder on every OS */
    PlatStat st;
    CHECK(plat_stat(fmt("%s/sub", o), &st) == 0 && st.is_dir);
    const char *o2 = fmt("%s/defl-out2 " U_JP, root);                         /* the public entry point takes the same path */
    CHECK(plat_extract_zip(z, o2) == 0 && file_eq(fmt("%s/audio " U_JP ".mp3", o2), mp3, sizeof mp3));
    CHECK(plat_extract_zip_builtin(z, o) == 0);                               /* extracting over an existing tree */

    {   /* a match 25000 bytes back (distance symbol 29) */
        unsigned char far_[28000]; lcg_bytes(far_, 3000, 3); memset(far_ + 3000, 0, 22000); lcg_bytes(far_ + 25000, 3000, 3);
        const char *fz = fmt("%s/far.zip", root), *fo = fmt("%s/far-out", root);
        CHECK(write_file(fz, ZIP_FAR, sizeof ZIP_FAR) == 0 && plat_extract_zip_builtin(fz, fo) == 0 && file_eq(fmt("%s/far.bin", fo), far_, sizeof far_));
    }

    /* stored archive with UTF-8 names (the writer in this file), directly through the built-in reader */
    ZEnt se[] = { { "Artist - T" U_MUSICA " [Easy].osu", "OSU" }, { "audio " U_JP ".mp3", "MP3\r\n" }, { "sub/inner " U_NTILDE ".txt", "in" }, { "./dot.txt", "d" } };
    const char *sz = fmt("%s/stored " U_CHECK ".zip", root), *so = fmt("%s/stored-out " U_JP, root);
    CHECK(make_zip(sz, se, 4) == 0 && plat_extract_zip_builtin(sz, so) == 0);
    CHECK(file_is(fmt("%s/audio " U_JP ".mp3", so), "MP3\r\n") && file_is(fmt("%s/sub/inner " U_NTILDE ".txt", so), "in") && file_is(fmt("%s/dot.txt", so), "d"));

    /* hostile names: refused (-2), nothing at all is written, not even the harmless entry before the bad one */
    static const char *bad[] = { "../evil.txt", "sub/../../evil.txt", "..\\evil.txt", "/abs-evil.txt", "a/../b.txt" };
    for (int i = 0; i < 5; i++) {
        ZEnt he[] = { { "ok.txt", "ok" }, { bad[i], "x" } };
        const char *hz = fmt("%s/hostile%d.zip", root, i), *ho = fmt("%s/hostile-out%d", root, i);
        CHECK(make_zip(hz, he, 2) == 0 && plat_extract_zip_builtin(hz, ho) == -2);
        CHECK(!plat_exists(fmt("%s/ok.txt", ho)) && !plat_exists(fmt("%s/evil.txt", root)) && !plat_exists("/abs-evil.txt"));
        CHECK(plat_extract_zip(hz, fmt("%s/hostile-fb%d", root, i)) != 0);     /* and the system tools are not asked */
    }

    /* damaged archives: never accepted with wrong data, never a crash (run this under ASan/UBSan too) */
    size_t nl1 = strlen("Artist - T" U_MUSICA " [Easy].osu");
    unsigned cs1 = (unsigned)(ZIP_OSZ[18] | ZIP_OSZ[19] << 8);                /* compressed size of the first entry, from its local header */
    const char *dz = fmt("%s/damaged.zip", root), *dd = fmt("%s/damaged-out", root);
    int flips = 0, accepted_wrong = 0;
    for (long off = (long)(30 + nl1); off < (long)(30 + nl1 + cs1); off += 3) {
        CHECK(write_file(dz, ZIP_OSZ, sizeof ZIP_OSZ) == 0 && flip_bit(dz, off) == 0);
        flips++;
        if (plat_extract_zip_builtin(dz, dd) == 0 && !file_is(fmt("%s/Artist - T" U_MUSICA " [Easy].osu", dd), OSU)) accepted_wrong++;
        plat_remove_tree(dd);
    }
    CHECK(flips > 20 && accepted_wrong == 0);
    {   /* a flip inside the stored entry's data must be caught by the CRC */
        size_t pos = 0; for (size_t i = 0; i + 12 < sizeof ZIP_OSZ; i++) if (!memcmp(ZIP_OSZ + i, "deep.txtdeep", 12)) { pos = i + 8; break; }   /* local header name, then the stored bytes */
        CHECK(pos && write_file(dz, ZIP_OSZ, sizeof ZIP_OSZ) == 0 && flip_bit(dz, (long)pos) == 0 && plat_extract_zip_builtin(dz, dd) == -1);
        plat_remove_tree(dd);
    }
    static const size_t cuts[] = { 0, 10, 21, 60, 2000, 4151 };
    for (size_t i = 0; i < sizeof cuts / sizeof *cuts; i++) {
        CHECK(write_file(dz, ZIP_OSZ, cuts[i]) == 0 && plat_extract_zip_builtin(dz, dd) != 0);
        plat_remove_tree(dd);
    }
    write_file(dz, "not a zip at all, but longer than a zip header is", 50);
    CHECK(plat_extract_zip_builtin(dz, dd) == -1 && plat_extract_zip_builtin(fmt("%s/no-such.zip", root), dd) == -1);

    /* bzip2 member (method 12): the built-in reader says "unsupported", the system tool has to do it (ASCII paths) */
    const char *bz = fmt("%s/bz.zip", root), *bo = fmt("%s/fb-out", root);
    CHECK(write_file(bz, ZIP_BZ, sizeof ZIP_BZ) == 0 && plat_extract_zip_builtin(bz, fmt("%s/bz-builtin", root)) == 1);
    int rc = plat_extract_zip(bz, bo);
    if (rc == 0) CHECK(file_is(fmt("%s/bz.txt", bo), "bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 bzip2 "));
    else {
        const char *req = getenv("AIMLESS_TEST_REQUIRE_ZIP");
        if (req && *req == '1') { checks++; fails++; puts("  FAIL: system tool fallback (bzip2 member) did not work (AIMLESS_TEST_REQUIRE_ZIP=1)"); }
        else { puts("  SKIPPED: system tool fallback (bzip2 member) did not work"); skipped++; }
    }
}

static void test_import_deflate(void) {
    puts("import: deflate .osz / .osk with UTF-8 names and subfolders");
    char msg[300], dest[1100];
    unsigned char mp3[6000]; lcg_bytes(mp3, sizeof mp3, 1);
    Library lib;
    lib_scan(&lib, "", NULL, NULL); int before = lib.n; lib_free(&lib);
    const char *osz = fmt("%s/3 Artist - Tdeflate " U_CHECK ".osz", root);
    CHECK(write_file(osz, ZIP_OSZ, sizeof ZIP_OSZ) == 0);
    int rc = import_path_ex(osz, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Songs/3 Artist - Tdeflate " U_CHECK) && strstr(msg, "imported song"));
    CHECK(file_eq(fmt("%s/audio " U_JP ".mp3", dest), mp3, sizeof mp3) && file_is(fmt("%s/Artist - T" U_MUSICA " [Easy].osu", dest), OSU));
    CHECK(file_is(fmt("%s/sub/inner " U_NTILDE ".txt", dest), "hi") && file_is(fmt("%s/sub/dir " U_CHECK "/deep.txt", dest), "deep"));
    lib_scan(&lib, "", NULL, NULL);
    CHECK(lib.n == before + 1);
    lib_free(&lib);

    const char *osk = fmt("%s/Deflate Skin " U_CHECK ".osk", root);
    unsigned char hc[3000]; lcg_bytes(hc, sizeof hc, 7);
    unsigned char dg[100]; memset(dg, 'd', 50); memset(dg + 50, 'e', 50);
    char arg[1100];
    CHECK(write_file(osk, ZIP_OSK, sizeof ZIP_OSK) == 0);
    rc = import_path_ex(osk, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Skins/Deflate Skin " U_CHECK) && strstr(msg, "imported skin"));
    CHECK(skinfs_resolve(dest, "hitcircle", ".png", arg, sizeof arg) && file_eq(arg, hc, sizeof hc));
    CHECK(skinfs_resolve(dest, "Fonts/Hit/score-0", ".png", arg, sizeof arg) && file_eq(arg, dg, sizeof dg));   /* backslash member = sub-folder file */
}

static void test_import_songs(void) {
    puts("import: .osz and folders into Songs/, scan, library cache");
    char msg[300], dest[1100];
    CHECK(import_path_ex(fmt("%s/nothing.osz", root), msg, sizeof msg, dest, sizeof dest) != 0 && !dest[0]);
    CHECK(import_path_ex(fmt("%s/readme.txt", root), msg, sizeof msg, dest, sizeof dest) != 0);   /* does not exist, or wrong type */
    write_file(fmt("%s/readme.txt", root), "x", 1);
    CHECK(import_path_ex(fmt("%s/readme.txt", root), msg, sizeof msg, dest, sizeof dest) != 0 && strstr(msg, "use .osz"));

    if (zip_works()) {
        const char *osz = fmt("%s/1 Artist - T" U_MUSICA " " U_CHECK ".osz", root);
        ZEnt e[] = { { "Artist - T" U_MUSICA " [Easy].osu", OSU }, { "audio " U_JP ".mp3", "MP3" } };
        CHECK(make_zip(osz, e, 2) == 0);
        PlatStat before, after; plat_stat(osz, &before);
        int rc = import_path_ex(osz, msg, sizeof msg, dest, sizeof dest);
        printf("  -> %s\n", msg);
        CHECK(rc == 0 && strstr(dest, "/Songs/1 Artist - T" U_MUSICA " " U_CHECK) && strstr(msg, "imported song"));
        CHECK(file_is(fmt("%s/audio " U_JP ".mp3", dest), "MP3") && plat_exists(fmt("%s/Artist - T" U_MUSICA " [Easy].osu", dest)));
        CHECK(plat_stat(osz, &after) == 0 && after.size == before.size);                 /* source untouched */
    } else skipped++;

    /* a plain folder, also given with a trailing separator */
    const char *src = fmt("%s/Folder " U_CHECK, root);
    CHECK(plat_mkdir_p(fmt("%s/sub", src)) == 0 && write_file(fmt("%s/Artist - F [Hard].osu", src), OSU, strlen(OSU)) == 0 && write_file(fmt("%s/sub/a.txt", src), "a", 1) == 0);
    int rc = import_path_ex(fmt("%s/", src), msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Songs/Folder " U_CHECK) && file_is(fmt("%s/sub/a.txt", dest), "a"));
    CHECK(strlen(dest) > 0 && dest[strlen(dest) - 1] != '/');

    /* scan_songs / lib_scan see the UTF-8 names; the cache is replaced on the second scan, no .tmp left */
    List l = { NULL, 0 };
    scan_songs(&l, "");
    CHECK(l.n >= 1 && strstr(l.e[0].label, "Artist - T" U_MUSICA " [Easy]"));
    list_free(&l);
    Library lib;
    lib_scan(&lib, "", NULL, NULL);
    int sets1 = lib.n; CHECK(sets1 >= 1 && lib.ndiff >= 1);
    CHECK(sets1 > 0 && !strcmp(lib.s[0].d[0].mi.title, "T" U_MUSICA) && lib.s[0].audio[0] && strstr(lib.s[0].audio, "audio " U_JP ".mp3"));
    lib_free(&lib);
    CHECK(plat_exists(fmt("%s/library.cache", data)) && !plat_exists(fmt("%s/library.cache.tmp", data)));
    CHECK(plat_mkdir_p(fmt("%s/Songs/Second " U_JP, data)) == 0 && write_file(fmt("%s/Songs/Second " U_JP "/b.osu", data), OSU, strlen(OSU)) == 0);
    lib_scan(&lib, "", NULL, NULL);
    CHECK(lib.n == sets1 + 1);
    lib_free(&lib);
    CHECK(!plat_exists(fmt("%s/library.cache.tmp", data)));                              /* the cache was renamed over the old one */
    lib_scan(&lib, "", NULL, NULL);
    CHECK(lib.n == sets1 + 1);                                                            /* and it reads back */
    lib_free(&lib);
}

static void test_import_skins(void) {
    puts("import: .osk and skin folders into Skins/");
    char msg[300], dest[1100], dest2[1100];
    const char *skin_dir = fmt("%s/Skin " U_CHECK, root);
    CHECK(plat_mkdir_p(skin_dir) == 0 && write_file(fmt("%s/skin.ini", skin_dir), "[General]\nName: S\n", 18) == 0 && write_file(fmt("%s/Hitcircle.PNG", skin_dir), "x", 1) == 0);
    int rc = import_path_ex(skin_dir, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Skins/Skin " U_CHECK) && strstr(msg, "imported skin"));
    CHECK(skinfs_resolve(dest, "hitcircle", ".png", msg, sizeof msg));                    /* case-insensitive lookup in the copy */
    CHECK(plat_mkdir_p(fmt("%s/emptyfolder", root)) == 0 && import_path_ex(fmt("%s/emptyfolder", root), msg, sizeof msg, dest, sizeof dest) != 0);

    List l = { NULL, 0 };
    scan_skins(&l, "");
    int found = 0; for (int i = 0; i < l.n; i++) if (!strcmp(l.e[i].label, "Skin " U_CHECK)) found = 1;
    CHECK(found);
    list_free(&l);
    char arg[1100];
    CHECK(resolve_skin_arg("Skin " U_CHECK, "", arg, sizeof arg, msg, sizeof msg) == 0 && strstr(arg, "/Skins/Skin " U_CHECK));   /* bare name */
    CHECK(resolve_skin_arg("no-such-skin", "", arg, sizeof arg, msg, sizeof msg) != 0 && strstr(msg, "not found"));

    if (!zip_works()) { skipped++; return; }
    const char *osk = fmt("%s/My Skin " U_CHECK ".osk", root);
    ZEnt flat[] = { { "skin.ini", "[General]\nName: Z\n" }, { "Hitcircle.PNG", "png" }, { "Fonts\\Hit\\score-0.png", "digit" } };
    CHECK(make_zip(osk, flat, 3) == 0);
    rc = import_path_ex(osk, msg, sizeof msg, dest, sizeof dest);
    printf("  -> %s\n", msg);
    CHECK(rc == 0 && strstr(dest, "/Skins/My Skin " U_CHECK) && skinfs_resolve(dest, "hitcircle", ".png", arg, sizeof arg));
    /* Windows-made archives use backslashes in member names: unzip/tar.exe turn them into folders, bsdtar on Linux may not */
    printf("  (info) backslash member extracted as a sub-folder file: %s\n", skinfs_resolve(dest, "Fonts/Hit/score-0", ".png", arg, sizeof arg) ? "yes" : "no");
    rc = import_path_ex(osk, msg, sizeof msg, dest2, sizeof dest2);                       /* same name again: keep the old one */
    CHECK(rc == 0 && strcmp(dest, dest2) && strstr(dest2, " (2)") && strstr(msg, "already exists"));
    CHECK(plat_exists(osk));

    ZEnt wrap[] = { { "Wrapper " U_JP "/skin.ini", "[General]\n" }, { "Wrapper " U_JP "/hitcircle.png", "p" }, { "__MACOSX/junk", "j" } };
    const char *wosk = fmt("%s/wrapped.osk", root);
    CHECK(make_zip(wosk, wrap, 3) == 0);
    rc = import_path_ex(wosk, msg, sizeof msg, dest, sizeof dest);
    CHECK(rc == 0 && strstr(dest, "/Skins/wrapped") && skinfs_resolve(dest, "skin", ".ini", arg, sizeof arg) && !plat_exists(fmt("%s/__MACOSX", dest)));

    ZEnt none[] = { { "readme.txt", "r" } };
    CHECK(make_zip(fmt("%s/none.osk", root), none, 1) == 0);
    CHECK(import_path_ex(fmt("%s/none.osk", root), msg, sizeof msg, dest, sizeof dest) != 0 && strstr(msg, "no skin files") && !dest[0]);
    write_file(fmt("%s/bad.osk", root), "not a zip", 9);
    CHECK(import_path_ex(fmt("%s/bad.osk", root), msg, sizeof msg, dest, sizeof dest) != 0 && strstr(msg, "cannot extract"));
    /* failed imports leave no hidden temp folders behind */
    PlatDir *d = plat_opendir(fmt("%s/Skins", data)); const char *e; int leftovers = 0;
    while (d && (e = plat_readdir(d))) if (!strncmp(e, ".import", 7)) leftovers++;
    plat_closedir(d);
    CHECK(d != NULL && leftovers == 0);
    CHECK(resolve_skin_arg(osk, "", arg, sizeof arg, msg, sizeof msg) == 0 && strstr(arg, "/Skins/My Skin " U_CHECK " (3)"));   /* an .osk path as skin argument imports it */
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--child")) return child_main(argc, argv);
    self = argv[0];
    char base[700];
    if (!plat_getenv("TMPDIR", base, sizeof base) && !plat_getenv("TEMP", base, sizeof base) && !plat_getenv("TMP", base, sizeof base))
        snprintf(base, sizeof base, "%s", plat_is_sep('\\') ? "." : "/tmp");
    snprintf(root, sizeof root, "%s/aimless-plat-XXXXXX", base);
    if (!plat_mkdtemp(root)) { perror("mkdtemp"); return 2; }
    for (char *p = root; *p; p++) if (*p == '\\') *p = '/';
    snprintf(data, sizeof data, "%s/data " U_JP " " U_NTILDE, root);       /* the "user profile": non-ASCII on purpose */
    plat_setenv("XDG_DATA_HOME", data);                                    /* data_dir() reads this once */
    CHECK(!strcmp(data_dir(), fmt("%s/aimless", data)));
    snprintf(data, sizeof data, "%s", data_dir());
    ensure_dirs();
    CHECK(plat_exists(fmt("%s/Songs", data)) && plat_exists(fmt("%s/Skins", data)));

    apos = 0; test_paths();
    apos = 0; test_files();
    apos = 0; test_trees();
    apos = 0; test_run();
    apos = 0; test_extract();
    apos = 0; test_builtin_zip();
    apos = 0; test_import_songs();
    apos = 0; test_import_skins();
    apos = 0; test_import_deflate();

    plat_remove_tree(root);
    CHECK(!plat_exists(root));
    printf("\n%d checks, %d failed%s\n", checks, fails, skipped ? ", some zip cases SKIPPED" : "");
    return fails ? 1 : 0;
}
