/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_render.h"
#include "theme_style.h"
#include "theme_tint.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

enum {
    /* Native colors are Classic light; these identify what each one draws. */
    BACKGROUND_LIGHT_MIN = 0xb0,
    TEXT_DARK_MAX = 0x80,
    TEXT_WHITE_MIN = 0xf0,
    TEXT_LIGHT_REFERENCE =
        0x20, /* Classic light text role; native black maps below it. */
    COVER_FLOW_BORDER_LIGHT =
        0x7e8183, /* CoverFlow_Backside_Border_Color (0x0dad05d8) */
    ACCENT_FILL_LIGHT = 0x236cc5,
    ACCENT_TEXT_LIGHT = 0x3185d0,
};

/* Two native views draw labels on OptionBar well and thumb slices, each holding
 * two RGBA label colors, both system white: the Search keyboard (loader
 * 0x08203880, Black slices, key drawer 0x08203e8c) and the generic option bar
 * such as the Clock's Add/Edit (loader 0x081dfe38 picks the slices by a style
 * byte, draw loop 0x081e091c). */
enum {
    SEARCH_KEY_WELL_LETTER = 0x1cd,
    SEARCH_KEY_LETTER = 0x1d1,
    OPTION_BAR_STYLE = 0x121, /* 0 White, 1 Black, 2 Now Playing (own colors) */
    OPTION_BAR_WELL_LETTER = 0x1cc,
    OPTION_BAR_LETTER = 0x1d0,
    OPTION_BAR_THEMED_STYLES = 2,
};

enum { WELL_WHITE, WELL_BLACK };

/* The Search text box draws its typed text and caret over the
 * System_Input_Field slices. Classic keeps the stock light field, so the text
 * stays native there; a custom theme may paint the field light or dark, and the
 * text then follows whatever contrasts with the field art. */
enum { SEARCH_INPUT_TEXT = 0x0dad0ddc };

/* The NowPlaying_StatusBar_Image header (Stopwatch, Charging) draws its title in
 * a fixed light gray that no role mapping covers. A custom theme may paint the
 * bar light or dark, so the title follows whatever contrasts with the bar art. */
enum { STATUS_BAR_TITLE = 0x0dad020e };

