/* SPDX-License-Identifier: MIT */

/* Colour bars that scroll with the image's frame count. */

#include "effect.h"

static const uint16_t colors[] = {0xf800, 0xfd20, 0xffe0, 0x07e0, 0x001f, 0x781f};

static void draw(const struct eapp_framebuffer *fb)
{
    enum { COUNT = sizeof colors / sizeof colors[0], WIDTH = 24 };
    for (int x = 0; x < EAPP_SCREEN_WIDTH; x += WIDTH) {
        unsigned band = (unsigned)(x / WIDTH + frames / 4) % COUNT;
        eapp_fill_rect(fb, x, 0, WIDTH, EAPP_SCREEN_HEIGHT, colors[band]);
    }
}

EAPP_MODULE(struct effect) = {"BARS", draw};
