/* src/icons.c: the procedural SF-Symbols-style icon rasteriser. Pure C, no SDL. */
#include "../src/icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define PX 32
static long sum(const unsigned char *a) { long s = 0; for (int i = 0; i < PX * PX; i++) s += a[i]; return s; }

int main(void) {
    unsigned char a[PX * PX], b[PX * PX], big[256 * 256];

    /* every icon draws something, stays inside its box (corners empty) and uses partial coverage for anti-aliasing */
    for (int i = 0; i < ICON_N; i++) {
        CHECK(icon_raster(i, PX, 0, a) == 0, "icon %d does not rasterise", i);
        long s = sum(a);
        CHECK(s > 255 * 20, "icon %d is (nearly) empty: %ld", i, s);
        CHECK(s < 255L * PX * PX * 8 / 10, "icon %d fills its whole box: %ld", i, s);
        CHECK(a[0] == 0 && a[PX - 1] == 0 && a[(PX - 1) * PX] == 0 && a[PX * PX - 1] == 0, "icon %d touches a corner", i);
        int partial = 0; for (int k = 0; k < PX * PX; k++) if (a[k] > 0 && a[k] < 255) partial++;
        CHECK(partial > 10, "icon %d has no anti-aliased edge", i);
    }

    /* bad arguments are refused, nothing is written */
    memset(a, 7, sizeof a);
    CHECK(icon_raster(-1, PX, 0, a) == -1 && icon_raster(ICON_N, PX, 0, a) == -1, "unknown icon accepted");
    CHECK(icon_raster(ICON_PLAY, 3, 0, a) == -1 && icon_raster(ICON_PLAY, 257, 0, a) == -1, "bad size accepted");
    CHECK(icon_raster(ICON_PLAY, PX, 0, NULL) == -1, "NULL buffer accepted");
    CHECK(a[0] == 7 && a[100] == 7, "buffer touched on error");

    /* left and right chevrons are mirror images */
    icon_raster(ICON_CHEVRON_LEFT, PX, 0, a); icon_raster(ICON_CHEVRON_RIGHT, PX, 0, b);
    int mirror = 1;
    for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) { int d = (int)a[y * PX + x] - (int)b[y * PX + PX - 1 - x]; if (d < -3 || d > 3) mirror = 0; }
    CHECK(mirror, "chevron.left is not the mirror of chevron.right");

    /* up/down chevrons are mirrored vertically */
    icon_raster(ICON_CHEVRON_UP, PX, 0, a); icon_raster(ICON_CHEVRON_DOWN, PX, 0, b);
    mirror = 1;
    for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) { int d = (int)a[y * PX + x] - (int)b[(PX - 1 - y) * PX + x]; if (d < -3 || d > 3) mirror = 0; }
    CHECK(mirror, "chevron.up is not the mirror of chevron.down");

    /* heavier weights draw more ink for stroked icons */
    icon_raster(ICON_SEARCH, PX, 0, a); long w0 = sum(a);
    icon_raster(ICON_SEARCH, PX, 1, a); long w1 = sum(a);
    icon_raster(ICON_SEARCH, PX, 2, a); long w2 = sum(a);
    CHECK(w0 < w1 && w1 < w2, "stroke weight not monotonic: %ld %ld %ld", w0, w1, w2);

    /* cut-outs: the gear has a hole, xmark.circle.fill has the x knocked out, the target keeps its dot */
    icon_raster(ICON_GEAR, PX, 0, a);
    CHECK(a[(PX / 2) * PX + PX / 2] == 0 && a[(PX / 2) * PX + PX / 2 + 6] > 200, "gear: centre %d, body %d", a[(PX / 2) * PX + PX / 2], a[(PX / 2) * PX + PX / 2 + 6]);
    icon_raster(ICON_XMARK_CIRCLE, PX, 0, a);
    CHECK(a[(PX / 2) * PX + PX / 2] < 30 && a[(PX / 2) * PX + 4] > 200, "xmark.circle.fill: centre %d, edge %d", a[(PX / 2) * PX + PX / 2], a[(PX / 2) * PX + 4]);
    icon_raster(ICON_TARGET, PX, 0, a);
    CHECK(a[(PX / 2) * PX + PX / 2] > 200 && a[(PX / 2) * PX + PX / 2 + 8] < 30, "target: dot %d, gap %d", a[(PX / 2) * PX + PX / 2], a[(PX / 2) * PX + PX / 2 + 8]);

    /* the largest allowed size works */
    CHECK(icon_raster(ICON_PERSON, 256, 2, big) == 0, "256 px icon failed");

    printf("icons: %s\n", fails ? "FAILED" : "ok");
    printf("%d failed\n", fails);
    return fails != 0;
}
