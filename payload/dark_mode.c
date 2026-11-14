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

enum {
    DARK_MODE_OFF_RECORD = 0x304b5244, /* DRK0 */
    DARK_MODE_ON_RECORD = 0x314b5244,  /* DRK1 */
    BACKGROUND_LIGHT_MIN = 0xb0,
    BACKGROUND_DARK_BASE = 0x18,
    COVER_FLOW_BORDER_DARK = 0x38,
    TEXT_DARK_MAX = 0x80,
    TEXT_LIGHT_BASE = 0xe8,
    ACCENT_FILL_LIGHT = 0x236cc5,
    ACCENT_TEXT_LIGHT = 0x3185d0,
    ACCENT_DARK = 0x00a1ff,
    IMG_SUN = 0x0dad0bff, /* Settings_Brightness_Image */
};

int cfw_dark_mode_enabled(void)
{
    return cfw_assets_enabled();
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

    apply(osos_settings_model(controller), !cfw_dark_mode_enabled());
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

static int accent_color(const unsigned char *color, uint32_t light, unsigned char *out)
{
    uint32_t rgb = (color[0] << 16) | (color[1] << 8) | color[2];
    if (rgb != light)
        return 0;
    out[0] = (ACCENT_DARK >> 16) & 0xff;
    out[1] = (ACCENT_DARK >> 8) & 0xff;
    out[2] = ACCENT_DARK & 0xff;
    out[3] = color[3];
    return 1;
}

PATCH_ARM void cfw_dark_mode_fill(void *surface, const void *rect,
                                  const unsigned char *color, uint32_t mode, void *clip)
{
    unsigned char mapped[4];
    int enabled = cfw_dark_mode_enabled();
    int shade = -1;
    if (enabled && color[0] == color[1] && color[1] == color[2] &&
        color[0] >= BACKGROUND_LIGHT_MIN)
        shade = BACKGROUND_DARK_BASE + (255 - color[0]) / 4;
    /* CoverFlow_Backside_Border_Color (COLR 0x0dad05d8). */
    else if (enabled && color[0] == 0x7e && color[1] == 0x81 && color[2] == 0x83)
        shade = COVER_FLOW_BORDER_DARK;
    else if (enabled && accent_color(color, ACCENT_FILL_LIGHT, mapped))
        color = mapped;
    if (shade >= 0) {
        mapped[0] = mapped[1] = mapped[2] = shade;
        mapped[3] = color[3];
        color = mapped;
    }
    osos_surface_fill_rect(surface, rect, color, mode, clip);
}

PATCH_ARM void cfw_dark_mode_clear_color(int red, int green, int blue, int alpha)
{
    if (cfw_dark_mode_enabled())
        red = green = blue = BACKGROUND_DARK_BASE * 0x10000 / 255;
    osos_gles_clear_color(red, green, blue, alpha);
}

PATCH_ARM uint32_t cfw_dark_mode_reflection_blend(uint32_t pixel, uint32_t background,
                                                  int weight)
{
    if (cfw_dark_mode_enabled())
        background = ((BACKGROUND_DARK_BASE >> 3) << 11) |
                     ((BACKGROUND_DARK_BASE >> 2) << 5) | (BACKGROUND_DARK_BASE >> 3);
    return osos_rgb565_blend(pixel, background, weight);
}

PATCH_ARM void cfw_dark_mode_text_color(void *context, const unsigned char *color,
                                        const void *view)
{
    unsigned char mapped[4];
    int enabled = cfw_dark_mode_enabled() && !native_text_color(view);
    if (enabled && (accent_color(color, ACCENT_FILL_LIGHT, mapped) ||
                    accent_color(color, ACCENT_TEXT_LIGHT, mapped))) {
        color = mapped;
    } else if (enabled && color[0] == color[1] && color[1] == color[2] &&
               color[0] <= TEXT_DARK_MAX) {
        mapped[0] = mapped[1] = mapped[2] = TEXT_LIGHT_BASE - color[0] / 2;
        mapped[3] = color[3];
        color = mapped;
    }
    osos_graphics_set_color(context, color);
}
