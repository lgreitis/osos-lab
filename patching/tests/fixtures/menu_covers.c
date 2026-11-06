/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#include "../../../payload/menu_covers.h"
#include "../../../payload/theme_style.h"
#include "menu_covers_ui.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static struct {
    unsigned char reserved[0xb0];
    void *model;
} controller;
static int model, handle, provider;
static uint32_t stored;
static int present, write_error;
static int opens, writes, notifications, invalidations, queries;
static unsigned render_flags;

unsigned cfw_theme_render_flags(void)
{
    return render_flags;
}

static int file_open(const struct osos_path *path, uint32_t mode, void **out)
{
    assert(path->volume == 0);
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWCovers"));
    assert(mode == 1 || mode == 2);
    if (mode == 1 && !present)
        return -43;
    if (mode == 2 && write_error)
        return -1;
    opens++;
    *out = &handle;
    return 0;
}

static int file_read(void *value, void *out, uint32_t size, uint32_t *count)
{
    assert(value == &handle && size == sizeof(stored));
    memcpy(out, &stored, sizeof(stored));
    *count = sizeof(stored);
    return 0;
}

static int file_write(void *value, const void *data, uint32_t size, uint32_t *count)
{
    assert(value == &handle && size == sizeof(stored));
    memcpy(&stored, data, sizeof(stored));
    present = 1;
    writes++;
    *count = sizeof(stored);
    return 0;
}

static void file_close(void *value)
{
    assert(value == &handle);
}

static uintptr_t string(uint32_t id)
{
    return id;
}

static uintptr_t on_off(int value)
{
    return value ? 2 : 1;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    assert(value == &model && type == OSOS_NOTIFY_ALL);
    assert(property == CFW_MenuCovers_Value || property == CFW_MenuCovers_PreviewText);
    notifications++;
}

static void invalidate(void)
{
    invalidations++;
}

#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_ui_string string
#define osos_settings_on_off on_off
#define osos_model_notify notify
#define osos_ui_invalidate invalidate
#include "../../../payload/menu_covers.c"

int cfw_menu_covers_query_original(void *value, uint32_t flags)
{
    assert(value == &provider && flags == 0);
    queries++;
    return 7;
}

/* The row reads Theme while following the theme, otherwise On or Off. */
static void check(int enabled, int theme)
{
    uintptr_t value = 0;
    assert(cfw_menu_covers_enabled() == enabled);
    assert(cfw_menu_covers_property(OSOS_PROPERTY_STRING, CFW_MenuCovers_Value, &value));
    assert(value == (theme ? CFW_MenuCovers_ThemeLabel : enabled ? 2u : 1u));
    assert(cfw_menu_covers_property(OSOS_PROPERTY_STRING, CFW_MenuCovers_PreviewText,
                                    &value));
    assert(value == (enabled ? CFW_MenuCovers_OnLabel : CFW_MenuCovers_OffLabel));
    assert(!cfw_menu_covers_property(OSOS_RESOURCE_BMAP, CFW_MenuCovers_Value, &value));
    assert(!cfw_menu_covers_property(OSOS_PROPERTY_STRING, CFW_MenuCovers_Item, &value));
    int before = queries;
    assert(cfw_menu_covers_query(&provider, 0) == (enabled ? 7 : 0));
    assert(queries == before + enabled);
}

int main(void)
{
    controller.model = &model;

    /* No preference file: the theme decides, and Classic shows covers. */
    cfw_menu_covers_load();
    check(1, 1);
    render_flags = CFW_THEME_MENU_COVERS_OFF;
    check(0, 1);
    render_flags = CFW_THEME_FLAT_SELECTION;
    check(1, 1);

    /* Unknown records are ignored. */
    present = 1;
    stored = 0x12345678;
    cfw_menu_covers_load();
    check(1, 1);

    /* A stored choice outranks the theme. */
    render_flags = CFW_THEME_MENU_COVERS_OFF;
    stored = COVERS_ON_RECORD;
    cfw_menu_covers_load();
    check(1, 0);

    render_flags = 0;
    stored = COVERS_OFF_RECORD;
    cfw_menu_covers_load();
    check(0, 0);

    /* A stored Theme choice follows the theme again. */
    render_flags = CFW_THEME_MENU_COVERS_OFF;
    stored = COVERS_THEME_RECORD;
    cfw_menu_covers_load();
    check(0, 1);

    /* Other actions pass through; the toggle answers the support query. */
    assert(!cfw_menu_covers_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_menu_covers_action(&controller, NULL, 0));
    assert(cfw_menu_covers_action(&controller, "CFW_MenuCovers_Toggle",
                                  OSOS_ACTION_SUPPORT_QUERY));
    assert(writes == 0 && notifications == 0);

    /* The toggle cycles Theme, On, Off and back to Theme. */
    assert(cfw_menu_covers_action(&controller, "CFW_MenuCovers_Toggle", 0));
    assert(stored == COVERS_ON_RECORD && writes == 1);
    assert(notifications == 2 && invalidations == 1);
    check(1, 0);

    assert(cfw_menu_covers_action(&controller, "CFW_MenuCovers_Toggle", 0));
    assert(stored == COVERS_OFF_RECORD && writes == 2);
    assert(notifications == 4 && invalidations == 2);
    check(0, 0);

    /* A failed write keeps the current state. */
    write_error = 1;
    assert(cfw_menu_covers_action(&controller, "CFW_MenuCovers_Toggle", 0));
    assert(stored == COVERS_OFF_RECORD && writes == 2 && notifications == 4);
    check(0, 0);

    write_error = 0;
    assert(cfw_menu_covers_action(&controller, "CFW_MenuCovers_Toggle", 0));
    assert(stored == COVERS_THEME_RECORD && writes == 3);
    assert(notifications == 6 && invalidations == 3);
    check(0, 1);
    return 0;
}
