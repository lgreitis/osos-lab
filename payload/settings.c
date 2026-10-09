/* SPDX-License-Identifier: GPL-3.0-only */
#include "album_artists_menu.h"
#include "custom_eq.h"
#include "dark_mode.h"
#include "osos.h"
#include "patch.h"

PATCH_ARM int cfw_settings_action(void *controller, const char *action,
                                  uint32_t argument)
{
    if (cfw_dark_mode_action(controller, action, argument) ||
        cfw_eq_action(action, argument))
        return 1;
    return osos_settings_action(controller, action, argument);
}

PATCH_ARM int cfw_settings_property(void *model, uint32_t type,
                                    uint32_t property, uintptr_t *out)
{
    if (cfw_dark_mode_property(type, property, out))
        return 1;
    int result = osos_settings_property(model, type, property, out);
    if (result)
        cfw_album_artists_menu_adjust_property(type, property, out);
    return result;
}
