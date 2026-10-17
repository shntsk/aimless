/* src/sliderbody.c against the constants of osu!lazer's LegacySliderBody (see the header). No SDL needed. */
#include "../src/sliderbody.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(float a, float b, float e) { return fabsf(a - b) <= e; }

int main(void) {
    const unsigned char red[3] = { 255, 0, 0 }, grey[3] = { 100, 120, 140 }, white[3] = { 255, 255, 255 }, bord[3] = { 10, 200, 30 };
    float c[4];

    /* border: opaque SliderBorder from the end of the shadow to 18.75 % */
    sliderbody_colour(0.15f, red, bord, c);
    CHECK(near(c[0], 10 / 255.f, 1e-4) && near(c[1], 200 / 255.f, 1e-4) && near(c[2], 30 / 255.f, 1e-4) && near(c[3], 1, 1e-4),
          "border colour %.3f %.3f %.3f %.3f", c[0], c[1], c[2], c[3]);

    /* edge of the body: accent / 1.1 (Darken(0.1)), alpha 0.7, no 35 % darkening */
    sliderbody_colour(SB_BORDER_PORTION + 1e-4f, grey, white, c);
    CHECK(near(c[0], 100 / 255.f / 1.1f, 2e-3) && near(c[1], 120 / 255.f / 1.1f, 2e-3) && near(c[2], 140 / 255.f / 1.1f, 2e-3),
          "outer %.4f %.4f %.4f", c[0], c[1], c[2]);
    CHECK(near(c[3], 0.7f, 1e-4), "outer alpha %.4f", c[3]);

    /* centre line: lighten(accent, 0.5): c*(1+0.125)+0.25 */
    sliderbody_colour(1.0f, grey, white, c);
    CHECK(near(c[0], 100 / 255.f * 1.125f + 0.25f, 1e-4) && near(c[1], 120 / 255.f * 1.125f + 0.25f, 1e-4), "inner %.4f %.4f", c[0], c[1]);
    CHECK(near(c[3], 0.7f, 1e-4), "inner alpha %.4f", c[3]);
    sliderbody_colour(1.0f, red, white, c);
    CHECK(near(c[0], 1, 1e-4) && near(c[1], 0.25f, 1e-4), "lighten saturates: %.4f %.4f", c[0], c[1]);

    /* shadow: black, 0 -> 25 %; fully transparent at the very edge (antialiasing) */
    sliderbody_colour(SB_SHADOW_PORTION * 0.999f, red, white, c);
    CHECK(c[0] == 0 && c[1] == 0 && c[2] == 0 && near(c[3], 0.25f, 5e-3), "shadow %.4f", c[3]);
    sliderbody_colour(0, red, white, c);
    CHECK(c[3] == 0, "edge alpha %.4f", c[3]);
    sliderbody_colour(SB_AA_PORTION / 2, red, white, c);
    CHECK(near(c[3], 0.25f * ((SB_AA_PORTION / 2) / SB_SHADOW_PORTION) * 0.5f, 1e-4), "aa ramp %.5f", c[3]);

    /* rendered horizontal slider: cross-section through the middle */
    enum { W = 200, H = 100 };
    float pts[] = { 60, 50, 140, 50 };
    static unsigned char px[W * H * 4];
    const float R = 30;
    sliderbody_render(px, W, H, pts, 2, R, grey, bord);
#define PX(x, y, k) px[((y) * W + (x)) * 4 + (k)]
    CHECK(PX(100, 5, 3) == 0 && PX(5, 5, 3) == 0, "far outside is transparent");
    CHECK(PX(100, 50 - 29, 3) > 0 && PX(100, 50 - 29, 3) < 70, "shadow ring is faint, got %d", PX(100, 50 - 29, 3));
    int by = 50 - (int)(R * (1 - 0.13f));                        /* p ~ 0.13: inside the border band */
    CHECK(PX(100, by, 0) == 10 && PX(100, by, 1) == 200 && PX(100, by, 2) == 30 && PX(100, by, 3) == 255,
          "border pixel %d %d %d %d", PX(100, by, 0), PX(100, by, 1), PX(100, by, 2), PX(100, by, 3));
    CHECK(near(PX(100, 50, 3), 0.7f * 255, 1.5f), "centre alpha %d", PX(100, 50, 3));
    CHECK(near(PX(100, 50, 0), (100 / 255.f * 1.125f + 0.25f) * 255, 4), "centre red %d", PX(100, 50, 0));
    CHECK(PX(100, 50 + 20, 0) < PX(100, 50, 0), "gradient gets lighter towards the centre");
    CHECK(PX(60 - 20, 50, 3) > 0 && PX(60 - 31, 50, 3) == 0, "round cap");
    /* symmetry */
    CHECK(PX(100, 50 - 12, 0) == PX(100, 50 + 11, 0), "symmetric across the centre line");

    /* banded rendering (game.c spreads one body over several frames) must give exactly the bytes of a full render,
     * for any band height, for a curved multi-segment path and for a single point */
    {
        enum { CW = 173, CH = 131, NP = 40 };
        float cp[NP * 2];
        for (int i = 0; i < NP; i++) { float u = i / (float)(NP - 1); cp[i*2] = 25 + 120 * u; cp[i*2+1] = 65 + 40 * sinf(u * 6.2831f); }
        static unsigned char full[CW * CH * 4], band[CW * CH * 4];
        int bands[] = { 1, 7, 16, 50, CH, CH + 9 };
        for (int variant = 0; variant < 2; variant++) {
            int np = variant ? 1 : NP;
            sliderbody_render(full, CW, CH, cp, np, 14.5f, grey, bord);
            for (int b = 0; b < (int)(sizeof bands / sizeof *bands); b++) {
                for (int i = 0; i < CW * CH * 4; i++) band[i] = 0xAA;      /* stale data must be overwritten */
                for (int y = 0; y < CH; y += bands[b]) sliderbody_render_rows(band, CW, CH, cp, np, 14.5f, grey, bord, y, y + bands[b]);
                CHECK(!memcmp(full, band, sizeof full), "banded render (%s, band %d rows) differs from the full render",
                      variant ? "single point" : "curve", bands[b]);
            }
        }
        /* a band only touches its own rows */
        for (int i = 0; i < CW * CH * 4; i++) band[i] = 0x55;
        sliderbody_render_rows(band, CW, CH, cp, NP, 14.5f, grey, bord, 40, 60);
        int untouched = 1;
        for (int i = 0; i < CW * CH * 4; i++) if ((i < 40 * CW * 4 || i >= 60 * CW * 4) && band[i] != 0x55) untouched = 0;
        CHECK(untouched, "sliderbody_render_rows wrote outside its rows");
        sliderbody_render_rows(band, CW, CH, cp, NP, 14.5f, grey, bord, -5, 0);     /* empty / out of range: no-op, no crash */
        sliderbody_render_rows(band, CW, CH, cp, NP, 14.5f, grey, bord, CH, CH + 5);
    }

    if (getenv("SLIDERBODY_DUMP")) {   /* preview on a dark playfield colour, for eyeballing only */
        FILE *f = fopen(getenv("SLIDERBODY_DUMP"), "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int i = 0; i < W * H; i++) for (int k = 0; k < 3; k++) {
                float a = px[i*4+3] / 255.f;
                fputc((int)(px[i*4+k] * a + 25 * (1 - a) + 0.5f), f);
            }
            fclose(f);
        }
    }
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_sliderbody: all checks passed\n");
    return 0;
}
