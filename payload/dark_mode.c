/* SPDX-License-Identifier: GPL-3.0-only */

#include "dark_mode.h"
#include "dark_mode_ui.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "patch.h"

enum {
    DARK_MODE_OFF_RECORD = 0x304b5244, /* DRK0 */
    DARK_MODE_ON_RECORD = 0x314b5244,  /* DRK1 */
    BACKGROUND_LIGHT_MIN = 0xb0,
    BACKGROUND_DARK_BASE = 0x18,
    COVER_FLOW_BORDER_DARK = 0x38,
    TEXT_DARK_MAX = 0x80,
    TEXT_LIGHT_BASE = 0xe8,
};

static unsigned int enabled;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWDark"};

int cfw_dark_mode_enabled(void)
{
    return enabled != 0;
}

void cfw_dark_mode_load(void)
{
    uint32_t record;
    enabled = cfw_preferences_read(&settings_path, &record, sizeof(record)) &&
              record == DARK_MODE_ON_RECORD;
}

static int save(unsigned int value)
{
    uint32_t record = value ? DARK_MODE_ON_RECORD : DARK_MODE_OFF_RECORD;
    return cfw_preferences_write(&settings_path, &record, sizeof(record));
}

int cfw_dark_mode_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_DarkMode_Value)
        return 0;
    *out = osos_settings_on_off(enabled);
    return 1;
}

int cfw_dark_mode_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_DarkMode_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    if (save(!enabled)) {
        enabled = !enabled;
        osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                          CFW_DarkMode_Value);
        void *now_playing = osos_now_playing_model();
        if (now_playing) {
            /* Match Apple's playback-state notifications to reload cached icons. */
            osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0c);
            osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0d);
        }
        osos_ui_invalidate();
    }
    return 1;
}

PATCH_ARM void cfw_dark_mode_fill(void *surface, const void *rect,
                                const unsigned char *color, uint32_t mode,
                                void *clip)
{
    unsigned char mapped[4];
    int shade = -1;
    if (enabled && color[0] == color[1] && color[1] == color[2] &&
        color[0] >= BACKGROUND_LIGHT_MIN)
        shade = BACKGROUND_DARK_BASE + (255 - color[0]) / 4;
    /* CoverFlow_Backside_Border_Color (COLR 0x0dad05d8). */
    else if (enabled && color[0] == 0x7e && color[1] == 0x81 && color[2] == 0x83)
        shade = COVER_FLOW_BORDER_DARK;
    if (shade >= 0) {
        mapped[0] = mapped[1] = mapped[2] = shade;
        mapped[3] = color[3];
        color = mapped;
    }
    osos_surface_fill_rect(surface, rect, color, mode, clip);
}

PATCH_ARM void cfw_dark_mode_clear_color(int red, int green, int blue, int alpha)
{
    if (enabled)
        red = green = blue = BACKGROUND_DARK_BASE * 0x10000 / 255;
    osos_gles_clear_color(red, green, blue, alpha);
}

PATCH_ARM uint32_t cfw_dark_mode_reflection_blend(uint32_t pixel, uint32_t background,
                                                int weight)
{
    if (enabled)
        background = ((BACKGROUND_DARK_BASE >> 3) << 11) |
                     ((BACKGROUND_DARK_BASE >> 2) << 5) | (BACKGROUND_DARK_BASE >> 3);
    return osos_rgb565_blend(pixel, background, weight);
}

PATCH_ARM void cfw_dark_mode_text_color(void *context, const unsigned char *color)
{
    unsigned char mapped[4];
    if (enabled && color[0] == color[1] && color[1] == color[2] &&
        color[0] <= TEXT_DARK_MAX) {
        mapped[0] = mapped[1] = mapped[2] = TEXT_LIGHT_BASE - color[0] / 2;
        mapped[3] = color[3];
        color = mapped;
    }
    osos_graphics_set_color(context, color);
}
