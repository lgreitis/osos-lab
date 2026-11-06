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

typedef void (*pixel_fn)(unsigned char *bgra, const unsigned char *color);

/* Palette entries and ARGB pixels are BGRA; coverage stays in the alpha byte.
 * Every palette entry is visited; fully transparent ARGB pixels keep their bytes
 * so stock art compares exactly. */
static void each_pixel(uint32_t index, pixel_fn fn, const unsigned char *color)
{
    uint32_t colors, width, height, stride;
    unsigned char *entry = cfw_theme_asset_palette(index, &colors);
    if (entry) {
        for (; colors--; entry += 4)
            fn(entry, color);
        return;
    }
    unsigned char *row = cfw_theme_asset_argb(index, &width, &height, &stride);
    if (!row)
        return;
    for (; height--; row += stride)
        for (unsigned char *p = row, *end = row + width * 4; p < end; p += 4)
            if (p[3])
                fn(p, color);
}

static void fill(unsigned char *bgra, const unsigned char *color)
{
    bgra[0] = color[2];
    bgra[1] = color[1];
    bgra[2] = color[0];
}

static void recolor(uint32_t index, const unsigned char *color)
{
    each_pixel(index, fill, color);
}

/* Stock accent art is shaded around the stock selection blue (#236cc5). */
static const unsigned char stock_accent[4] = {35, 108, 197, 255};

enum {
    /* Chroma (max - min channel) below GRAY_CHROMA is gray art: letters, gloss
     * and shadows keep their color. Full recoloring starts at FULL_CHROMA;
     * antialiased edges in between are blended. */
    GRAY_CHROMA = 24,
    FULL_CHROMA = 96,
    BRIGHTNESS_MAX = 255 * 256,
};

/* Moves color toward white or black by how much brighter or darker the stock
 * pixel is than the stock blue, so the stock blue becomes color exactly and the
 * art keeps its gradients. */
static void colorize(unsigned char *bgra, const unsigned char *color)
{
    static const unsigned char white[4] = {255, 255, 255, 255},
                               black[4] = {0, 0, 0, 255};
    unsigned char stock[4] = {bgra[2], bgra[1], bgra[0], 255}, shaded[4];
    int high = stock[0], low = stock[0];
    for (unsigned i = 1; i < 3; i++) {
        high = stock[i] > high ? stock[i] : high;
        low = stock[i] < low ? stock[i] : low;
    }
    int chroma = high - low;
    if (chroma < GRAY_CHROMA)
        return;
    int brightness = cfw_theme_brightness(stock);
    int base = cfw_theme_brightness(stock_accent);
    if (brightness >= base)
        cfw_theme_mix(shaded, color, white,
                      (unsigned)(brightness - base) * 255 / (BRIGHTNESS_MAX - base));
    else
        cfw_theme_mix(shaded, color, black, (unsigned)(base - brightness) * 255 / base);
    unsigned weight = chroma >= FULL_CHROMA ? 255
                                            : (chroma - GRAY_CHROMA) * 255 /
                                                  (FULL_CHROMA - GRAY_CHROMA);
    cfw_theme_mix(shaded, stock, shaded, weight);
    fill(bgra, shaded);
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

void cfw_theme_accent_art_apply(const uint32_t *skip, uint32_t *touched)
{
    if (!cfw_theme_accent_art_active())
        return;
    int dark = cfw_theme_appearance_get(0);
    for (uint32_t i = 0; i < cfw_bitmap_accent_art_count; i++) {
        uint32_t index = cfw_bitmap_accent_art[i].index, mask = 1u << (index % 32);
        if (index >= CFW_THEME_MAX_ASSETS || (skip[index / 32] & mask))
            continue;
        enum cfw_theme_role role = (enum cfw_theme_role)cfw_bitmap_accent_art[i].role;
        each_pixel(index, colorize, cfw_theme_color(role, dark));
        touched[index / 32] |= mask;
    }
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
