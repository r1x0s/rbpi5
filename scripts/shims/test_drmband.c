/*
 * test_drmband.c -- an overlay's place on a panel that is mounted upside down. No Pi, no DRM.
 *
 * drm_band_rot180_rect() is what turns a band's LOGICAL position (where rbp's picture has the
 * thing the band sits over) into the crtc position on a mode whose primary plane is scanned out
 * with ROTATE_180. What is pinned:
 *
 * 1. THE MEASURED CASE: deck 1's badge, 95x50 at 11,584 on 1280x800, lands at 1174,166 -- the
 *    spot it was seen inverted over the BEAT FX panel before the band followed the rotation.
 * 2. FULL-WIDTH AND FULL-SIZE BANDS: the top menu band (1280x56 at 0,0) goes to the bottom edge,
 *    and a band covering the whole mode stays at 0,0.
 * 3. IT IS ITS OWN INVERSE, at every corner, so nothing that maps a position twice can drift.
 */
#include <stdio.h>

#include "drmband.h"

static int checks, failures;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static void place(int x, int y, int w, int h, int ex, int ey, const char *who)
{
    int cx = x, cy = y;

    drm_band_rot180_rect(1280, 800, w, h, &cx, &cy);
    CHECK(cx == ex && cy == ey, "%s: %dx%d at %d,%d went to %d,%d, expected %d,%d",
          who, w, h, x, y, cx, cy, ex, ey);
}

int main(void)
{
    static const int pos[][4] = {
        { 0, 0, 95, 50 }, { 1185, 750, 95, 50 }, { 11, 584, 95, 50 },
        { 651, 584, 95, 50 }, { 0, 0, 1280, 56 }, { 300, 100, 1, 1 },
    };
    unsigned int i;

    place(11, 584, 95, 50, 1174, 166, "deck 1 badge");
    place(651, 584, 95, 50, 534, 166, "deck 2 badge");
    place(0, 0, 1280, 56, 0, 744, "menu band");
    place(0, 0, 1280, 800, 0, 0, "full-mode band");
    place(0, 0, 1, 1, 1279, 799, "top-left pixel");

    for (i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        int x = pos[i][0], y = pos[i][1];

        drm_band_rot180_rect(1280, 800, pos[i][2], pos[i][3], &x, &y);
        CHECK(x >= 0 && y >= 0 && x + pos[i][2] <= 1280 && y + pos[i][3] <= 800,
              "rect %u left the mode at %d,%d", i, x, y);
        drm_band_rot180_rect(1280, 800, pos[i][2], pos[i][3], &x, &y);
        CHECK(x == pos[i][0] && y == pos[i][1], "rect %u is not its own inverse", i);
    }

    printf("test_drmband: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
