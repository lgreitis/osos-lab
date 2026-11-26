/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_visibility.h"
#include "dark_mode.h"
#include "osos.h"
#include "patch.h"
#include "resources.h"
#include "rvx/runtime.h"
#include "store.h"

static void *native_preferences;

/* Runs after the native Preferences load; the disk is spinning at boot, so a
 * migrated store is written now instead of waiting for sleep. */
void cfw_settings_boot(void *preferences)
{
    native_preferences = preferences;
    cfw_store_load();
    cfw_dark_mode_load();
    cfw_rvx_boot();
    cfw_store_flush();
}

/* Apple writes its settings only around USB disk mode; both stores are also
 * written before every light sleep, while the disk can still spin. */
void cfw_settings_sleep(void)
{
    cfw_rvx_flush();
    if (native_preferences)
        osos_preferences_flush(native_preferences);
    cfw_store_flush();
}

/* Legacy import: removes the pre-store files once the store holds their settings. */
void cfw_store_delete_legacy(void)
{
    cfw_dark_mode_delete_legacy();
}

PATCH_ARM void cfw_settings_reset(void *model)
{
    osos_reset_all_settings(model);
    cfw_store_clear();
    cfw_rvx_reset();
    cfw_dark_mode_reset();
}

PATCH_ARM int cfw_settings_action(void *controller, const char *action,
                                  uint32_t argument)
{
    if (cfw_dark_mode_action(controller, action, argument))
        return 1;

    return osos_settings_action(controller, action, argument);
}

PATCH_ARM int cfw_settings_property(void *model, uint32_t type, uint32_t property,
                                    uintptr_t *out)
{
    if (cfw_rvx_property(type, property, out) ||
        cfw_dark_mode_property(type, property, out))
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
    if (cfw_rvx_indexed_property(type, property, index, out))
        return 1;

    return cfw_ui_visibility_property(model, type, property, index, out);
}
