#include "icons.h"
#include <math.h>
#include <string.h>

/* An icon is a list of signed-distance primitives in the unit square. "Add" primitives are united (max of coverage),
 * "cut" primitives are then removed from the result (used for the xmark in xmark.circle.fill and the gear hole). */
enum { P_LINE, P_RING, P_DISC, P_TRI };
typedef struct { int kind, cut; float v[6]; float w; } Prim;   /* w: stroke width (LINE/RING) or corner radius (TRI) */
#define MAXP 24

static float sc_w = 1;                      /* stroke scale for the current icon */
#define SW 0.085f                           /* regular stroke, fraction of the icon size */

static int add(Prim *p, int n, int kind, int cut, float a, float b, float c, float d, float e, float f, float w) {
    if (n >= MAXP) return n;
    p[n].kind = kind; p[n].cut = cut; p[n].v[0] = a; p[n].v[1] = b; p[n].v[2] = c; p[n].v[3] = d; p[n].v[4] = e; p[n].v[5] = f; p[n].w = w;
    return n + 1;
}
#define LINE(a, b, c, d)     n = add(p, n, P_LINE, 0, a, b, c, d, 0, 0, SW * sc_w)
#define CUTLINE(a, b, c, d)  n = add(p, n, P_LINE, 1, a, b, c, d, 0, 0, SW * 1.15f * sc_w)
#define RING(x, y, r)        n = add(p, n, P_RING, 0, x, y, r, 0, 0, 0, SW * sc_w)
#define DISC(x, y, r)        n = add(p, n, P_DISC, 0, x, y, r, 0, 0, 0, 0)
#define CUTDISC(x, y, r)     n = add(p, n, P_DISC, 1, x, y, r, 0, 0, 0, 0)
#define TRI(a, b, c, d, e, f, r) n = add(p, n, P_TRI, 0, a, b, c, d, e, f, r)

