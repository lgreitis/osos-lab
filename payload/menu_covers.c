/* SPDX-License-Identifier: GPL-3.0-only */

#include "menu_covers.h"
#include "menu_covers_ui.h"
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "preferences.h"
#include "theme_style.h"

enum {
    COVERS_OFF_RECORD = 0x30525643, /* CVR0 */
    COVERS_ON_RECORD = 0x31525643,  /* CVR1 */
};

static const char settings_file[] = "iPod_Control\\Device\\CFWCovers";

/* The user's choice, once made, outlives theme switches; until then the active
 * theme's rendering flag decides, and Classic shows covers. */
enum { PREFERENCE_THEME, PREFERENCE_ON, PREFERENCE_OFF };
static int preference;

int cfw_menu_covers_enabled(void)
{
    if (preference == PREFERENCE_THEME)
        return !(cfw_theme_render_flags() & CFW_THEME_MENU_COVERS_OFF);
    return preference == PREFERENCE_ON;
}

void cfw_menu_covers_load(void)
{
    uint32_t record;
    preference = PREFERENCE_THEME;
    if (cfw_preferences_read(settings_file, &record, sizeof(record))) {
        if (record == COVERS_ON_RECORD)
            preference = PREFERENCE_ON;
        else if (record == COVERS_OFF_RECORD)
            preference = PREFERENCE_OFF;
    }
}

static int save(int enabled)
{
    uint32_t record = enabled ? COVERS_ON_RECORD : COVERS_OFF_RECORD;
    return cfw_preferences_write(settings_file, &record, sizeof(record));
}

/* The main and Music menu highlight handlers ask the media artwork provider
 * whether the highlighted row has covers to show; a zero answer selects the
 * row's stock no-artwork preview layout instead of the cover pane. */
PATCH_ARM int cfw_menu_covers_query(void *provider, uint32_t flags)
{
    if (!cfw_menu_covers_enabled())
        return 0;
    return cfw_menu_covers_query_original(provider, flags);
}

int cfw_menu_covers_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING)
        return 0;
    if (property == CFW_MenuCovers_Value) {
        *out = osos_settings_on_off(cfw_menu_covers_enabled());
        return 1;
    }
    if (property == CFW_MenuCovers_PreviewText) {
        *out = osos_ui_string(cfw_menu_covers_enabled() ? CFW_MenuCovers_OnLabel
                                                        : CFW_MenuCovers_OffLabel);
        return 1;
    }
    return 0;
}

int cfw_menu_covers_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_MenuCovers_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    int next = !cfw_menu_covers_enabled();
    if (save(next)) {
        preference = next ? PREFERENCE_ON : PREFERENCE_OFF;
        void *settings = osos_settings_model(controller);
        osos_model_notify(settings, OSOS_NOTIFY_ALL, CFW_MenuCovers_Value);
        osos_model_notify(settings, OSOS_NOTIFY_ALL, CFW_MenuCovers_PreviewText);
        osos_ui_invalidate();
    }
    return 1;
}
