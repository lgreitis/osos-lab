/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_visibility.h"
#include "clicker.h"
#include "custom_eq.h"
#include "dark_mode.h"
#include "osos.h"
#include "patch.h"
#include "resources.h"

PATCH_ARM int cfw_settings_action(void *controller, const char *action,
                                  uint32_t argument)
{
    if (cfw_ui_handle_action(action, argument) ||
        cfw_clicker_action(controller, action, argument) ||
        cfw_dark_mode_action(controller, action, argument) ||
        cfw_eq_action(action, argument))
        return 1;

    return osos_settings_action(controller, action, argument);
}

PATCH_ARM int cfw_settings_property(void *model, uint32_t type, uint32_t property,
                                    uintptr_t *out)
{
    if (cfw_ui_property(type, property, out) ||
        cfw_dark_mode_property(type, property, out) ||
        cfw_clicker_property(model, type, property, out))
        return 1;

    if (type == OSOS_RESOURCE_BMAP) {
        const void *bitmap = cfw_resource_bitmap(property);
        if (bitmap) {
            *out = (uintptr_t)bitmap;
            return 1;
        }
    }

    int result = osos_settings_property(model, type, property, out);
    if (result)
        cfw_ui_visibility_adjust_property(type, property, out);

    return result;
}

PATCH_ARM int cfw_settings_indexed_property(void *model, uint32_t type,
                                            uint32_t property, uint32_t index,
                                            uintptr_t *out)
{
    if (cfw_ui_indexed_property(type, property, index, out))
        return 1;

    return cfw_ui_visibility_property(model, type, property, index, out);
}
