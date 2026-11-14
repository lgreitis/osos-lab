/* SPDX-License-Identifier: GPL-3.0-only */

#include "clicker.h"
#include "osos.h"
#include "cfw.h"
#include "patch.h"

enum {
    SETTINGS_PREFS_OFFSET = 0x378,
    CLICKER_PREF = 0x608c,
    CLICKER_BOTH = 0x608d,
    CLICKER_OFF = 0x608e,
    /* On Classic 7G / 2.0.5, these outputs are reversed from Apple's names. */
    CLICKER_SPEAKER = 0x6090,
    CLICKER_HEADPHONES = 0x608f,
    PREF_TYPE = 0x6000,
    CLICKER_ROW_VALUE = 0x8912,     /* blue value text on the Settings row */
    CLICKER_PREVIEW_TEXT = 0x8914,  /* description under the preview picture */
    CLICKER_PREVIEW_IMAGE = 0x8925, /* the preview picture (a 'BMap' property) */
    IMG_ON = 0x0dad0c03,            /* Settings_ClickerOn_Image */
    IMG_OFF = 0x0dad0c04,           /* Settings_ClickerOff_Image */
    STR_SPEAKER = 0x0dad0bd3,       /* Settings_Speaker_String */
    STR_HEADPHONES = 0x0dad0bd4,    /* Settings_Headphones_String */
    STR_BOTH = 0x0dad0bd5,          /* Settings_Both_String */
    STR_OFF = 0x0dad0bda,           /* Settings_Off_String */
    DESC_OFF = 0x0dad0bd6,          /* "Turn Off Sound While Scrolling" */
    DESC_SPEAKER = 0x0dad0bd7,      /* "Play Sound Through Speaker" */
    DESC_HEADPHONES = 0x0dad0bd8,   /* "Play Sound Through Headphones" */
    DESC_BOTH = 0x0dad0bd9,         /* "Play Sound Through Headphones and Speaker" */
};

static void *prefs(void *model)
{
    return *(void **)((unsigned char *)model + SETTINGS_PREFS_OFFSET);
}

static uint32_t clicker_get(void *model)
{
    void *preferences = prefs(model);
    return osos_preferences_get(preferences, CLICKER_PREF, PREF_TYPE);
}

static void clicker_set(void *model, uint32_t value)
{
    void *preferences = prefs(model);
    osos_preferences_set(preferences, CLICKER_PREF, PREF_TYPE, value);
}

PATCH_ARM int cfw_clicker_action(void *controller, const char *action,
                                 uint32_t argument)
{
    if (!cfw_string_equal(action, "ToggleSetting_Clicker"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    void *model = osos_settings_model(controller);
    uint32_t next;
    switch (clicker_get(model)) {
    case CLICKER_OFF:
        next = CLICKER_SPEAKER;
        break;
    case CLICKER_SPEAKER:
        next = CLICKER_HEADPHONES;
        break;
    case CLICKER_HEADPHONES:
        next = CLICKER_BOTH;
        break;
    default:
        next = CLICKER_OFF;
        break; /* Both (Classic "On") */
    }
    clicker_set(model, next);
    osos_model_notify(model, OSOS_NOTIFY_ALL, CLICKER_ROW_VALUE);
    osos_model_notify(model, OSOS_NOTIFY_ALL, CLICKER_PREVIEW_TEXT);
    osos_model_notify(model, OSOS_NOTIFY_ALL, CLICKER_PREVIEW_IMAGE);
    return 1;
}

PATCH_ARM int cfw_clicker_property(void *model, uint32_t type, uint32_t property,
                                   uintptr_t *out)
{
    /* The native two-state preview getter uses resource 0 for the extra modes,
     * causing an allocator abort. Handle the bitmap before native dispatch. */
    if (property == CLICKER_PREVIEW_IMAGE && type == OSOS_RESOURCE_BMAP) {
        *out = osos_ui_bitmap(clicker_get(model) == CLICKER_OFF ? IMG_OFF : IMG_ON);
        return 1;
    }
    if (type != OSOS_PROPERTY_STRING ||
        (property != CLICKER_ROW_VALUE && property != CLICKER_PREVIEW_TEXT))
        return 0;
    int row = property == CLICKER_ROW_VALUE;
    uint32_t id;
    switch (clicker_get(model)) {
    case CLICKER_OFF:
        id = row ? STR_OFF : DESC_OFF;
        break;
    case CLICKER_SPEAKER:
        id = row ? STR_SPEAKER : DESC_SPEAKER;
        break;
    case CLICKER_HEADPHONES:
        id = row ? STR_HEADPHONES : DESC_HEADPHONES;
        break;
    default:
        id = row ? STR_BOTH : DESC_BOTH;
        break;
    }
    *out = osos_ui_string(id);
    return 1;
}
