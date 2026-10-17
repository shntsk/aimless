#ifndef SKINFS_H
#define SKINFS_H
#include <stddef.h>

/* File lookup inside a skin (or beatmap) folder. No SDL dependency.
 *
 * Skins are authored on Windows, where file names are case-insensitive, so "Hitcircle.PNG" or
 * "Fonts\Hit\Score-0.png" must resolve on Linux too. Lookups try the exact spelling first and only then
 * fall back to a case-insensitive directory scan, one path component at a time. Paths can never leave
 * the folder they are resolved against (no "..", no absolute paths). */

/* Resolve "<base><suffix>" inside dir. base may contain sub-folders, with '/' or '\\' as separator
 * (skin.ini [Fonts] prefixes look like "fonts/hit/score"). Returns 1 and writes the real path to out
 * when a regular file exists, else 0. */
int skinfs_resolve(const char *dir, const char *base, const char *suffix, char *out, size_t n);

/* Image lookup order of osu! stable, one candidate per index: 0 name@2x.png, 1 name@2x.jpg, 2 name.png, 3 name.jpg.
 * Returns 1 and fills out (real path) and *scale (2 for @2x, else 1) when candidate idx exists, else 0.
 * Every skin element is looked up on its own, so a skin may mix e.g. hitcircle@2x.png with a 1x overlay. */
#define SKINFS_N_IMG 4
int skinfs_image_candidate(const char *dir, const char *name, int idx, int *scale, char *out, size_t n);

/* 1 if path is an existing directory */
int skinfs_isdir(const char *path);

/* 1 if dir looks like a skin: has a skin.ini (any case) or at least one image/sound file at its top level */
int skinfs_looks_like_skin(const char *dir);

#endif
