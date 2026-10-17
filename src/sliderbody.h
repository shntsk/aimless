#ifndef SLIDERBODY_H
#define SLIDERBODY_H

/* osu!lazer legacy-skin slider body, rasterised on the CPU (no SDL here, so it is unit-testable).
 *
 * Mirrors osu.Game.Rulesets.Osu/Skinning/Legacy/LegacySliderBody.cs (LegacyDrawableSliderPath.ColourAt) plus the
 * SmoothPath texture/shader in osu-framework: every pixel gets its distance to the centre line, position
 * p = 1 - dist / radius (0 at the outer edge, 1 on the centre line) indexes a colour ramp:
 *   p <= SB_SHADOW_PORTION  transparent black -> black @ 25 % (the soft drop shadow outside the coloured disc)
 *   p <= SB_BORDER_PORTION  SliderBorder, opaque
 *   otherwise               accent / 1.1 -> lighten(accent, 0.5), interpolated in sRGB, alpha SB_BODY_ALPHA
 * and the alpha is multiplied by min(p / SB_AA_PORTION, 1), the framework's edge antialiasing. */

#define SB_SHADOW_PORTION (1.0f - 59.0f / 64.0f)   /* 1 - LEGACY_CIRCLE_RADIUS / OBJECT_RADIUS */
#define SB_BORDER_PORTION 0.1875f
#define SB_BODY_ALPHA     0.7f                      /* legacy skins: constant, whatever the source colour */
#define SB_AA_PORTION     0.02f

/* RGBA (straight alpha, 0..1) at position p in [0,1]. accent = SliderTrackOverride or the combo colour. */
void sliderbody_colour(float p, const unsigned char accent[3], const unsigned char border[3], float out[4]);

/* Fills rgba[w*h*4] (byte order R,G,B,A, straight alpha) with the body of the polyline pts (n points, x,y in
 * pixels of this image, pixel (i,j) covers [i,i+1) x [j,j+1)). radius is the half width of the whole path in
 * pixels (lazer: OBJECT_RADIUS * scale, i.e. the same size the hit circle sprite is drawn at). */
void sliderbody_render(unsigned char *rgba, int w, int h, const float *pts, int n, float radius,
                       const unsigned char accent[3], const unsigned char border[3]);

#endif
