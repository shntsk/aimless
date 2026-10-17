#include "sliderbody.h"
#include <math.h>
#include <stdlib.h>

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* LegacyDrawableSliderPath.lighten: "lightens a colour in a way more friendly to dark or strong colours" */
static float lighten1(float c, float amount) {
    amount *= 0.5f;
    return fminf(1, c * (1 + 0.5f * amount) + 1 * amount);
}

void sliderbody_colour(float p, const unsigned char accent[3], const unsigned char border[3], float out[4]) {
    if (p <= SB_SHADOW_PORTION) {
        /* InterpolateNonLinear(position, black@0, black@0.25, 0, shadow_portion) */
        out[0] = out[1] = out[2] = 0;
        out[3] = 0.25f * clamp01(p / SB_SHADOW_PORTION);
    } else if (p <= SB_BORDER_PORTION) {
        for (int i = 0; i < 3; i++) out[i] = border[i] / 255.0f;
        out[3] = 1;
    } else {
        float t = clamp01((p - SB_BORDER_PORTION) / (1 - SB_BORDER_PORTION));
        for (int i = 0; i < 3; i++) {
            float a = accent[i] / 255.0f;
            float outer = a / 1.1f;                /* Colour4.Darken(0.1): divide by max(1, 1 + amount) */
            float inner = lighten1(a, 0.5f);
            out[i] = outer + t * (inner - outer);
        }
        out[3] = SB_BODY_ALPHA;
    }
    out[3] *= fminf(p / SB_AA_PORTION, 1);         /* SmoothPath: alpha * min(progress / aa_portion, 1) */
}

#define LUT_N 1024

void sliderbody_render(unsigned char *rgba, int w, int h, const float *pts, int n, float radius,
                       const unsigned char accent[3], const unsigned char border[3]) {
    for (long i = 0; i < (long)w * h * 4; i++) rgba[i] = 0;
    if (n < 1 || radius <= 0 || w <= 0 || h <= 0) return;

    /* squared distance to the centre line, per pixel centre */
    float *d2 = malloc((size_t)w * h * sizeof(float));
    if (!d2) return;
    float r2 = radius * radius;
    for (long i = 0; i < (long)w * h; i++) d2[i] = r2;

    for (int k = 0; k < (n > 1 ? n - 1 : 1); k++) {
        float ax = pts[k*2], ay = pts[k*2+1];
        float bx = n > 1 ? pts[k*2+2] : ax, by = n > 1 ? pts[k*2+3] : ay;
        float dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy;
        int x0 = (int)floorf(fminf(ax, bx) - radius), x1 = (int)ceilf(fmaxf(ax, bx) + radius);
        int y0 = (int)floorf(fminf(ay, by) - radius), y1 = (int)ceilf(fmaxf(ay, by) + radius);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
        for (int y = y0; y < y1; y++) {
            float py = y + 0.5f - ay;
            float *row = d2 + (long)y * w;
            for (int x = x0; x < x1; x++) {
                float px = x + 0.5f - ax;
                float u = ll > 0 ? (px * dx + py * dy) / ll : 0;
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                float ex = px - u * dx, ey = py - u * dy, e = ex * ex + ey * ey;
                if (e < row[x]) row[x] = e;
            }
        }
    }

    /* colour ramp as a lookup table over p in [0,1] */
    static unsigned char lut[LUT_N + 1][4];
    for (int i = 0; i <= LUT_N; i++) {
        float c[4]; sliderbody_colour((float)i / LUT_N, accent, border, c);
        for (int k = 0; k < 4; k++) lut[i][k] = (unsigned char)(clamp01(c[k]) * 255.0f + 0.5f);
    }
    /* the table is rebuilt per call (cheap) so the static only avoids a large stack array */

    for (long i = 0; i < (long)w * h; i++) {
        if (d2[i] >= r2) continue;
        float p = 1 - sqrtf(d2[i]) / radius;
        float f = p * LUT_N; int i0 = (int)f; if (i0 >= LUT_N) i0 = LUT_N - 1;
        float fr = f - i0;
        for (int k = 0; k < 4; k++) rgba[i*4+k] = (unsigned char)(lut[i0][k] + (lut[i0+1][k] - lut[i0][k]) * fr + 0.5f);
    }
    free(d2);
}
