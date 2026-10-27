/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_visibility.h"
#include "osos.h"
#include "patch.h"
#include "preferences.h"

static int loaded;
static const uint32_t settings_items[] = {OSOS_SETTINGS_MAIN_MENU_ITEM,
                                          OSOS_SETTINGS_MUSIC_MENU_ITEM};
void *cfw_main_menu_reset_original(void *model);
void *cfw_music_menu_reset_original(void *model);

static void set_path(struct osos_path *path, const char *filename)
{
    path->volume = 0;
    uint32_t i = 0;
    while (filename[i] && i < sizeof(path->text) - 1) {
        path->text[i] = filename[i];
        i++;
    }
    path->text[i] = 0;
}

static void load_preferences(void)
{
    if (loaded)
        return;
    loaded = 1;
    for (uint32_t i = 0; i < cfw_ui_visibility_count; i++) {
        const struct cfw_ui_visibility *entry = &cfw_ui_visibility[i];
        struct osos_path path;
        set_path(&path, entry->filename);
        struct cfw_ui_visibility_file file;
        if (cfw_preferences_read(&path, &file, sizeof(file))) {
            if (cfw_ui_visibility_valid(&file, entry->key))
                *entry->visible = file.visible;
        }
    }
}

static void save_preferences(const struct cfw_ui_visibility *entry)
{
    struct osos_path path;
    set_path(&path, entry->filename);
    struct cfw_ui_visibility_file file =
        cfw_ui_visibility_encode(entry->key, *entry->visible);
    cfw_preferences_write(&path, &file, sizeof(file));
}

static void apply(const struct cfw_ui_visibility *entry, unsigned menu)
{
    osos_menu_item_set_visible(entry->items[menu], (*entry->visible >> menu) & 1);
}

static const uint32_t *native_rows(unsigned menu)
{
    return (const uint32_t *)(menu ? OSOS_MUSIC_MENU_SETTINGS_ROWS
                                   : OSOS_MAIN_MENU_SETTINGS_ROWS);
}

static uint32_t native_count(unsigned menu)
{
    return menu ? OSOS_MUSIC_MENU_SETTINGS_COUNT : OSOS_MAIN_MENU_SETTINGS_COUNT;
}

void cfw_ui_visibility_adjust_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type == OSOS_PROPERTY_COUNT && (property == 0x8909 || property == 0x890a)) {
        unsigned menu = property == 0x890a;
        *out +=
            cfw_ui_visibility_extra_count(menu, native_rows(menu), native_count(menu));
    }
}

int cfw_ui_visibility_property(void *model, uint32_t type, uint32_t property,
                               uint32_t index, uintptr_t *out)
{
    if (property != 0x8909 && property != 0x890a)
        return osos_settings_indexed_property(model, type, property, index, out);
    unsigned menu = property == 0x890a;
    const uint32_t *rows = native_rows(menu);
    uint32_t original = cfw_ui_visibility_index(menu, index, rows, native_count(menu));
    if (original == UINT32_MAX)
        return 0;
    load_preferences();
    const struct cfw_ui_visibility *entry = 0;
    if (original & CFW_UI_CUSTOM_ROW) {
        entry = &cfw_ui_visibility[original & ~CFW_UI_CUSTOM_ROW];
        for (original = 0; original < native_count(menu); original++)
            if (rows[original * 5] == entry->anchors[menu])
                break;
    }
    int result = osos_settings_indexed_property(model, type, property, original, out);
    if (!result)
        return 0;
    if (entry && type == OSOS_PROPERTY_STRING) {
        *out = osos_ui_string(entry->title);
    } else if (type == OSOS_RESOURCE_BMAP) {
        int checked = entry ? (*entry->visible >> menu) & 1
                            : ((const uint8_t *)rows)[original * 20 + 16] != 0;
        void *selection = osos_ui_selection();
        int selected = index == osos_selection_index(selection);
        uint32_t icon = checked ? OSOS_MENU_CHECKED_BITMAP + selected
                        : selected && original == native_count(menu) - 1
                            ? OSOS_MENU_RESET_BITMAP
                            : 0;
        *out = icon ? osos_ui_bitmap(icon) : 0;
    }
    return result;
}

PATCH_ARM uint32_t cfw_ui_main_menu_index(void *selection)
{
    return cfw_ui_visibility_index(0, osos_selection_index(selection), native_rows(0),
                                   native_count(0));
}

PATCH_ARM uint32_t cfw_ui_music_menu_index(void *selection)
{
    return cfw_ui_visibility_index(1, osos_selection_index(selection), native_rows(1),
                                   native_count(1));
}

static void toggle(void *model, unsigned menu, uint32_t index)
{
    if (!(index & CFW_UI_CUSTOM_ROW)) {
        if (index < native_count(menu) - 1)
            osos_menu_toggle(model, menu, index);
        return;
    }
    index &= ~CFW_UI_CUSTOM_ROW;
    if (index >= cfw_ui_visibility_count)
        return;
    load_preferences();
    const struct cfw_ui_visibility *entry = &cfw_ui_visibility[index];
    *entry->visible ^= 1u << menu;
    save_preferences(entry);
    apply(entry, menu);
    osos_model_notify(model, OSOS_NOTIFY_ALL, settings_items[menu]);
}

PATCH_ARM void cfw_ui_main_menu_toggle(void *model, uint32_t index)
{
    toggle(model, 0, index);
}

PATCH_ARM void cfw_ui_music_menu_toggle(void *model, uint32_t index)
{
    toggle(model, 1, index);
}

PATCH_ARM void *cfw_ui_visibility_load(void *model)
{
    void *result = osos_music_menu_apply(model);
    load_preferences();
    for (uint32_t i = 0; i < cfw_ui_visibility_count; i++)
        for (unsigned menu = 0; menu < 2; menu++)
            apply(&cfw_ui_visibility[i], menu);
    return result;
}

static void reset(unsigned menu)
{
    load_preferences();
    for (uint32_t i = 0; i < cfw_ui_visibility_count; i++) {
        const struct cfw_ui_visibility *entry = &cfw_ui_visibility[i];
        uint32_t mask = 1u << menu;
        *entry->visible = (*entry->visible & ~mask) | (entry->defaults & mask);
        save_preferences(entry);
        apply(entry, menu);
    }
}

PATCH_ARM void *cfw_ui_main_menu_reset(void *model)
{
    reset(0);
    return cfw_main_menu_reset_original(model);
}

PATCH_ARM void *cfw_ui_music_menu_reset(void *model)
{
    reset(1);
    return cfw_music_menu_reset_original(model);
}
