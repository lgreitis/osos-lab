/* SPDX-License-Identifier: GPL-3.0-only */

#include "volume_warning.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"
#include "preferences.h"
#include "volume_warning_ui.h"

enum {
    VOLUME_WARNING_OFF_RECORD = 0x304e5756, /* VWN0 */
    VOLUME_WARNING_ON_RECORD = 0x314e5756,  /* VWN1 */
};

/* Built on the stack; a static osos_path would keep its 258-byte buffer resident. */
static void settings_path(struct osos_path *path)
{
    static const char name[] = "iPod_Control\\Device\\CFWVolWarn";
    path->volume = 0;
    for (uint32_t i = 0; i < sizeof(name); i++)
        path->text[i] = name[i];
}

/* Apple's warning stays on unless the user turns it off. */
static int warning_enabled = 1;

void cfw_volume_warning_load(void)
{
    struct osos_path path;
    uint32_t record;
    settings_path(&path);
    warning_enabled = !cfw_preferences_read(&path, &record, sizeof(record)) ||
                      record != VOLUME_WARNING_OFF_RECORD;
}

/* Replaces the region test in Apple's warning acknowledgment check and in the
 * preferences loader's startup cap. A zero result reports the warning as
 * acknowledged, so every warning path skips the 168 stop, and keeps the saved volume
 * at startup. */
PATCH_ARM int cfw_volume_warning_active(void)
{
    typedef int (*fn)(void);
    return warning_enabled && ((fn)OSOS_VOLUME_POLICY_REGION)();
}

int cfw_volume_warning_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_VolumeWarning_Value)
        return 0;
    *out = osos_settings_on_off(warning_enabled);
    return 1;
}

int cfw_volume_warning_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_VolumeWarning_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    int next = !warning_enabled;
    uint32_t record = next ? VOLUME_WARNING_ON_RECORD : VOLUME_WARNING_OFF_RECORD;
    struct osos_path path;
    settings_path(&path);
    if (cfw_preferences_write(&path, &record, sizeof(record))) {
        warning_enabled = next;
        osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                          CFW_VolumeWarning_Value);
    }
    return 1;
}
