/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_render.h"
#include "theme_style.h"
#include "osos.h"
#include "patch.h"

enum {
    /* Native colors are Classic light; these identify what each one draws. */
    BACKGROUND_LIGHT_MIN = 0xb0,
    TEXT_DARK_MAX = 0x80,
    TEXT_LIGHT_REFERENCE =
        0x20, /* Classic light text role; native black maps below it. */
    COVER_FLOW_BORDER_LIGHT =
        0x7e8183, /* CoverFlow_Backside_Border_Color (0x0dad05d8) */
    ACCENT_FILL_LIGHT = 0x236cc5,
    ACCENT_TEXT_LIGHT = 0x3185d0,
};

static int native_text_color(const void *view)
{
    switch (osos_view_id(view)) {
    case 0x0dad030e: /* Alarms_PickDate_Preivew_NumericDay */
    case 0x0dad0607: /* Settings_DateTime_NumericDay */
    case 0x0dad061e: /* Settings_DateTime_Preivew_NumericDay */
    case 0x0dad0ddc: /* Search_Input_Text */
        return 1;
    default:
        return 0;
    }
}

/* These views retain light artwork in both modes. Resolve policy at draw time. */
PATCH_ARM void cfw_theme_background_fill(void *context, const int32_t *rect,
                                         const void *view)
{
    switch (osos_view_id(view)) {
    case 0x0dad08c5: /* NowPlaying_White_Background_View */
    case 0x0dad0925: /* NowPlaying_Idle_Template */
    case 0x0dad092d: /* NowPlaying_ChapterArt_BackgroundImage_View */
    case 0x0dad09ae: /* RentalInfo_White_Background_View */
        osos_graphics_fill_rect_unmapped(context, rect);
        return;
    default:
        osos_graphics_fill_rect(context, rect);
    }
}

/* Classic light passes through untouched; any other style recolors native draws. */
static int themed(void)
{
    return cfw_theme_appearance_get(0) || cfw_theme_style_custom() ||
           cfw_theme_accent_get();
}

/* Accent presets keep native background clears and blends at their stock values. */
static int background_themed(void)
{
    return cfw_theme_appearance_get(0) || cfw_theme_style_custom();
}

static const unsigned char *role(enum cfw_theme_role value)
{
    return cfw_theme_color(value, cfw_theme_appearance_get(0));
}

static uint32_t rgb(const unsigned char *color)
{
    return (color[0] << 16) | (color[1] << 8) | color[2];
}

static int gray(const unsigned char *color)
{
    return color[0] == color[1] && color[1] == color[2];
}

static unsigned char clamp(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static const unsigned char *recolor(const unsigned char *color,
                                    const unsigned char *target, unsigned char *out)
{
    out[0] = target[0];
    out[1] = target[1];
    out[2] = target[2];
    out[3] = color[3];
    return out;
}

/* Offset each role channel by the native shade, retaining the native gradations. */
static const unsigned char *shade(const unsigned char *color,
                                  const unsigned char *target, int offset,
                                  unsigned char *out)
{
    for (unsigned i = 0; i < 3; i++)
        out[i] = clamp(target[i] + offset);
    out[3] = color[3];
    return out;
}

PATCH_ARM void cfw_theme_fill(void *surface, const void *rect,
                              const unsigned char *color, uint32_t mode, void *clip)
{
    unsigned char mapped[4];
    if (themed()) {
        const unsigned char *background = role(CFW_THEME_BACKGROUND);
        /* Lighter native grays become darker backgrounds; a dark role inverts them. */
        if (gray(color) && color[0] >= BACKGROUND_LIGHT_MIN)
            color = shade(color, background,
                          cfw_theme_color_dark(background) ? (255 - color[0]) / 4
                                                           : color[0] - 255,
                          mapped);
        else if (rgb(color) == COVER_FLOW_BORDER_LIGHT)
            color = recolor(color, role(CFW_THEME_COVER_BORDER), mapped);
        else if (rgb(color) == ACCENT_FILL_LIGHT)
            color = recolor(color, role(CFW_THEME_ACCENT_FILL), mapped);
    }
    osos_surface_fill_rect(surface, rect, color, mode, clip);
}

PATCH_ARM void cfw_theme_clear_color(int red, int green, int blue, int alpha)
{
    if (background_themed()) {
        const unsigned char *background = role(CFW_THEME_BACKGROUND);
        red = background[0] * 0x10000 / 255;
        green = background[1] * 0x10000 / 255;
        blue = background[2] * 0x10000 / 255;
    }
    osos_gles_clear_color(red, green, blue, alpha);
}

PATCH_ARM uint32_t cfw_theme_reflection_blend(uint32_t pixel, uint32_t background,
                                              int weight)
{
    if (background_themed()) {
        const unsigned char *color = role(CFW_THEME_BACKGROUND);
        background = ((color[0] >> 3) << 11) | ((color[1] >> 2) << 5) | (color[2] >> 3);
    }
    return osos_rgb565_blend(pixel, background, weight);
}

PATCH_ARM void cfw_theme_text_color(void *context, const unsigned char *color,
                                    const void *view)
{
    unsigned char mapped[4];
    if (themed() && !native_text_color(view)) {
        const unsigned char *text = role(CFW_THEME_TEXT);
        if (color == (const unsigned char *)OSOS_PALETTE_WHITE)
            color = recolor(color, role(CFW_THEME_ON_ACCENT), mapped);
        else if (rgb(color) == ACCENT_FILL_LIGHT)
            color = recolor(color, role(CFW_THEME_ACCENT_FILL), mapped);
        else if (rgb(color) == ACCENT_TEXT_LIGHT)
            color = recolor(color, role(CFW_THEME_ACCENT_TEXT), mapped);
        else if (gray(color) && color[0] <= TEXT_DARK_MAX)
            color = shade(color, text,
                          cfw_theme_color_dark(text) ? color[0] - TEXT_LIGHT_REFERENCE
                                                     : -(color[0] / 2),
                          mapped);
    }
    osos_graphics_set_color(context, color);
}
