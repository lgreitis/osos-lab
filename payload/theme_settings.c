/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_settings.h"
#include "theme_classic.h"
#include "theme_input.h"
#include "theme_settings_ui.h"
#include "accent_settings_ui.h"
#include "dark_mode_ui.h"
#include "menu_covers_ui.h"
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
/* Whether /themes holds a valid custom package. Probed at boot and after each
 * library load, and updated by picker scans. Disk mode marks it stale; if no
 * library load follows, the next Settings open probes. */
static int custom_available, custom_stale = 1;
static const char *error, *dark_error, *accent_error;
/* Display order of the Accent Color list; stored values keep enum order. */
static const unsigned char accent_order[CFW_THEME_ACCENT_COUNT] = {
    CFW_THEME_ACCENT_DEFAULT, CFW_THEME_ACCENT_BLUE, CFW_THEME_ACCENT_TEAL,
    CFW_THEME_ACCENT_GREEN, CFW_THEME_ACCENT_YELLOW, CFW_THEME_ACCENT_ORANGE,
    CFW_THEME_ACCENT_RED, CFW_THEME_ACCENT_PINK, CFW_THEME_ACCENT_PURPLE,
    CFW_THEME_ACCENT_GRAPHITE,
};
static char active_name[48];
static struct cfw_theme_switch switcher;

static const struct cfw_theme_switch_ops ops = {cfw_theme_preferences_store};

static void set_active_name(const char *name)
{
    unsigned i = 0;
    for (; i + 1 < sizeof(active_name) && name[i]; i++)
        active_name[i] = name[i];
    active_name[i] = 0;
}

/* Without a usable baseline no theme can be restored, so the settings stay
 * hidden for the session. */
static void disable(void)
{
    classic_available = 0;
    osos_menu_item_set_visible(CFW_Theme_Item, 0);
    osos_menu_item_set_visible(CFW_Accent_Item, 0);
    osos_menu_item_set_visible(CFW_DarkMode_Item, 0);
}

/* The scan stops at the first valid package. */
static void probe(void)
{
    int any;
    cfw_theme_discovery_scan(0, 0, &any);
    custom_available = any;
    custom_stale = 0;
}

void cfw_theme_settings_load(void)
{
    if (initialized)
        return;
    initialized = 1;
    cfw_theme_switch_init(&switcher, &ops);
    cfw_theme_appearance_set(0);
    set_active_name("Classic");
    /* Paths live on the caller's stack; the request's pack path doubles as
     * scratch until the preferences resolve. */
    struct osos_path baseline_path;
    cfw_theme_path(&baseline_path, CFW_THEME_CLASSIC_BASELINE);
    struct cfw_theme_boot_request request;
    cfw_theme_path(&request.pack, CFW_THEME_CLASSIC_DARK);
    classic_available = cfw_theme_baseline_valid(&baseline_path) &&
                        cfw_theme_pack_valid(&request.pack, 1);
    if (!classic_available) {
        disable();
        return;
    }
    int recover = cfw_theme_boot_resolve(&request);
    if (recover) {
        cfw_theme_switch_store_classic(&switcher);
    } else if (request.pack.text[0] || request.selection.accent) {
        const struct osos_path *boot_path = request.pack.text[0] ? &request.pack : 0;
        int ok = cfw_theme_switch_restore(&switcher, &request.selection, boot_path,
                                          &baseline_path);
        if (ok && request.selection.id[0])
            set_active_name(request.name);
        else if (!ok && switcher.result == CFW_THEME_LOAD_FAILED)
            cfw_theme_switch_store_classic(&switcher);
        if (switcher.result == CFW_THEME_RESTORE_FAILED) {
            disable();
            return;
        }
    }
    osos_menu_item_set_visible(CFW_DarkMode_Item,
                               !switcher.active.custom || request.appearance_mask == 3);
    if (switcher.active.custom) {
        custom_available = 1;
        custom_stale = 0;
    } else if (custom_stale) {
        probe();
    }
}

void cfw_theme_settings_refresh(void)
{
    cfw_theme_settings_load();
    if (!classic_available)
        return;
    if (custom_stale)
        probe();
    osos_menu_item_set_visible(CFW_Theme_Item, custom_available);
}

void cfw_theme_settings_invalidate(void)
{
    custom_stale = 1;
}

void cfw_theme_settings_probe_stale(void)
{
    if (custom_stale && classic_available)
        probe();
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
    custom_available = count || overflow;
    custom_stale = 0;
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

/* controller is null when there is no Settings model to notify. */
static void notify(void *controller, int changed)
{
    const uint32_t properties[] = {
        CFW_Accent_Choices,       CFW_Accent_PreviewText,   CFW_Theme_Choices,
        CFW_Theme_PreviewText,    CFW_DarkMode_Value,       CFW_DarkMode_PreviewImage,
        CFW_DarkMode_PreviewText, CFW_MenuCovers_Value,     CFW_MenuCovers_PreviewText,
    };
    if (controller) {
        void *model = osos_settings_model(controller);
        for (unsigned i = 0; i < sizeof(properties) / sizeof(*properties); i++)
            osos_model_notify(model, OSOS_NOTIFY_ALL, properties[i]);
    }
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
    cfw_zero(&selection, sizeof(selection));
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
        cfw_copy(selection.id, metadata.id, sizeof(selection.id));
        path = &pack;
    } else if (dark) {
        cfw_theme_path(&pack, CFW_THEME_CLASSIC_DARK);
        path = &pack;
    }
    struct osos_path baseline_path;
    cfw_theme_path(&baseline_path, CFW_THEME_CLASSIC_BASELINE);
    int changed = cfw_theme_switch_apply(&switcher, &selection, path, &baseline_path);
    if (switcher.result == CFW_THEME_BUSY)
        return;
    error = switch_error();
    dark_error = fallback ? 0 : error;
    if (changed) {
        osos_menu_item_set_visible(CFW_DarkMode_Item,
                                   !selection.id[0] || metadata.appearance_mask == 3);
        set_active_name(id[0] ? metadata.name : "Classic");
    } else if (switcher.result == CFW_THEME_RECOVERED) {
        osos_menu_item_set_visible(CFW_DarkMode_Item, 1);
        set_active_name("Classic");
        changed = 1;
    } else if (switcher.result == CFW_THEME_RESTORE_FAILED) {
        set_active_name("Classic");
        disable();
        changed = 1;
    }
    notify(controller, changed);
}

/* Reset All Settings: saves Classic light with the default accent even when it
 * cannot be applied now, so the next boot uses it. */
void cfw_theme_settings_reset(void)
{
    cfw_theme_settings_load();
    struct cfw_theme_preferences classic;
    cfw_zero(&classic, sizeof(classic));
    classic.version = 1;
    cfw_theme_preferences_store(&classic);
    const struct cfw_theme_selection *active = &switcher.active.selection;
    if (active->id[0] || active->dark || active->accent != CFW_THEME_ACCENT_DEFAULT)
        apply(0, "", 0, CFW_THEME_ACCENT_DEFAULT, 1);
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
        struct osos_path baseline_path;
        cfw_theme_path(&baseline_path, CFW_THEME_CLASSIC_BASELINE);
        int changed =
            cfw_theme_switch_set_accent(&switcher, accent_order[row], &baseline_path);
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
                cfw_copy(choices[count - 1].id, metadata.id, sizeof(metadata.id));
                cfw_copy(choices[count - 1].name, metadata.name, sizeof(metadata.name));
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
