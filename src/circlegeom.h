#ifndef CIRCLEGEOM_H
#define CIRCLEGEOM_H
/* Layout rules for the hit circle sprites. Pure arithmetic, no SDL, so tests/test_skin.c can check them.
 *
 * Skin sprites keep their own proportions: the hitcircle sprite is stretched to the on-screen circle
 * diameter, and every other circle sprite (overlay, approach circle) is drawn at its own logical size
 * (pixels / 2 for @2x files) multiplied by that same factor. A skin whose approach circle is a larger
 * canvas than its hitcircle therefore gets a larger approach ring, exactly as authored. */

/* factor that maps a sprite's logical size to screen px; hitcircle_w is the hitcircle's logical width */
static inline float cg_k(float diameter, int hitcircle_w) { return hitcircle_w > 0 ? diameter / (float)hitcircle_w : 1.0f; }

/* approach circle scale relative to its natural size: 4x when the circle appears, 1x at hit time */
static inline float cg_approach(float prog) {
    if (prog < 0) prog = 0;
    if (prog > 1) prog = 1;
    return 1.0f + 3.0f * (1.0f - prog);
}

/* draw order of the circle head layers, bottom to top. The approach circle is not part of it: it goes
 * above every circle on screen, in a separate pass. skin.ini HitCircleOverlayAboveNumber (default 1). */
enum { CG_BASE, CG_NUMBER, CG_OVERLAY };
static inline void cg_layers(int overlay_above_number, int order[3]) {
    order[0] = CG_BASE;
    order[1] = overlay_above_number ? CG_NUMBER : CG_OVERLAY;
    order[2] = overlay_above_number ? CG_OVERLAY : CG_NUMBER;
}
#endif