static int native_text_color(const void *view)
{
    switch (osos_view_id(view)) {
    case 0x0dad030e: /* Alarms_PickDate_Preivew_NumericDay */
    case 0x0dad0607: /* Settings_DateTime_NumericDay */
    case 0x0dad061e: /* Settings_DateTime_Preivew_NumericDay */
        return 1;
    case SEARCH_INPUT_TEXT:
        return !cfw_theme_style_custom();
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

/* Now Playing draws its cover with GLES into a 320 by 240 RGB565 color buffer:
 * the tilted, reflected artwork, then antialiased lines along its edges. A flat
 * theme copies the artwork's pixels into the buffer instead, face-on and
 * unscaled where the stock cover starts, and skips the edge lines. */
enum {
    ALBUM_ART_TARGET_WIDTH = 320,
    ALBUM_ART_SIZE = 128,
    FLAT_COVER_X = 18,
    FLAT_COVER_Y = 38,
};

static const void *drawing_view;

static int flat_artwork(void)
{
    return cfw_theme_render_flags() & CFW_THEME_FLAT_ARTWORK;
}

PATCH_ARM void cfw_theme_album_art_view_draw(void *view)
{
    drawing_view = view;
    osos_album_art_view_draw(view);
    drawing_view = NULL;
}

PATCH_ARM void cfw_theme_album_art_draw(void *renderer)
{
    if (!flat_artwork() || !drawing_view) {
        osos_artwork_renderer_draw(renderer);
        return;
    }
    const unsigned char *image = osos_album_art_view_image(drawing_view);
    const uint16_t *pixels;
    if (!image)
        return;
    cfw_copy(&pixels, image + 4, sizeof(pixels));
    uint16_t *target = osos_gles_render_target() +
                       FLAT_COVER_Y * ALBUM_ART_TARGET_WIDTH + FLAT_COVER_X;
    for (int row = 0; row < ALBUM_ART_SIZE; row++, target += ALBUM_ART_TARGET_WIDTH)
        for (int column = 0; column < ALBUM_ART_SIZE; column++)
            target[column] = *pixels++;
}

PATCH_ARM void cfw_theme_album_art_edge_lines(const void *bitmap)
{
    if (!flat_artwork())
        osos_artwork_edge_lines(bitmap);
}

/* Native white text sits on the stock blue-gray preview pane and on dark art. A
 * custom light background would hide it, so it takes the text role there. */
PATCH_ARM void cfw_theme_text_color(void *context, const unsigned char *color,
                                    const void *view)
{
    unsigned char mapped[4];
    if (themed() && !native_text_color(view)) {
        const unsigned char *text = role(CFW_THEME_TEXT);
        const unsigned char *field =
            osos_view_id(view) == SEARCH_INPUT_TEXT ? cfw_theme_field_text_color() : 0;
        const unsigned char *bar =
            osos_view_id(view) == STATUS_BAR_TITLE ? cfw_theme_status_bar_text_color() : 0;
        if (bar)
            color = recolor(color, bar, mapped);
        else if (field && gray(color) && color[0] <= TEXT_DARK_MAX)
            color = recolor(color, field, mapped);
        else if (color == (const unsigned char *)OSOS_PALETTE_WHITE)
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
        else if (gray(color) && color[0] >= TEXT_WHITE_MIN &&
                 cfw_theme_style_custom() &&
                 !cfw_theme_color_dark(role(CFW_THEME_BACKGROUND)))
            color = recolor(color, text, mapped);
    }
    osos_graphics_set_color(context, color);
}

/* The highlighted label sits on the selection-tinted thumb, so it takes
 * on_accent; the other labels follow whatever contrasts with the well art. */
static const unsigned char *bar_label_color(const unsigned char *color,
                                            const unsigned char *bytes, unsigned thumb,
                                            unsigned well, unsigned style,
                                            unsigned char *mapped)
{
    if (color == bytes + thumb) {
        if (themed())
            return recolor(color, role(CFW_THEME_ON_ACCENT), mapped);
    } else if (color == bytes + well) {
        const unsigned char *derived = cfw_theme_well_letter_color(style);
        if (derived)
            return recolor(color, derived, mapped);
    }
    return color;
}

PATCH_ARM void cfw_theme_search_key_color(void *context, const unsigned char *color,
                                          const void *view)
{
    unsigned char mapped[4];
    osos_graphics_set_color(context, bar_label_color(color, view, SEARCH_KEY_LETTER,
                                                     SEARCH_KEY_WELL_LETTER, WELL_BLACK,
                                                     mapped));
}

PATCH_ARM void cfw_theme_option_bar_color(void *context, const unsigned char *color,
                                          const void *view)
{
    unsigned char mapped[4];
    const unsigned char *bytes = view;
    if (bytes[OPTION_BAR_STYLE] < OPTION_BAR_THEMED_STYLES)
        color = bar_label_color(color, bytes, OPTION_BAR_LETTER, OPTION_BAR_WELL_LETTER,
                                bytes[OPTION_BAR_STYLE], mapped);
    osos_graphics_set_color(context, color);
}

/* Mask bitmaps (formats 4 and 8) take the view's foreground; tinted ones take
 * the icon tint, or the on-accent role while highlighted. Other bitmap views
 * never reach this call. */
PATCH_ARM void cfw_theme_bitmap_color(void *context, const unsigned char *color,
                                      const void *view)
{
    const unsigned char *tinted = cfw_theme_highlight_tint(view);
    osos_graphics_set_color(context, tinted ? tinted : color);
}
