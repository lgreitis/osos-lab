/* SPDX-License-Identifier: GPL-3.0-only */

#include "theme_render.h"
#include "assets.h"
#include "osos.h"
#include "patch.h"

enum {
    BACKGROUND_LIGHT_MIN = 0xb0,
    BACKGROUND_DARK_BASE = 0x18,
    COVER_FLOW_BORDER_DARK = 0x38,
    TEXT_DARK_MAX = 0x80,
    TEXT_LIGHT_BASE = 0xe8,
};

int cfw_theme_enabled(void)
{
    return cfw_assets_enabled();
}

static int accent_color(const unsigned char *color, unsigned char *mapped)
{
    uint32_t accent = cfw_assets_accent();
    if (!cfw_theme_enabled() || !accent || color[2] <= color[0] + 12 ||
        color[2] <= color[1])
        return 0;

    /* Blue barely varies across the native ramp; preserve its full luminance. */
    int luminance = (77 * color[0] + 150 * color[1] + 29 * color[2]) / 256;
    int shade = luminance - 103;
    unsigned rgb[3] = {accent >> 16, (accent >> 8) & 255, accent & 255};
    for (unsigned i = 0; i < 3; i++)
        mapped[i] = shade >= 0 ? rgb[i] + (255 - rgb[i]) * shade / 152
                               : rgb[i] * luminance / 103;
    mapped[3] = color[3];
    return 1;
}

PATCH_ARM void cfw_theme_selection_color(void *context, const unsigned char *color)
{
    unsigned char mapped[4];
    osos_graphics_set_color(context, accent_color(color, mapped) ? mapped : color);
}

PATCH_ARM int cfw_theme_selection_changed(const int *rect, const int *cached)
{
    static uint32_t previous_accent;
    uint32_t accent = cfw_assets_accent();
    int changed = previous_accent != accent;
    previous_accent = accent;
    for (unsigned i = 0; i < 4; i++)
        changed |= rect[i] != cached[i];
    return changed;
}

/*
 * The Now Playing idle clock (shown while playing on battery) draws its digits and
 * icons as light bitmaps, so it keeps its light background. COLR data is one
 * 0x00RRGGBB word; the fill only darkens neutral grays, so a one-step tint is
 * enough to leave this color alone.
 */
const void *cfw_theme_color(uint32_t id, const void *color)
{
    static uint32_t idle_background;
    static uint32_t light;
    if (!cfw_theme_enabled() || !color)
        return color;

    if (!idle_background) {
        struct osos_string name;
        osos_string_init(&name, "NowPlaying_Idle_Background");
        idle_background = *osos_resource_name_slot(&name);
        osos_string_destroy(&name);
    }

    uint32_t rgb = *(const uint32_t *)color;
    if (id == 0x0dad00cf && cfw_assets_accent()) {
        light = cfw_assets_accent();
        return &light;
    }

    uint32_t blue = rgb & 0xff;
    if (id != idle_background || ((rgb >> 16) & 0xff) != blue ||
        ((rgb >> 8) & 0xff) != blue || blue < BACKGROUND_LIGHT_MIN)
        return color;

    light = rgb - 1;
    return &light;
}

PATCH_ARM void cfw_theme_fill(void *surface, const void *rect,
                              const unsigned char *color, uint32_t mode, void *clip)
{
    unsigned char mapped[4];
    int shade = -1;
    if (cfw_theme_enabled() && color[0] == color[1] && color[1] == color[2] &&
        color[0] >= BACKGROUND_LIGHT_MIN)
        shade = BACKGROUND_DARK_BASE + (255 - color[0]) / 4;
    /* CoverFlow_Backside_Border_Color (COLR 0x0dad05d8). */
    else if (cfw_theme_enabled() && color[0] == 0x7e && color[1] == 0x81 &&
             color[2] == 0x83)
        shade = COVER_FLOW_BORDER_DARK;

    if (shade >= 0) {
        mapped[0] = mapped[1] = mapped[2] = shade;
        mapped[3] = color[3];
        color = mapped;
    }

    osos_surface_fill_rect(surface, rect, color, mode, clip);
}

PATCH_ARM void cfw_theme_clear_color(int red, int green, int blue, int alpha)
{
    if (cfw_theme_enabled())
        red = green = blue = BACKGROUND_DARK_BASE * 0x10000 / 255;
    osos_gles_clear_color(red, green, blue, alpha);
}

PATCH_ARM uint32_t cfw_theme_reflection_blend(uint32_t pixel, uint32_t background,
                                              int weight)
{
    if (cfw_theme_enabled())
        background = ((BACKGROUND_DARK_BASE >> 3) << 11) |
                     ((BACKGROUND_DARK_BASE >> 2) << 5) | (BACKGROUND_DARK_BASE >> 3);
    return osos_rgb565_blend(pixel, background, weight);
}

PATCH_ARM void cfw_theme_text_color(void *context, const unsigned char *color)
{
    unsigned char mapped[4];
    if (cfw_theme_enabled() && color[0] == color[1] && color[1] == color[2] &&
        color[0] <= TEXT_DARK_MAX) {
        mapped[0] = mapped[1] = mapped[2] = TEXT_LIGHT_BASE - color[0] / 2;
        mapped[3] = color[3];
        color = mapped;
    }

    if (cfw_theme_enabled() && cfw_assets_accent() && color[0] == 35 &&
        color[1] == 108 && color[2] == 196) {
        uint32_t accent = cfw_assets_accent();
        mapped[0] = accent >> 16;
        mapped[1] = accent >> 8;
        mapped[2] = accent;
        mapped[3] = color[3];
        color = mapped;
    }

    osos_graphics_set_color(context, color);
}
