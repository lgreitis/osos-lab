/* SPDX-License-Identifier: GPL-3.0-only */

#include "dark_mode.h"
#include "assets.h"
#include "dark_mode_ui.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "store.h"
#include "patch.h"
#include "resources.h"
#include "theme_style.h"
#include "theme_input.h"

enum {
    DARK_MODE_OFF_RECORD = 0x304b5244, /* DRK0 */
    DARK_MODE_ON_RECORD = 0x314b5244,  /* DRK1 */
    /* Native colors are Classic light; these identify what each one draws. */
    BACKGROUND_LIGHT_MIN = 0xb0,
    TEXT_DARK_MAX = 0x80,
    TEXT_LIGHT_REFERENCE =
        0x20, /* Classic light text role; native black maps below it. */
    COVER_FLOW_BORDER_LIGHT =
        0x7e8183, /* CoverFlow_Backside_Border_Color (0x0dad05d8) */
    ACCENT_FILL_LIGHT = 0x236cc5,
    ACCENT_TEXT_LIGHT = 0x3185d0,
    IMG_SUN = 0x0dad0bff, /* Settings_Brightness_Image */
};

int cfw_dark_mode_enabled(void)
{
    return cfw_theme_appearance_get(cfw_assets_enabled());
}

/* Legacy import: reads the pre-store CFWDark file. */
static uint32_t load_legacy(void)
{
    uint32_t record;
    if (!cfw_preferences_read(CFW_DARK_MODE_LEGACY_FILE, &record, sizeof(record)) ||
        (record != DARK_MODE_ON_RECORD && record != DARK_MODE_OFF_RECORD))
        return 0;
    cfw_store_set(CFW_STORE_DARK, &record, sizeof(record));
    return record;
}

void cfw_dark_mode_load(void)
{
    uint32_t record = 0;
    if (cfw_store_get(CFW_STORE_DARK, &record, sizeof(record)) != sizeof(record))
        record = cfw_store_migrating() ? load_legacy() : 0;
    int next = record == DARK_MODE_ON_RECORD;
    if (next != cfw_assets_enabled() && cfw_assets_prepare())
        cfw_assets_set_enabled(next);
}

static void save(unsigned int value)
{
    uint32_t record = value ? DARK_MODE_ON_RECORD : DARK_MODE_OFF_RECORD;
    cfw_store_set(CFW_STORE_DARK, &record, sizeof(record));
}

int cfw_dark_mode_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type == OSOS_RESOURCE_BMAP && property == CFW_DarkMode_PreviewImage) {
        *out = cfw_dark_mode_enabled()
                   ? (uintptr_t)cfw_resource_bitmap(CFW_DarkMode_Moon)
                   : osos_ui_bitmap(IMG_SUN);
        return 1;
    }
    if (type != OSOS_PROPERTY_STRING)
        return 0;
    if (property == CFW_DarkMode_Value) {
        *out = osos_settings_on_off(cfw_dark_mode_enabled());
        return 1;
    }
    if (property == CFW_DarkMode_PreviewText) {
        *out = osos_ui_string(cfw_dark_mode_enabled() ? CFW_DarkMode_DarkLabel
                                                      : CFW_DarkMode_LightLabel);
        return 1;
    }
    return 0;
}

/* settings is null when the caller has no Settings model to notify. */
static void apply(void *settings, int next)
{
    if (!cfw_assets_prepare())
        return;
    save(next);
    cfw_assets_set_enabled(next);
    if (settings) {
        osos_model_notify(settings, OSOS_NOTIFY_ALL, CFW_DarkMode_Value);
        osos_model_notify(settings, OSOS_NOTIFY_ALL, CFW_DarkMode_PreviewImage);
        osos_model_notify(settings, OSOS_NOTIFY_ALL, CFW_DarkMode_PreviewText);
    }
    void *now_playing = osos_now_playing_model();
    if (now_playing) {
        /* Match Apple's playback-state notifications to reload cached icons. */
        osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0c);
        osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0d);
    }
    osos_ui_invalidate();
}

int cfw_dark_mode_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_DarkMode_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    if (!cfw_theme_input_begin())
        return 1;
    apply(osos_settings_model(controller), !cfw_dark_mode_enabled());
    cfw_theme_input_finish();
    return 1;
}

/* Saves Off even when the light assets cannot load, so the next boot is light. */
void cfw_dark_mode_reset(void)
{
    save(0);
    if (cfw_dark_mode_enabled())
        apply(0, 0);
}

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
PATCH_ARM void cfw_dark_mode_background_fill(void *context, const int32_t *rect,
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
    return cfw_dark_mode_enabled() || cfw_theme_style_custom() ||
           cfw_theme_accent_get();
}

/* Accent presets keep native background clears and blends at their stock values. */
static int background_themed(void)
{
    return cfw_dark_mode_enabled() || cfw_theme_style_custom();
}

static const unsigned char *role(enum cfw_theme_role value)
{
    return cfw_theme_color(value, cfw_dark_mode_enabled());
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

PATCH_ARM void cfw_dark_mode_fill(void *surface, const void *rect,
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

PATCH_ARM void cfw_dark_mode_clear_color(int red, int green, int blue, int alpha)
{
    if (background_themed()) {
        const unsigned char *background = role(CFW_THEME_BACKGROUND);
        red = background[0] * 0x10000 / 255;
        green = background[1] * 0x10000 / 255;
        blue = background[2] * 0x10000 / 255;
    }
    osos_gles_clear_color(red, green, blue, alpha);
}

PATCH_ARM uint32_t cfw_dark_mode_reflection_blend(uint32_t pixel, uint32_t background,
                                                  int weight)
{
    if (background_themed()) {
        const unsigned char *color = role(CFW_THEME_BACKGROUND);
        background = ((color[0] >> 3) << 11) | ((color[1] >> 2) << 5) | (color[2] >> 3);
    }
    return osos_rgb565_blend(pixel, background, weight);
}

PATCH_ARM void cfw_dark_mode_text_color(void *context, const unsigned char *color,
                                        const void *view)
{
    unsigned char mapped[4];
    if (themed() && !native_text_color(view)) {
        const unsigned char *text = role(CFW_THEME_TEXT);
        if (rgb(color) == ACCENT_FILL_LIGHT)
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
