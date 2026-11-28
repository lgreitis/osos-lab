/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

/* Three native rows, the last one Reset Menu; the second is checked. */
static const uint32_t native_rows_table[15] = {1, 0, 0, 0, 0, 2, 0, 0, 0, 1, 3};
#undef OSOS_MAIN_MENU_SETTINGS_ROWS
#undef OSOS_MUSIC_MENU_SETTINGS_ROWS
#undef OSOS_MAIN_MENU_SETTINGS_COUNT
#undef OSOS_MUSIC_MENU_SETTINGS_COUNT
#define OSOS_MAIN_MENU_SETTINGS_ROWS ((uintptr_t)native_rows_table)
#define OSOS_MUSIC_MENU_SETTINGS_ROWS ((uintptr_t)native_rows_table)
#define OSOS_MAIN_MENU_SETTINGS_COUNT 3
#define OSOS_MUSIC_MENU_SETTINGS_COUNT 3

static uint32_t selected_row, native_index = UINT32_MAX, toggled = UINT32_MAX;
static unsigned rvx_rows = 2, rvx_toggled = 99, notifications;

static uint32_t selection_index(void *selection)
{
    (void)selection;
    return selected_row;
}

static void *ui_selection(void)
{
    return &selected_row;
}

static int indexed_property(void *model, uint32_t type, uint32_t property,
                            uint32_t index, uintptr_t *out)
{
    (void)model;
    (void)type;
    (void)property;
    native_index = index;
    *out = 1;
    return 1;
}

static uintptr_t ui_bitmap(uint32_t id)
{
    return id;
}

static void notify(void *model, uint32_t type, uint32_t property)
{
    (void)model;
    (void)type;
    (void)property;
    notifications++;
}

static void menu_toggle(void *model, unsigned menu, uint32_t index)
{
    (void)model;
    (void)menu;
    toggled = index;
}

#define osos_selection_index selection_index
#define osos_ui_selection ui_selection
#define osos_settings_indexed_property indexed_property
#define osos_ui_bitmap ui_bitmap
#define osos_model_notify notify
#define osos_menu_toggle menu_toggle
#include "../../../payload/ui_visibility.c"

unsigned cfw_rvx_visible_rows(unsigned menu)
{
    (void)menu;
    return rvx_rows;
}

int cfw_rvx_visible_property(unsigned menu, unsigned row, uint32_t type, int selected,
                             uintptr_t *out)
{
    (void)menu;
    (void)type;
    (void)selected;
    *out = 100 + row;
    return 1;
}

/* The runtime's order with no extension row anchored on a native row: all of
 * them before Reset Menu. */
uint32_t cfw_rvx_visible_locate(unsigned menu, uint32_t position, uint32_t *anchor)
{
    (void)menu;
    uint32_t last = OSOS_MAIN_MENU_SETTINGS_COUNT - 1;
    *anchor = last - 1;
    if (position < last)
        return position;
    if (position - last < rvx_rows)
        return RVX_VISIBILITY_ROW + (position - last);
    return position - rvx_rows == last ? last : UINT32_MAX;
}

void cfw_rvx_toggle_row(unsigned menu, unsigned row)
{
    (void)menu;
    rvx_toggled = row;
}

int main(void)
{
    /* Extension rows sit between the native rows and Reset Menu. */
    const uint32_t expected[] = {
        0, 1, RVX_VISIBILITY_ROW, RVX_VISIBILITY_ROW + 1, 2, UINT32_MAX};
    for (uint32_t row = 0; row < 6; row++) {
        selected_row = row;
        assert(cfw_ui_main_menu_index(NULL) == expected[row]);
        assert(cfw_ui_music_menu_index(NULL) == expected[row]);
    }
    uintptr_t count = 3;
    cfw_ui_visibility_adjust_property(OSOS_PROPERTY_COUNT, 0x8909, &count);
    assert(count == 5);
    count = 3;
    cfw_ui_visibility_adjust_property(OSOS_PROPERTY_COUNT, 0x1234, &count);
    assert(count == 3);

    uintptr_t out = 0;
    selected_row = 0;
    assert(cfw_ui_visibility_property(NULL, OSOS_PROPERTY_STRING, 0x890a, 3, &out));
    assert(out == 101);
    assert(cfw_ui_visibility_property(NULL, OSOS_PROPERTY_STRING, 0x890a, 4, &out));
    assert(native_index == 2 && out == 1);
    assert(cfw_ui_visibility_property(NULL, OSOS_RESOURCE_BMAP, 0x8909, 1, &out));
    assert(native_index == 1 && out == OSOS_MENU_CHECKED_BITMAP);
    selected_row = 4;
    assert(cfw_ui_visibility_property(NULL, OSOS_RESOURCE_BMAP, 0x8909, 4, &out));
    assert(out == OSOS_MENU_RESET_BITMAP);
    assert(!cfw_ui_visibility_property(NULL, OSOS_PROPERTY_STRING, 0x8909, 5, &out));

    /* Extension rows toggle themselves; Reset Menu and rows past the end do nothing. */
    cfw_ui_music_menu_toggle(NULL, RVX_VISIBILITY_ROW + 1);
    assert(rvx_toggled == 1 && notifications == 1 && toggled == UINT32_MAX);
    cfw_ui_main_menu_toggle(NULL, 1);
    assert(toggled == 1);
    toggled = UINT32_MAX;
    cfw_ui_main_menu_toggle(NULL, 2);
    cfw_ui_main_menu_toggle(NULL, UINT32_MAX);
    assert(toggled == UINT32_MAX && notifications == 1);

    /* Without extension rows the native list is unchanged. */
    rvx_rows = 0;
    selected_row = 2;
    assert(cfw_ui_main_menu_index(NULL) == 2);
    selected_row = 3;
    assert(cfw_ui_main_menu_index(NULL) == UINT32_MAX);
    return 0;
}
