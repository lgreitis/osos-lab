/* SPDX-License-Identifier: GPL-3.0-only */

#include "doom_port.h"

#include <string.h>

static char lines[22][52];
static unsigned row, column;

void doom_console_write(const char *text, size_t size)
{
    while (size--) {
        char c = *text++;
        if (c == '\r')
            continue;

        if (c == '\n' || column == 51) {
            column = 0;
            if (++row == 22) {
                memmove(lines, lines + 1, 21 * sizeof(lines[0]));
                row = 21;
            }
            memset(lines[row], 0, sizeof(lines[row]));
        }
        if (c != '\n')
            lines[row][column++] = c;
    }
}

void doom_console_draw(const struct game_framebuffer *fb)
{
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = 0;

    game_draw_text(fb, 4, 4, "DOOM STARTUP ERROR - PRESS ANY BUTTON TO EXIT", 0xffff);
    for (unsigned i = 0; i < 22; ++i)
        game_draw_text(fb, 4, 20 + i * 10, lines[i], 0xffff);
}
