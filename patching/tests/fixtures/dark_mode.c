/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
/* This fixture mocks native calls, not their 32-bit object layouts. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static uint32_t disk_record, disk_size = 4;
static int missing = 1, fail_write, writes, closes, redraws, notifications;
static int controller, model, surface;
static unsigned char drawn[4];

static int file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWDark"));
    if (mode == 1 && missing)
        return -1;
    *handle = &disk_record;
    return 0;
}

static int file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    memcpy(out, &disk_record, 4);
    *count = disk_size;
    return 0;
}

static int file_write(void *handle, const void *record, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    writes++;
    *count = fail_write ? 0 : size;
    if (!fail_write) {
        memcpy(&disk_record, record, size);
        missing = 0;
    }
    return 0;
}

static void file_close(void *handle)
{
    assert(handle == &disk_record);
    closes++;
}

static void *settings_model(void *value)
{
    assert(value == &controller);
    return &model;
}

static uintptr_t on_off(int value) { return value ? 2 : 1; }
static void invalidate(void) { redraws++; }
static void notify(void *value, uint32_t type, uint32_t property)
{
    assert(value == &model && type == OSOS_NOTIFY_ALL);
    (void)property;
    notifications++;
}

static void fill(void *target, const void *rect, const unsigned char *color,
                 uint32_t mode, void *clip)
{
    assert(target == &surface && rect == &model && mode == 7 && clip == &controller);
    memcpy(drawn, color, 4);
}

static void text_color(void *target, const unsigned char *color)
{
    assert(target == &surface);
    memcpy(drawn, color, 4);
}

static int clear_rgb, clear_alpha;
static void clear_color(int red, int green, int blue, int alpha)
{
    assert(red == green && green == blue);
    clear_rgb = red;
    clear_alpha = alpha;
}

static uint32_t blend_background;
static uint32_t blend(uint32_t pixel, uint32_t background, int weight)
{
    assert(pixel == 0xabcd && weight == 0x8000);
    blend_background = background;
    return 0x1234;
}

#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_settings_model settings_model
#define osos_settings_on_off on_off
#define osos_model_notify notify
#define osos_ui_invalidate invalidate
#define osos_surface_fill_rect fill
#define osos_graphics_set_color text_color
#define osos_gles_clear_color clear_color
#define osos_rgb565_blend blend
#include "../../../payload/dark_mode.c"

static int stock_actions, stock_properties, adjustments;
static int settings_action(void *value, const char *action, uint32_t argument)
{
    assert(value == &controller && !strcmp(action, "stock") && argument == 0);
    stock_actions++;
    return 7;
}

static int settings_property(void *value, uint32_t type, uint32_t property, uintptr_t *out)
{
    assert(value == &model);
    (void)type;
    stock_properties++;
    *out = 5;
    return property == 123;
}

int cfw_eq_action(const char *action, uint32_t argument)
{
    (void)argument;
    return !strcmp(action, "CFW_EQ_Open");
}

void cfw_album_artists_menu_adjust_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    assert(type == OSOS_PROPERTY_COUNT && property == 123 && *out == 5);
    adjustments++;
    ++*out;
}

#define osos_settings_action settings_action
#define osos_settings_property settings_property
#include "../../../payload/settings.c"

int main(void)
{
    uintptr_t value = 0;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !closes);
    assert(cfw_settings_property(&model, OSOS_PROPERTY_STRING, CFW_DarkMode_Value, &value));
    assert(value == 1 && !stock_properties);
    assert(cfw_settings_property(&model, OSOS_PROPERTY_COUNT, 123, &value));
    assert(value == 6 && adjustments == 1);
    assert(!cfw_settings_property(&model, OSOS_PROPERTY_COUNT, 124, &value));
    assert(adjustments == 1);
    assert(cfw_settings_action(&controller, "stock", 0) == 7);
    assert(cfw_settings_action(&controller, "CFW_EQ_Open", 0));
    assert(cfw_settings_action(&controller, "CFW_DarkMode_Toggle", OSOS_ACTION_SUPPORT_QUERY));
    assert(stock_actions == 1 && !writes && !redraws);

    const unsigned char fills[][4] = {
        {255, 255, 255, 173}, {0x7e, 0x81, 0x83, 127}, {35, 108, 196, 91},
    };
    const unsigned char texts[][4] = {{0, 0, 0, 123}, {255, 255, 255, 255}};
    for (int cycle = 0; cycle < 3; cycle++) {
        if (cycle)
            assert(cfw_settings_action(&controller, "CFW_DarkMode_Toggle", 0));
        int dark = cycle == 1;
        assert(cfw_dark_mode_enabled() == dark);
        for (unsigned i = 0; i < 3; i++) {
            cfw_dark_mode_fill(&surface, &model, fills[i], 7, &controller);
            if (dark && i < 2) {
                unsigned char shade = i ? 0x38 : 0x18;
                assert(drawn[0] == shade && drawn[1] == shade && drawn[2] == shade);
                assert(drawn[3] == fills[i][3]);
            } else {
                assert(!memcmp(drawn, fills[i], 4));
            }
        }
        for (unsigned i = 0; i < 2; i++) {
            cfw_dark_mode_text_color(&surface, texts[i]);
            assert(drawn[0] == (dark && !i ? 0xe8 : texts[i][0]));
            assert(drawn[3] == texts[i][3]);
        }
        cfw_dark_mode_clear_color(0x10000, 0x10000, 0x10000, 0x4321);
        assert(clear_rgb == (dark ? 0x18 * 0x10000 / 255 : 0x10000));
        assert(clear_alpha == 0x4321);
        assert(cfw_dark_mode_reflection_blend(0xabcd, 0xffff, 0x8000) == 0x1234);
        assert(blend_background == (dark ? 0x18c3u : 0xffffu));
        cfw_dark_mode_load();
        assert(cfw_dark_mode_enabled() == dark);
    }
    assert(writes == 2 && redraws == 2 && notifications == 2);
    fail_write = 1;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && redraws == 2 && notifications == 2);
    assert(closes == 5);
    disk_record = DARK_MODE_ON_RECORD;
    disk_size = 2;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
    disk_size = 4;
    disk_record = 0;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
}
