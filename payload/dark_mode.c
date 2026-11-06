/* SPDX-License-Identifier: GPL-3.0-only */

#include "dark_mode.h"
#include "theme_settings.h"
#include "dark_mode_ui.h"
#include "osos.h"
#include "cfw.h"
#include "resources.h"
#include "theme_style.h"

enum { IMG_SUN = 0x0dad0bff }; /* Settings_Brightness_Image */

int cfw_dark_mode_enabled(void)
{
    return cfw_theme_appearance_get(0);
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
        const char *error = cfw_theme_settings_error();
        if (error) {
            *out = (uintptr_t)error;
            return 1;
        }
        *out = osos_ui_string(cfw_dark_mode_enabled() ? CFW_DarkMode_DarkLabel
                                                      : CFW_DarkMode_LightLabel);
        return 1;
    }
    return 0;
}

int cfw_dark_mode_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_DarkMode_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    cfw_theme_settings_toggle(controller);
    return 1;
}
