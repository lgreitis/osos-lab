/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_settings.h"
#include "theme_classic.h"
#include "theme_input.h"
#include "theme_settings_ui.h"
#include "accent_settings_ui.h"
#include "dark_mode_ui.h"
#include "theme_boot.h"
#include "theme_discovery.h"
#include "theme_preferences.h"
#include "cfw.h"

/* Metadata stays stable while the picker is open; packages are revalidated on
 * Select. The list is allocated when the picker opens and released when the
 * Settings main menu is destroyed. */
#define THEME_CHOICES 64
static struct cfw_theme_choice *choices;
static uint32_t count;
static int initialized, overflow, classic_available;
static const char *error, *dark_error, *accent_error;
/* Display order of the Accent Color list; stored values keep enum order. */
static const unsigned char accent_order[CFW_THEME_ACCENT_COUNT] = {
    CFW_THEME_ACCENT_DEFAULT, CFW_THEME_ACCENT_BLUE, CFW_THEME_ACCENT_TEAL,
    CFW_THEME_ACCENT_GREEN, CFW_THEME_ACCENT_YELLOW, CFW_THEME_ACCENT_ORANGE,
    CFW_THEME_ACCENT_RED, CFW_THEME_ACCENT_PINK, CFW_THEME_ACCENT_PURPLE,
    CFW_THEME_ACCENT_GRAPHITE,
};
static char active_name[48] = "Classic";
static struct cfw_theme_switch switcher;

static enum cfw_theme_store_result store(void *context,
                                         const struct cfw_theme_preferences *state)
{
    (void)context;
    return cfw_theme_preferences_store(state);
}
static const struct cfw_theme_switch_ops ops = {store};

void cfw_theme_settings_load(void)
{
    if (initialized)
        return;
    initialized = 1;
    cfw_theme_switch_init(&switcher, &ops, 0);
    cfw_theme_appearance_set(0);
    struct cfw_theme_pack dark_pack;
    classic_available = cfw_theme_baseline_valid(&cfw_theme_classic_baseline) &&
                        cfw_theme_pack_read(&cfw_theme_classic_dark, 1, 1, &dark_pack);
    if (classic_available)
        cfw_theme_pack_release(&dark_pack);
    else {
        osos_menu_item_set_visible(CFW_Theme_Item, 0);
        osos_menu_item_set_visible(CFW_Accent_Item, 0);
        osos_menu_item_set_visible(CFW_DarkMode_Item, 0);
        return;
    }
    struct cfw_theme_boot_request request;
    int recover = cfw_theme_boot_resolve(&request);
    if (recover) {
        cfw_theme_switch_store_classic(&switcher);
    } else if (request.pack.text[0] || request.selection.accent) {
        const struct osos_path *boot_path = request.pack.text[0] ? &request.pack : 0;
        int ok =
            request.migrate
                ? cfw_theme_switch_apply(&switcher, &request.selection, boot_path,
                                         &cfw_theme_classic_baseline)
                : cfw_theme_switch_restore(&switcher, &request.selection, boot_path,
                                           &cfw_theme_classic_baseline);
        if (ok && request.selection.id[0]) {
            for (unsigned i = 0; i < sizeof(active_name); i++)
                active_name[i] = request.name[i];
        } else if (!ok && switcher.result == CFW_THEME_LOAD_FAILED) {
            cfw_theme_switch_store_classic(&switcher);
        }
    }
    osos_menu_item_set_visible(CFW_DarkMode_Item,
                               !switcher.active.custom || request.appearance_mask == 3);
}

void cfw_theme_settings_refresh(void)
{
    cfw_theme_settings_load();
    if (!classic_available)
        return;
    int any;
    cfw_theme_discovery_scan(0, 0, &any);
    osos_menu_item_set_visible(CFW_Theme_Item, any);
}

static void open_list(void)
{
    if (!choices)
        choices = osos_import_alloc(THEME_CHOICES * sizeof(*choices));
    if (!choices) {
        count = overflow = 0;
        error = "Not enough memory";
        return;
    }
    count = cfw_theme_discovery_scan(choices, THEME_CHOICES, &overflow);
    error = 0;
}

