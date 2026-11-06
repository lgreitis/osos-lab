/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_tint.h"
#include "assets.h"
#include "osos.h"
#include "theme_package.h"
#include "theme_style.h"

/* Asset whose palette currently holds the highlight color; CFW_THEME_MAX_ASSETS when
 * none. Set between the begin and end halves of one blit. */
static uint32_t highlighted_asset = CFW_THEME_MAX_ASSETS;

static const unsigned char *tint(uint32_t index)
{
    return cfw_theme_tint_color(index, cfw_theme_appearance_get(0));
}

static const unsigned char *highlight(void)
{
    return cfw_theme_color(CFW_THEME_ON_ACCENT, cfw_theme_appearance_get(0));
}

/* Palette entries and ARGB pixels are BGRA; coverage stays in the alpha byte.
 * Fully transparent ARGB pixels keep their bytes so stock art compares exactly. */
static void recolor(uint32_t index, const unsigned char *color)
{
    uint32_t colors, width, height, stride;
    unsigned char *entry = cfw_theme_asset_palette(index, &colors);
    if (entry) {
        for (; colors--; entry += 4) {
            entry[0] = color[2];
            entry[1] = color[1];
            entry[2] = color[0];
        }
        return;
    }
    unsigned char *row = cfw_theme_asset_argb(index, &width, &height, &stride);
    if (!row)
        return;
    for (; height--; row += stride)
        for (unsigned char *p = row, *end = row + width * 4; p < end; p += 4)
            if (p[3]) {
                p[0] = color[2];
                p[1] = color[1];
                p[2] = color[0];
            }
}

const unsigned char *cfw_theme_bitmap_tint(const unsigned char *pixels)
{
    uint32_t index;
    if (!pixels || !cfw_theme_style_custom())
        return 0;
    if (cfw_theme_asset_from_pixels(pixels, &index))
        return cfw_theme_tinted(index) ? tint(index) : 0;
    return 0;
}

const unsigned char *cfw_theme_highlight_tint(const void *view)
{
    const unsigned char *color = cfw_theme_bitmap_tint(osos_bitmap_view_pixels(view));
    return color && osos_view_highlighted(view) ? highlight() : color;
}

void cfw_theme_bitmap_blit_begin(const void *view, const void *context)
{
    uint32_t index;
    const unsigned char *pixels = osos_bitmap_context_pixels(context);
    if (!pixels || !cfw_theme_style_custom() || !osos_view_highlighted(view) ||
        !cfw_theme_asset_from_pixels(pixels, &index) || !cfw_theme_tinted(index))
        return;
    recolor(index, highlight());
    highlighted_asset = index;
}

void cfw_theme_bitmap_blit_end(void)
{
    if (highlighted_asset == CFW_THEME_MAX_ASSETS)
        return;
    recolor(highlighted_asset, tint(highlighted_asset));
    highlighted_asset = CFW_THEME_MAX_ASSETS;
}

void cfw_theme_tint_refresh(void)
{
    if (!cfw_theme_style_custom())
        return;
    for (uint32_t i = 0; i < CFW_THEME_MAX_ASSETS; i++)
        if (cfw_theme_tinted(i))
            recolor(i, tint(i));
}

/* The text or background role, whichever differs more in brightness from the
 * center pixel of the resident art; NULL when the pixel is unreadable or clear. */
static const unsigned char *contrast_color(uint32_t index)
{
    unsigned char art[4];
    if (!cfw_theme_asset_center_pixel(index, art))
        return 0;
    int dark = cfw_theme_appearance_get(0);
    const unsigned char *text = cfw_theme_color(CFW_THEME_TEXT, dark);
    const unsigned char *background = cfw_theme_color(CFW_THEME_BACKGROUND, dark);
    int reference = cfw_theme_brightness(art);
    int text_gap = cfw_theme_brightness(text) - reference;
    int background_gap = cfw_theme_brightness(background) - reference;
    if (text_gap < 0)
        text_gap = -text_gap;
    if (background_gap < 0)
        background_gap = -background_gap;
    return text_gap >= background_gap ? text : background;
}

const unsigned char *cfw_theme_well_letter_color(unsigned style)
{
    if (style > 1 || !cfw_theme_style_custom())
        return 0;
    return contrast_color(cfw_bitmap_well_centers[style]);
}

const unsigned char *cfw_theme_field_text_color(void)
{
    if (!cfw_theme_style_custom())
        return 0;
    return contrast_color(cfw_bitmap_field_center);
}
