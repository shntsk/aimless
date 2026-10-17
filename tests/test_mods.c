/* src/mods.c: mod selection rules, difficulty adjustment and Hard Rock's flip on a really parsed beatmap. No SDL. */
#include "../src/mods.h"
#include "../src/beatmap.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(float a, float b) { return fabsf(a - b) < 0.01f; }

static const char *OSU =
    "osu file format v14\n[General]\nAudioFilename: a.mp3\n[Metadata]\nTitle:T\nArtist:A\nVersion:V\n"
    "[Difficulty]\nHPDrainRate:4\nCircleSize:4\nOverallDifficulty:6\nApproachRate:8\nSliderMultiplier:1.4\nSliderTickRate:1\n"
    "[TimingPoints]\n0,500,4,1,0,100,1,0\n"
    "[HitObjects]\n100,100,1000,5,0,0:0:0:0:\n"
    "200,50,2000,2,0,L|300:50,1,100\n";

int main(void) {
    /* selection rules */
    CHECK(mods_count() == 2 && mods_info(0) && mods_info(1) && !mods_info(2) && !mods_info(-1), "mod table");
    unsigned m = 0;
    m = mods_toggle(m, MOD_EASY);       CHECK(m == MOD_EASY, "EZ on: %u", m);
    m = mods_toggle(m, MOD_HARDROCK);   CHECK(m == MOD_HARDROCK, "HR replaces EZ: %u", m);
    m = mods_toggle(m, MOD_HARDROCK);   CHECK(m == 0, "HR off: %u", m);
    CHECK(mods_toggle(0, 1u << 7) == 0, "unknown bit accepted");
    CHECK(mods_sanitize(MOD_EASY | MOD_HARDROCK) == MOD_EASY, "conflict kept both");
    CHECK(mods_sanitize(0xFFu) == MOD_EASY, "unknown bits kept");
    CHECK(mods_active_count(0) == 0 && mods_active_count(MOD_HARDROCK) == 1, "active count");
    char a[32];
    mods_acronyms(0, a, sizeof a);              CHECK(!a[0], "acronyms none: '%s'", a);
    mods_acronyms(MOD_HARDROCK, a, sizeof a);   CHECK(!strcmp(a, "HR"), "acronyms HR: '%s'", a);
    mods_acronyms(MOD_EASY, a, 2);              CHECK(strlen(a) < 2, "acronyms overflow");
    CHECK(strstr(mods_unsupported(), "Double Time") && strstr(mods_unsupported(), "Hidden"), "unsupported list");

    /* difficulty adjustment */
    float cs = 4, ar = 8, od = 6, hp = 4;
    mods_adjust(0, &cs, &ar, &od, &hp);
    CHECK(near(cs, 4) && near(ar, 8) && near(od, 6) && near(hp, 4), "no mod changed values");
    mods_adjust(MOD_EASY, &cs, &ar, &od, &hp);
    CHECK(near(cs, 2) && near(ar, 4) && near(od, 3) && near(hp, 2), "EZ: %.2f %.2f %.2f %.2f", cs, ar, od, hp);
    cs = 4; ar = 8; od = 6; hp = 4;
    mods_adjust(MOD_HARDROCK, &cs, &ar, &od, &hp);
    CHECK(near(cs, 5.2f) && near(ar, 10) && near(od, 8.4f) && near(hp, 5.6f), "HR: %.2f %.2f %.2f %.2f", cs, ar, od, hp);
    cs = 9; ar = 9; od = 9; hp = 9;
    mods_adjust(MOD_HARDROCK, &cs, &ar, &od, &hp);
    CHECK(near(cs, 10) && near(ar, 10) && near(od, 10) && near(hp, 10), "HR cap 10");

    /* applied to a really parsed beatmap */
    const char *path = "tests/_mods_tmp.osu";
    FILE *f = fopen(path, "wb"); CHECK(f != NULL, "cannot write %s", path);
    if (!f) { printf("mods: FAILED\n"); return 1; }
    fputs(OSU, f); fclose(f);
    Beatmap b, hr, ez;
    CHECK(beatmap_load(&b, path, 0) == 0 && b.n == 2, "fixture did not load (n=%d)", b.n);
    CHECK(beatmap_load(&hr, path, 0) == 0 && beatmap_load(&ez, path, 0) == 0, "reload");
    remove(path);
    CHECK(b.n == 2 && b.objs[1].type == 1 && b.objs[1].npath > 1, "fixture slider has no path");
    if (b.n != 2 || !b.objs[1].path) { printf("mods: FAILED\n1 failed\n"); return 1; }

    mods_apply_beatmap(&b, 0);
    CHECK(near(b.cs, 4) && near(b.ar, 8) && near(b.od, 6) && near(b.objs[0].y, 100), "no mod must not change the map");

    mods_apply_beatmap(&ez, MOD_EASY);
    CHECK(near(ez.cs, 2) && near(ez.ar, 4) && near(ez.od, 3) && near(ez.objs[0].y, 100) && near(ez.objs[1].y, 50), "EZ on the map");

    float py0 = b.objs[1].path[1], py1 = b.objs[1].path[(b.objs[1].npath - 1) * 2 + 1];
    mods_apply_beatmap(&hr, MOD_HARDROCK);
    CHECK(near(hr.cs, 5.2f) && near(hr.ar, 10) && near(hr.od, 8.4f), "HR values on the map: %.2f %.2f %.2f", hr.cs, hr.ar, hr.od);
    CHECK(near(hr.objs[0].y, 284) && near(hr.objs[0].x, 100), "HR circle y=%.1f x=%.1f", hr.objs[0].y, hr.objs[0].x);
    CHECK(near(hr.objs[1].y, 334) && near(hr.objs[1].x, 200), "HR slider head y=%.1f", hr.objs[1].y);
    CHECK(near(hr.objs[1].path[1], 384 - py0) && near(hr.objs[1].path[(hr.objs[1].npath - 1) * 2 + 1], 384 - py1), "HR slider path not flipped");
    CHECK(near(hr.objs[1].path[0], b.objs[1].path[0]), "HR must not change x");
    CHECK(hr.objs[1].time == 2000 && hr.objs[1].endtime == b.objs[1].endtime && hr.n == 2, "HR changed timing");

    beatmap_free(&b); beatmap_free(&hr); beatmap_free(&ez);

    printf("mods: %s\n", fails ? "FAILED" : "ok");
    printf("%d failed\n", fails);
    return fails != 0;
}
