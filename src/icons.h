#ifndef ICONS_H
#define ICONS_H

/* Small SF-Symbols-style icon set for the Cupertino UI, drawn procedurally (no image files, no icon font).
 * Every icon lives in a unit square, uses round caps/joins and one stroke weight, like SF Symbols' "regular".
 * This file is pure C (no SDL) so the rasteriser can be unit-tested; gfx.c turns the coverage into textures. */
enum {
    ICON_CHEVRON_LEFT,      /* chevron.left */
    ICON_CHEVRON_RIGHT,     /* chevron.right */
    ICON_CHEVRON_UP,        /* chevron.up */
    ICON_CHEVRON_DOWN,      /* chevron.down */
    ICON_CHEVRON_UPDOWN,    /* chevron.up.chevron.down */
    ICON_PLAY,              /* play.fill */
    ICON_SHUFFLE,           /* shuffle */
    ICON_GEAR,              /* gearshape.fill */
    ICON_SEARCH,            /* magnifyingglass */
    ICON_XMARK_CIRCLE,      /* xmark.circle.fill */
    ICON_CLOCK,             /* clock */
    ICON_METRONOME,         /* metronome */
    ICON_PERSON,            /* person.fill */
    ICON_TARGET,            /* scope-like: ring + dot, used for the object count */
    ICON_SPEAKER,           /* speaker.wave.2 */
    ICON_SLIDERS,           /* slider.horizontal.3 */
    ICON_WINDOW,            /* macwindow */
    ICON_DISPLAY,           /* display */
    ICON_PALETTE,           /* paintpalette */
    ICON_CHECK,             /* checkmark */
    ICON_XMARK,             /* xmark */
    ICON_N
};

/* Writes px*px coverage values (0..255, row-major) for `icon`. `weight` scales the stroke: 0 regular,
 * 1 semibold, 2 bold. Returns 0 on success, -1 for an unknown icon or a size outside 4..256. */
int icon_raster(int icon, int px, int weight, unsigned char *alpha);

#endif