void cfw_theme_settings_close(void)
{
    if (choices)
        osos_import_free(choices);
    choices = 0;
    count = overflow = 0;
}

const char *cfw_theme_settings_error(void)
{
    return dark_error;
}

static void notify(void *controller, int changed)
{
    void *model = osos_settings_model(controller);
    const uint32_t properties[] = {CFW_Accent_Choices,        CFW_Accent_PreviewText,
                                   CFW_Accent_PreviewImage,   CFW_Theme_Choices,
                                   CFW_Theme_PreviewText,     CFW_Theme_PreviewImage,
                                   CFW_DarkMode_Value,        CFW_DarkMode_PreviewImage,
                                   CFW_DarkMode_PreviewText};
    for (unsigned i = 0; i < sizeof(properties) / sizeof(*properties); i++)
        osos_model_notify(model, OSOS_NOTIFY_ALL, properties[i]);
    if (changed) {
        void *now_playing = osos_now_playing_model();
        if (now_playing) {
            osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0c);
            osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0d);
        }
    }
    osos_ui_invalidate();
}

static const char *switch_error(void)
{
    return switcher.result == CFW_THEME_OK             ? 0
           : switcher.result == CFW_THEME_STORE_FAILED ? "Could not save theme"
                                                       : "Could not load theme";
}

static void apply(void *controller, const char *id, uint32_t dark, unsigned accent, int fallback)
{
    if (!classic_available || !cfw_theme_input_available())
        return;
    struct cfw_theme_selection selection;
    for (unsigned i = 0; i < sizeof(selection); i++)
        ((unsigned char *)&selection)[i] = 0;
    selection.dark = dark;
    selection.accent = accent;
    struct osos_path pack;
    struct cfw_theme_metadata metadata;
    const struct osos_path *path = 0;
    if (id[0]) {
        if (!cfw_theme_discovery_select(id, dark, fallback, &metadata, &pack,
                                        &selection.dark)) {
            cfw_theme_input_begin();
            cfw_theme_input_finish();
            error = fallback ? "Theme unavailable" : "Appearance unavailable";
            dark_error = fallback ? 0 : error;
            notify(controller, 0);
            return;
        }
        for (unsigned i = 0; i < sizeof(selection.id); i++)
            selection.id[i] = metadata.id[i];
        path = &pack;
    } else if (dark) {
        path = &cfw_theme_classic_dark;
    }
    int changed = cfw_theme_switch_apply(&switcher, &selection, path,
                                         &cfw_theme_classic_baseline);
    if (switcher.result == CFW_THEME_BUSY)
        return;
    error = switch_error();
    dark_error = fallback ? 0 : error;
    if (changed) {
        osos_menu_item_set_visible(CFW_DarkMode_Item,
                                   !selection.id[0] || metadata.appearance_mask == 3);
        const char *name = id[0] ? metadata.name : "Classic";
        unsigned i = 0;
        do {
            active_name[i] = name[i];
        } while (name[i++]);
    }
    notify(controller, changed);
}

void cfw_theme_settings_toggle(void *controller)
{
    cfw_theme_settings_load();
    apply(controller, switcher.active.selection.id, !switcher.active.selection.dark,
          switcher.active.selection.accent, 0);
}

