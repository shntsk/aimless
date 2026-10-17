#ifndef SKININI_H
#define SKININI_H

/* Tolerant skin.ini reader (osu! stable format). No SDL dependency.
 *
 * Handles: UTF-8 BOM, CRLF / CR / LF line ends, indented lines, "//" comment lines and trailing " //"
 * comments, "[Section]" headers, "Key: Value" with any spacing, arbitrarily long lines. Section and key
 * names are compared by the caller with strcasecmp. Unknown sections/keys are simply passed through. */

/* called once per "Key: Value" line; section is "" before the first header. Strings are trimmed. */
typedef void (*skinini_cb)(const char *section, const char *key, const char *value, void *ud);

/* Parse a buffer (not necessarily NUL-terminated: len bytes). */
void skinini_parse(const char *buf, long len, skinini_cb cb, void *ud);

/* Read and parse a file. Returns 0 on success, -1 if it cannot be opened/read (cb is not called). */
int skinini_read(const char *path, skinini_cb cb, void *ud);

/* "R,G,B" or "R,G,B,A" (spaces allowed, values clamped to 0..255). Returns 1 on success and fills rgb. */
int skinini_color(const char *value, int rgb[3]);

/* "1"/"0" (any integer) or "true"/"false". Returns 1 and sets *out when recognised. */
int skinini_bool(const char *value, int *out);

#endif
