/* SPDX-License-Identifier: GPL-3.0-only */
/* Extension rows in Settings > Main Menu and Music Menu, before Reset Menu. */
#include "ui_visibility.h"
#include "osos.h"
#include "patch.h"
#include "rvx/runtime.h"

static const uint32_t settings_items[] = {OSOS_SETTINGS_MAIN_MENU_ITEM,
                                          OSOS_SETTINGS_MUSIC_MENU_ITEM};

static const uint32_t *native_rows(unsigned menu)
{
    return (const uint32_t *)(menu ? OSOS_MUSIC_MENU_SETTINGS_ROWS
                                   : OSOS_MAIN_MENU_SETTINGS_ROWS);
}

static uint32_t native_count(unsigned menu)
{
    return menu ? OSOS_MUSIC_MENU_SETTINGS_COUNT : OSOS_MAIN_MENU_SETTINGS_COUNT;
}

enum { RVX_VISIBILITY_ROW = 0x40000000 };

static uint32_t displayed_index(unsigned menu, uint32_t row)
{
    uint32_t last = native_count(menu) - 1;
    if (row >= last) {
        unsigned added = cfw_rvx_visible_rows(menu);
        if (row - last < added)
            return RVX_VISIBILITY_ROW + (row - last);
        row -= added;
    }
    return row < native_count(menu) ? row : UINT32_MAX;
}

void cfw_ui_visibility_adjust_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type == OSOS_PROPERTY_COUNT && (property == 0x8909 || property == 0x890a))
        *out += cfw_rvx_visible_rows(property == 0x890a);
}

int cfw_ui_visibility_property(void *model, uint32_t type, uint32_t property,
                               uint32_t index, uintptr_t *out)
{
    if (property != 0x8909 && property != 0x890a)
        return osos_settings_indexed_property(model, type, property, index, out);
    unsigned menu = property == 0x890a;
    const uint32_t *rows = native_rows(menu);
    uint32_t original = displayed_index(menu, index);
    if (original == UINT32_MAX)
        return 0;
    if (original & RVX_VISIBILITY_ROW) {
        unsigned selected = index == osos_selection_index(osos_ui_selection());
        if (cfw_rvx_visible_property(menu, original - RVX_VISIBILITY_ROW, type,
                                     selected, out))
            return 1;
        return osos_settings_indexed_property(model, type, property, 0, out);
    }
    int result = osos_settings_indexed_property(model, type, property, original, out);
    if (result && type == OSOS_RESOURCE_BMAP) {
        int checked = ((const uint8_t *)rows)[original * 20 + 16] != 0;
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
    return displayed_index(0, osos_selection_index(selection));
}

PATCH_ARM uint32_t cfw_ui_music_menu_index(void *selection)
{
    return displayed_index(1, osos_selection_index(selection));
}

static void toggle(void *model, unsigned menu, uint32_t index)
{
    if (index != UINT32_MAX && (index & RVX_VISIBILITY_ROW)) {
        cfw_rvx_toggle_row(menu, index - RVX_VISIBILITY_ROW);
        osos_model_notify(model, OSOS_NOTIFY_ALL, settings_items[menu]);
        return;
    }
    if (index < native_count(menu) - 1)
        osos_menu_toggle(model, menu, index);
}

PATCH_ARM void cfw_ui_main_menu_toggle(void *model, uint32_t index)
{
    toggle(model, 0, index);
}

PATCH_ARM void cfw_ui_music_menu_toggle(void *model, uint32_t index)
{
    toggle(model, 1, index);
}