int cfw_theme_settings_action(void *controller, const char *action, uint32_t argument)
{
    int accent_open = cfw_string_equal(action, "CFW_Accent_Open");
    int accent_select = cfw_string_equal(action, "CFW_Accent_Select");
    if (accent_open || accent_select) {
        if (argument == OSOS_ACTION_SUPPORT_QUERY)
            return 1;
        cfw_theme_settings_load();
        if (!classic_available)
            return 1;
        if (accent_open) {
            accent_error = 0;
            return 1;
        }
        unsigned row = osos_selection_index(osos_ui_selection());
        if (row >= CFW_THEME_ACCENT_COUNT || !cfw_theme_input_available())
            return 1;
        int changed = cfw_theme_switch_set_accent(&switcher, accent_order[row]);
        if (switcher.result != CFW_THEME_BUSY) {
            accent_error = switch_error();
            notify(controller, changed);
        }
        return 1;
    }
    int open = cfw_string_equal(action, "CFW_Theme_Open");
    if (!open && !cfw_string_equal(action, "CFW_Theme_Select"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    cfw_theme_settings_load();
    if (!classic_available)
        return 1;
    if (open) {
        open_list();
        /* Retain the active theme when the sorted list reaches its limit. */
        if (overflow && switcher.active.custom && !cfw_theme_settings_selected()) {
            struct osos_path pack;
            uint32_t dark;
            struct cfw_theme_metadata metadata;
            if (cfw_theme_discovery_select(switcher.active.selection.id,
                                           switcher.active.selection.dark, 1, &metadata,
                                           &pack, &dark)) {
                for (unsigned i = 0; i < sizeof(metadata.id); i++)
                    choices[count - 1].id[i] = metadata.id[i];
                for (unsigned i = 0; i < sizeof(metadata.name); i++)
                    choices[count - 1].name[i] = metadata.name[i];
            }
        }
        dark_error = 0;
    } else {
        uint32_t index = osos_selection_index(osos_ui_selection());
        if (index <= count)
            apply(controller, index ? choices[index - 1].id : "",
                  switcher.active.selection.dark, switcher.active.selection.accent, 1);
    }
    return 1;
}

int cfw_theme_settings_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (property == CFW_Accent_Choices && type == OSOS_PROPERTY_COUNT) {
        *out = CFW_THEME_ACCENT_COUNT;
        return 1;
    }
    if (property == CFW_Accent_PreviewText && type == OSOS_PROPERTY_STRING) {
        *out = (uintptr_t)(accent_error ? accent_error :
                          cfw_theme_accent_label(switcher.active.selection.accent));
        return 1;
    }
    if (property == CFW_Theme_Choices && type == OSOS_PROPERTY_COUNT) {
        *out = count + 1 + (error != 0 || overflow);
        return 1;
    }
    if (property == CFW_Theme_PreviewText && type == OSOS_PROPERTY_STRING) {
        *out = (uintptr_t)(error ? error : active_name);
        return 1;
    }
    return 0;
}

int cfw_theme_settings_indexed_property(uint32_t type, uint32_t property,
                                        uint32_t index, uintptr_t *out)
{
    if (property == CFW_Accent_Choices) {
        if (index >= CFW_THEME_ACCENT_COUNT)
            return 0;
        if (type == OSOS_PROPERTY_STRING)
            *out = (uintptr_t)cfw_theme_accent_label(accent_order[index]);
        else if (type == OSOS_RESOURCE_BMAP)
            *out = 0;
        else
            return 0;
        return 1;
    }
    if (property != CFW_Theme_Choices)
        return index == 0 ? cfw_theme_settings_property(type, property, out) : 0;
    if (index > count) {
        if (index != count + 1 || (!error && !overflow))
            return 0;
        if (type == OSOS_PROPERTY_STRING)
            *out = (uintptr_t)(error ? error : "Showing 64 themes");
        else if (type == OSOS_RESOURCE_BMAP)
            *out = 0;
        else
            return 0;
        return 1;
    }
    if (type == OSOS_PROPERTY_STRING) {
        *out = (uintptr_t)(index ? choices[index - 1].name : "Classic");
    } else if (type == OSOS_RESOURCE_BMAP) {
        *out = 0;
    } else {
        return 0;
    }
    return 1;
}

uint32_t cfw_theme_settings_selected(void)
{
    for (unsigned i = 0; i < count; i++)
        if (cfw_string_equal(choices[i].id, switcher.active.selection.id))
            return i + 1;
    return 0;
}

uint32_t cfw_theme_settings_accent_selected(void)
{
    for (unsigned i = 0; i < CFW_THEME_ACCENT_COUNT; i++)
        if (accent_order[i] == switcher.active.selection.accent)
            return i;
    return 0;
}
