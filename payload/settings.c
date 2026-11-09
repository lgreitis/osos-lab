/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_visibility.h"
#include "clicker.h"
#include "custom_eq.h"
#include "dark_mode.h"
#include "menu_covers.h"
#include "theme_settings.h"
#include "osos.h"
#include "patch.h"
#include "preferences.h"
#include "resources.h"
#include "store.h"

static void *native_preferences;

/* Runs after the native Preferences load; the disk is spinning at boot, so a
 * migrated store is written now instead of waiting for sleep. */
void cfw_settings_boot(void *preferences)
{
    native_preferences = preferences;
    cfw_store_load();
    cfw_eq_load_preferences();
    cfw_theme_settings_load();
    cfw_menu_covers_load();
    cfw_ui_visibility_load_preferences();
    cfw_store_flush();
}

/* Apple writes its settings only around USB disk mode; both stores are also
 * written before every light sleep, while the disk can still spin. */
void cfw_settings_sleep(void)
{
    if (native_preferences)
        osos_preferences_flush(native_preferences);
    cfw_store_flush();
}

/* Legacy import: removes the pre-store files once the store holds their settings. */
void cfw_store_delete_legacy(void)
{
    cfw_preferences_delete(CFW_EQ_LEGACY_FILE);
    cfw_preferences_delete(CFW_DARK_MODE_LEGACY_FILE);
    cfw_ui_visibility_delete_legacy();
}

/* Menu visibility resets through the native menu-reset hooks. */
PATCH_ARM void cfw_settings_reset(void *model)
{
    osos_reset_all_settings(model);
    cfw_eq_reset_preferences();
    cfw_theme_settings_reset();
    cfw_menu_covers_reset();
}

PATCH_ARM int cfw_settings_action(void *controller, const char *action,
                                  uint32_t argument)
{
    if (cfw_theme_settings_action(controller, action, argument) ||
        cfw_ui_handle_action(action, argument) ||
        cfw_clicker_action(controller, action, argument) ||
        cfw_dark_mode_action(controller, action, argument) ||
        cfw_menu_covers_action(controller, action, argument) ||
        cfw_eq_action(action, argument))
        return 1;

    return osos_settings_action(controller, action, argument);
}

PATCH_ARM int cfw_settings_property(void *model, uint32_t type, uint32_t property,
                                    uintptr_t *out)
{
    if (cfw_theme_settings_property(type, property, out) ||
        cfw_ui_property(type, property, out) ||
        cfw_dark_mode_property(type, property, out) ||
        cfw_menu_covers_property(type, property, out) ||
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
    if (cfw_theme_settings_indexed_property(type, property, index, out) ||
        cfw_ui_indexed_property(type, property, index, out))
        return 1;

    return cfw_ui_visibility_property(model, type, property, index, out);
}