static int build(int icon, Prim *p) {
    int n = 0;
    switch (icon) {
    case ICON_CHEVRON_LEFT:  LINE(0.62f, 0.16f, 0.30f, 0.50f); LINE(0.30f, 0.50f, 0.62f, 0.84f); break;
    case ICON_CHEVRON_RIGHT: LINE(0.38f, 0.16f, 0.70f, 0.50f); LINE(0.70f, 0.50f, 0.38f, 0.84f); break;
    case ICON_CHEVRON_UP:    LINE(0.16f, 0.62f, 0.50f, 0.30f); LINE(0.50f, 0.30f, 0.84f, 0.62f); break;
    case ICON_CHEVRON_DOWN:  LINE(0.16f, 0.38f, 0.50f, 0.70f); LINE(0.50f, 0.70f, 0.84f, 0.38f); break;
    case ICON_CHEVRON_UPDOWN:
        LINE(0.28f, 0.38f, 0.50f, 0.17f); LINE(0.50f, 0.17f, 0.72f, 0.38f);
        LINE(0.28f, 0.62f, 0.50f, 0.83f); LINE(0.50f, 0.83f, 0.72f, 0.62f); break;
    case ICON_PLAY:   TRI(0.27f, 0.17f, 0.27f, 0.83f, 0.81f, 0.50f, 0.07f * sc_w); break;
    case ICON_SHUFFLE:
        LINE(0.08f, 0.31f, 0.30f, 0.31f); LINE(0.30f, 0.31f, 0.60f, 0.69f); LINE(0.60f, 0.69f, 0.80f, 0.69f);
        LINE(0.08f, 0.69f, 0.30f, 0.69f); LINE(0.30f, 0.69f, 0.41f, 0.55f);
        LINE(0.50f, 0.43f, 0.60f, 0.31f);  LINE(0.60f, 0.31f, 0.80f, 0.31f);
        LINE(0.72f, 0.21f, 0.84f, 0.31f); LINE(0.84f, 0.31f, 0.72f, 0.41f);
        LINE(0.72f, 0.59f, 0.84f, 0.69f); LINE(0.84f, 0.69f, 0.72f, 0.79f); break;
    case ICON_GEAR:
        DISC(0.5f, 0.5f, 0.33f);
        for (int i = 0; i < 8; i++) {
            float a = i * 0.7853982f, c = cosf(a), s = sinf(a);
            n = add(p, n, P_LINE, 0, 0.5f + c * 0.31f, 0.5f + s * 0.31f, 0.5f + c * 0.375f, 0.5f + s * 0.375f, 0, 0, 0.15f);
        }
        CUTDISC(0.5f, 0.5f, 0.13f); break;
    case ICON_SEARCH: RING(0.43f, 0.43f, 0.28f); LINE(0.64f, 0.64f, 0.84f, 0.84f); break;
    case ICON_XMARK_CIRCLE:
        DISC(0.5f, 0.5f, 0.47f);
        CUTLINE(0.35f, 0.35f, 0.65f, 0.65f); CUTLINE(0.65f, 0.35f, 0.35f, 0.65f); break;
    case ICON_CLOCK: RING(0.5f, 0.5f, 0.41f); LINE(0.5f, 0.5f, 0.5f, 0.25f); LINE(0.5f, 0.5f, 0.68f, 0.60f); break;
    case ICON_METRONOME:
        LINE(0.40f, 0.12f, 0.60f, 0.12f); LINE(0.40f, 0.12f, 0.20f, 0.86f); LINE(0.60f, 0.12f, 0.80f, 0.86f);
        LINE(0.20f, 0.86f, 0.80f, 0.86f); LINE(0.5f, 0.66f, 0.66f, 0.30f); break;
    case ICON_PERSON: DISC(0.5f, 0.30f, 0.18f); n = add(p, n, P_LINE, 0, 0.34f, 0.78f, 0.66f, 0.78f, 0, 0, 0.38f); break;
    case ICON_TARGET: RING(0.5f, 0.5f, 0.41f); DISC(0.5f, 0.5f, 0.15f); break;
    case ICON_SPEAKER:
        n = add(p, n, P_LINE, 0, 0.18f, 0.5f, 0.27f, 0.5f, 0, 0, 0.22f);
        TRI(0.56f, 0.20f, 0.56f, 0.80f, 0.26f, 0.5f, 0.03f * sc_w);
        LINE(0.70f, 0.38f, 0.74f, 0.50f); LINE(0.74f, 0.50f, 0.70f, 0.62f);
        LINE(0.78f, 0.28f, 0.86f, 0.50f); LINE(0.86f, 0.50f, 0.78f, 0.72f); break;
    case ICON_SLIDERS:
        LINE(0.12f, 0.26f, 0.88f, 0.26f); LINE(0.12f, 0.50f, 0.88f, 0.50f); LINE(0.12f, 0.74f, 0.88f, 0.74f);
        DISC(0.64f, 0.26f, 0.11f); DISC(0.34f, 0.50f, 0.11f); DISC(0.70f, 0.74f, 0.11f); break;
    case ICON_WINDOW:
        LINE(0.14f, 0.20f, 0.86f, 0.20f); LINE(0.86f, 0.20f, 0.86f, 0.80f); LINE(0.86f, 0.80f, 0.14f, 0.80f); LINE(0.14f, 0.80f, 0.14f, 0.20f);
        LINE(0.14f, 0.38f, 0.86f, 0.38f); break;
    case ICON_DISPLAY:
        LINE(0.10f, 0.20f, 0.90f, 0.20f); LINE(0.90f, 0.20f, 0.90f, 0.64f); LINE(0.90f, 0.64f, 0.10f, 0.64f); LINE(0.10f, 0.64f, 0.10f, 0.20f);
        LINE(0.5f, 0.64f, 0.5f, 0.82f); LINE(0.32f, 0.84f, 0.68f, 0.84f); break;
    case ICON_PALETTE:
        RING(0.5f, 0.5f, 0.41f);
        DISC(0.34f, 0.42f, 0.07f); DISC(0.52f, 0.30f, 0.07f); DISC(0.68f, 0.46f, 0.07f); DISC(0.42f, 0.64f, 0.07f); break;
    case ICON_CHECK: LINE(0.18f, 0.54f, 0.40f, 0.76f); LINE(0.40f, 0.76f, 0.84f, 0.26f); break;
    case ICON_XMARK: LINE(0.22f, 0.22f, 0.78f, 0.78f); LINE(0.78f, 0.22f, 0.22f, 0.78f); break;
    default: return 0;
    }
    return n;
}

