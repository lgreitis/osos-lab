/* SPDX-License-Identifier: GPL-3.0-only */

#include "doom_port.h"

#include <string.h>

static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-+:";
static const uint8_t glyphs[][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    {0, 0, 0, 31, 0, 0, 0},       {0, 4, 4, 31, 4, 4, 0},
    {0, 4, 4, 0, 4, 4, 0},
};

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

static void draw_text(const struct game_framebuffer *fb, unsigned x, unsigned y,
                      const char *value)
{
    for (; *value && x + 5 <= fb->width; ++value, x += 6) {
        char c = *value;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        const char *glyph = strchr(alphabet, c);
        if (!glyph)
            continue;

        for (unsigned dy = 0; dy < 7 && y + dy < fb->height; ++dy)
            for (unsigned dx = 0; dx < 5; ++dx)
                if (glyphs[glyph - alphabet][dy] & (16u >> dx))
                    fb->pixels[(y + dy) * fb->stride_pixels + x + dx] = 0xffff;
    }
}

void doom_console_draw(const struct game_framebuffer *fb)
{
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = 0;

    draw_text(fb, 4, 4, "DOOM STARTUP ERROR - PRESS ANY BUTTON TO EXIT");
    for (unsigned i = 0; i < 22; ++i)
        draw_text(fb, 4, 20 + i * 10, lines[i]);
}
