/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_render.h"
#include "theme_style.h"
#include "osos.h"
#include "patch.h"

/* Start, end and highlight for the RAM-color branch of the native selection bar.
 * The defaults are the firmware's own values for that branch (0x082a7cb8). */
static const unsigned char native_colors[3][4] = {
    {0x58, 0x98, 0xe8, 255}, {0x28, 0x64, 0xd0, 255}, {0x28, 0x64, 0xd0, 255}};
static unsigned char selection_gradient[2][4] __attribute__((aligned(4))) = {
    {0x58, 0x98, 0xe8, 255}, {0x28, 0x64, 0xd0, 255}};
static unsigned char selection_highlight[4] __attribute__((aligned(4))) = {0x28, 0x64, 0xd0, 255};
static int drawn_custom = -1;

PATCH_POINTER(OSOS_SELECTION_GRADIENT_LITERAL, 0x089d4ee8, selection_gradient);
PATCH_POINTER(OSOS_SELECTION_HIGHLIGHT_LITERAL, 0x089d4eec, selection_highlight);
PATCH_CALL_WORD(OSOS_SELECTION_STYLE_BRANCH, OSOS_SELECTION_STYLE_BRANCH_WORD,
                cfw_theme_selection_bar_hook);

/* Runs before the bar's cache check; a changed style empties the cached bounds so
 * the shared bar bitmap is redrawn. Nonzero selects the RAM-color branch. */
PATCH_ARM int cfw_theme_selection_bar_prepare(int native_ram)
{
    unsigned char next[3][4];
    int custom = cfw_theme_selection_bar_colors(next);
    const unsigned char *source = custom ? &next[0][0] : &native_colors[0][0];
    int changed = custom != drawn_custom;
    unsigned char *targets[3] = {selection_gradient[0], selection_gradient[1],
                                 selection_highlight};
    for (unsigned stop = 0; stop < 3; stop++)
        for (unsigned i = 0; i < 4; i++) {
            changed |= targets[stop][i] != source[stop * 4 + i];
            targets[stop][i] = source[stop * 4 + i];
        }
    if (changed) {
        uint32_t *bounds = (uint32_t *)OSOS_SELECTION_CACHE_BOUNDS;
        for (unsigned i = 0; i < 4; i++)
            bounds[i] = 0;
        drawn_custom = custom;
    }
    return custom || native_ram;
}