static float sd_seg(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = (pax * bax + pay * bay) / (bax * bax + bay * bay + 1e-9f);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return hypotf(pax - bax * h, pay - bay * h);
}

/* signed distance to a triangle (negative inside), any winding */
static float sd_tri(float px, float py, const float *v) {
    float d = 1e9f;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float s = sd_seg(px, py, v[i * 2], v[i * 2 + 1], v[j * 2], v[j * 2 + 1]);
        if (s < d) d = s;
    }
    int pos = 0, neg = 0;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float cr = (v[j * 2] - v[i * 2]) * (py - v[i * 2 + 1]) - (v[j * 2 + 1] - v[i * 2 + 1]) * (px - v[i * 2]);
        if (cr >= 0) pos++; else neg++;
    }
    return (pos == 3 || neg == 3) ? -d : d;
}

static float dist(const Prim *q, float x, float y) {
    switch (q->kind) {
    case P_LINE: return sd_seg(x, y, q->v[0], q->v[1], q->v[2], q->v[3]) - q->w * 0.5f;
    case P_RING: return fabsf(hypotf(x - q->v[0], y - q->v[1]) - q->v[2]) - q->w * 0.5f;
    case P_DISC: return hypotf(x - q->v[0], y - q->v[1]) - q->v[2];
    default:     return sd_tri(x, y, q->v) - q->w;      /* rounded: shrink the corners by w, then grow back */
    }
}

int icon_raster(int icon, int px, int weight, unsigned char *alpha) {
    if (icon < 0 || icon >= ICON_N || px < 4 || px > 256 || !alpha) return -1;
    sc_w = weight <= 0 ? 1.0f : weight == 1 ? 1.3f : 1.6f;
    Prim p[MAXP];
    int n = build(icon, p);
    if (!n) return -1;
    /* the rounded triangle is inset so the rounding does not grow it past its nominal outline */
    for (int i = 0; i < n; i++) if (p[i].kind == P_TRI) {
        float *v = p[i].v, cx = (v[0] + v[2] + v[4]) / 3, cy = (v[1] + v[3] + v[5]) / 3;
        for (int k = 0; k < 3; k++) {
            float dx = v[k * 2] - cx, dy = v[k * 2 + 1] - cy, l = hypotf(dx, dy);
            float s = (l - p[i].w * 1.4f) / l;
            v[k * 2] = cx + dx * s; v[k * 2 + 1] = cy + dy * s;
        }
    }
    for (int y = 0; y < px; y++) for (int x = 0; x < px; x++) {
        float ux = (x + 0.5f) / px, uy = (y + 0.5f) / px, add_c = 0, cut_c = 0;
        for (int i = 0; i < n; i++) {
            float c = 0.5f - dist(&p[i], ux, uy) * px;      /* 1 px wide linear edge */
            c = c < 0 ? 0 : c > 1 ? 1 : c;
            if (p[i].cut) { if (c > cut_c) cut_c = c; } else if (c > add_c) add_c = c;
        }
        float a = add_c * (1 - cut_c);
        alpha[y * px + x] = (unsigned char)(a * 255 + 0.5f);
    }
    return 0;
}
