/* SPDX-License-Identifier: MIT */

/* Stars that fly towards the viewer, kept in the module's own bss, which
 * starts zeroed, so every star spawns on the first frame, each at its own
 * depth. */

#include "effect.h"

enum { STARS = 96 };
static struct {
    int x, y, z;
} stars[STARS];

static void draw(const struct eapp_framebuffer *fb)
{
    eapp_fill_rect(fb, 0, 0, EAPP_SCREEN_WIDTH, EAPP_SCREEN_HEIGHT, 0x0000);
    for (unsigned i = 0; i < STARS; ++i) {
        if (stars[i].z <= 4) {
            stars[i].x = (int)eapp_random_below(2000) - 1000;
            stars[i].y = (int)eapp_random_below(2000) - 1000;
            stars[i].z = 64 + (int)eapp_random_below(192);
        }
        stars[i].z -= 4;
        int x = EAPP_SCREEN_WIDTH / 2 + stars[i].x * 32 / stars[i].z;
        int y = EAPP_SCREEN_HEIGHT / 2 + stars[i].y * 32 / stars[i].z;
        int size = stars[i].z < 64 ? 2 : 1;
        eapp_fill_rect(fb, x, y, size, size, 0xffff);
    }
}

EAPP_MODULE(struct effect) = {"STARS", draw};
