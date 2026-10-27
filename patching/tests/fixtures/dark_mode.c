/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#include "../../../payload/dark_mode.h"
#include "dark_mode_ui.h"
#include "custom_eq_ui.h"
#include "cfw_info_ui.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static int assets_ready = 1, assets_active, asset_cancellations;

int cfw_assets_prepare(void)
{
    return assets_ready;
}

void cfw_assets_cancel(void)
{
    asset_cancellations++;
}

void cfw_assets_set_enabled(int value)
{
    assets_active = value;
}

int cfw_assets_enabled(void)
{
    return assets_active;
}

static uint32_t disk_record, disk_size = 4;
static int missing = 1, fail_write, writes, closes, redraws, notifications;
static int controller, surface;

static struct {
    unsigned char reserved[0x378];
    void *preferences;
} model;

static int now_playing, now_playing_available = 1, icon_notifications;
static unsigned icon_invalidations;
static unsigned preview_invalidations;
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

static uintptr_t on_off(int value)
{
    return value ? 2 : 1;
}

static void *now_playing_model(void)
{
    return now_playing_available ? &now_playing : NULL;
}

static void invalidate(void)
{
    assert(icon_invalidations == (now_playing_available ? 3u : 0u));
    assert(preview_invalidations == 3);
    icon_invalidations = 0;
    preview_invalidations = 0;
    redraws++;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    if (value == &now_playing) {
        assert(type == OSOS_RESOURCE_DRAW);
        assert(property == 0x7f0c || property == 0x7f0d);
        assert(cfw_dark_mode_enabled() == (disk_record == 0x314b5244));
        unsigned bit = 1u << (property - 0x7f0c);
        assert(!(icon_invalidations & bit));
        icon_invalidations |= bit;
        icon_notifications++;
        return;
    }
    assert(value == &model && type == OSOS_NOTIFY_ALL);
    if (property == CFW_DarkMode_PreviewImage || property == CFW_DarkMode_PreviewText) {
        unsigned bit = property == CFW_DarkMode_PreviewImage ? 1 : 2;
        assert(!(preview_invalidations & bit));
        preview_invalidations |= bit;
        return;
    }
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

static uintptr_t resource(uint32_t id)
{
    return id;
}

const void *cfw_resource_bitmap(uint32_t id)
{
    return id == CFW_DarkMode_Moon || id == CFW_EQ_Icon || id == CFW_Info_Icon
               ? (const void *)(uintptr_t)id
               : NULL;
}

#define osos_ui_string resource
#define osos_ui_bitmap resource
#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_settings_model settings_model
#define osos_settings_on_off on_off
#define osos_model_notify notify
#define osos_now_playing_model now_playing_model
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

static int settings_property(void *value, uint32_t type, uint32_t property,
                             uintptr_t *out)
{
    assert(value == &model);
    /* The native Clicker bitmap getter crashes for the two extra output modes. */
    assert(type != OSOS_RESOURCE_BMAP || property != 0x8925);
    stock_properties++;
    *out = 5;
    return property == 123;
}

int cfw_eq_action(const char *action, uint32_t argument)
{
    (void)argument;
    return !strcmp(action, "CFW_EQ_Open");
}

static uint32_t clicker_value;
static uintptr_t preference_methods[0xe0 / 4];
static uintptr_t *preference_object = preference_methods;

static uint32_t get_preference(void *object, uint32_t property, uint32_t type)
{
    assert(object == &preference_object && property == 0x608c && type == 0x6000);
    return clicker_value;
}

static void set_preference(void *object, uint32_t property, uint32_t type,
                           uint32_t value)
{
    assert(object == &preference_object && property == 0x608c && type == 0x6000);
    clicker_value = value;
}

#include "../../../payload/clicker.c"

void cfw_ui_visibility_adjust_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    assert(type == OSOS_PROPERTY_COUNT && property == 123 && *out == 5);
    adjustments++;
    ++*out;
}

int cfw_ui_handle_action(const char *action, uint32_t argument)
{
    (void)action;
    (void)argument;
    return 0;
}

int cfw_ui_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    (void)type;
    (void)property;
    (void)out;
    return 0;
}

int cfw_ui_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                            uintptr_t *out)
{
    (void)type;
    (void)property;
    (void)index;
    (void)out;
    return 0;
}

int cfw_ui_visibility_property(void *model, uint32_t type, uint32_t property,
                               uint32_t index, uintptr_t *out)
{
    (void)model;
    (void)type;
    (void)property;
    (void)index;
    (void)out;
    return 0;
}

#define osos_settings_action settings_action
#define osos_settings_property settings_property
#include "../../../payload/settings.c"

static void check_clicker(void)
{
    const uint32_t modes[] = {0x608e, 0x6090, 0x608f, 0x608d};
    preference_methods[0xdc / 4] = (uintptr_t)get_preference;
    preference_methods[0xd4 / 4] = (uintptr_t)set_preference;
    model.preferences = &preference_object;
    clicker_value = modes[0];
    int previous_notifications = notifications;
    int previous_properties = stock_properties;
    assert(
        cfw_settings_action(NULL, "ToggleSetting_Clicker", OSOS_ACTION_SUPPORT_QUERY));
    assert(clicker_value == modes[0] && notifications == previous_notifications);

    for (unsigned i = 0; i < 4; i++) {
        uintptr_t image;
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP, 0x8925, &image));
        assert(image == (i == 0 ? 0x0dad0c04u : 0x0dad0c03u));
        assert(stock_properties == previous_properties);
        assert(cfw_settings_action(&controller, "ToggleSetting_Clicker", 0));
        assert(clicker_value == modes[(i + 1) % 4]);
    }
}

int main(void)
{
    uintptr_t value = 0;
    const uint32_t icons[] = {CFW_EQ_Icon, CFW_Info_Icon};
    for (unsigned i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP, icons[i], &value));
        assert(value == icons[i] && !stock_properties);
    }
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !closes);
    assert(cfw_settings_property(&model, OSOS_PROPERTY_STRING, CFW_DarkMode_Value,
                                 &value));
    assert(value == 1 && !stock_properties);
    assert(cfw_settings_property(&model, OSOS_PROPERTY_COUNT, 123, &value));
    assert(value == 6 && adjustments == 1);
    assert(!cfw_settings_property(&model, OSOS_PROPERTY_COUNT, 124, &value));
    assert(adjustments == 1);
    assert(cfw_settings_action(&controller, "stock", 0) == 7);
    assert(cfw_settings_action(&controller, "CFW_EQ_Open", 0));
    assert(cfw_settings_action(&controller, "CFW_DarkMode_Toggle",
                               OSOS_ACTION_SUPPORT_QUERY));
    assert(stock_actions == 1 && !writes && !redraws && !icon_notifications);

    struct color_case {
        unsigned char light[4], dark[4];
    };
    const struct color_case fills[] = {
        {{255, 255, 255, 173}, {0x18, 0x18, 0x18, 173}},
        {{0x7e, 0x81, 0x83, 127}, {0x38, 0x38, 0x38, 127}},
        {{35, 108, 196, 91}, {35, 108, 196, 91}},
        {{0x23, 0x6c, 0xc5, 91}, {0x00, 0xa1, 0xff, 91}},
        {{0xc5, 0x6c, 0x23, 91}, {0xc5, 0x6c, 0x23, 91}},
        {{0x31, 0x85, 0xd0, 77}, {0x31, 0x85, 0xd0, 77}},
    };
    const struct color_case texts[] = {
        {{0, 0, 0, 123}, {0xe8, 0xe8, 0xe8, 123}},
        {{255, 255, 255, 255}, {255, 255, 255, 255}},
        {{0x31, 0x85, 0xd0, 77}, {0x00, 0xa1, 0xff, 77}},
        {{0xd0, 0x85, 0x31, 77}, {0xd0, 0x85, 0x31, 77}},
        {{0x23, 0x6c, 0xc5, 91}, {0x00, 0xa1, 0xff, 91}},
    };
    for (int cycle = 0; cycle < 3; cycle++) {
        if (cycle)
            assert(cfw_settings_action(&controller, "CFW_DarkMode_Toggle", 0));
        int dark = cycle == 1;
        assert(cfw_dark_mode_enabled() == dark);
        const uint32_t black = 0;
        const uint32_t text_resources[] = {0x0dad030f, 0x0dad0dcf};
        for (unsigned i = 0; i < 2; i++) {
            uint32_t rgb =
                *(const uint32_t *)cfw_dark_mode_color(text_resources[i], &black);
            unsigned char rgba[] = {rgb >> 16, rgb >> 8, rgb, 123};
            cfw_dark_mode_text_color(&surface, rgba);
            assert(drawn[0] == 0 && drawn[1] == 0 && drawn[2] == dark &&
                   drawn[3] == 123);
        }
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP,
                                     CFW_DarkMode_PreviewImage, &value));
        assert(value == (dark ? CFW_DarkMode_Moon : IMG_SUN));
        assert(cfw_settings_property(&model, OSOS_PROPERTY_STRING,
                                     CFW_DarkMode_PreviewText, &value));
        assert(value == (dark ? CFW_DarkMode_DarkLabel : CFW_DarkMode_LightLabel));
        for (unsigned i = 0; i < sizeof(fills) / sizeof(fills[0]); i++) {
            cfw_dark_mode_fill(&surface, &model, fills[i].light, 7, &controller);
            assert(
                !memcmp(drawn, dark ? fills[i].dark : fills[i].light, sizeof(drawn)));
        }
        for (unsigned i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
            cfw_dark_mode_text_color(&surface, texts[i].light);
            assert(
                !memcmp(drawn, dark ? texts[i].dark : texts[i].light, sizeof(drawn)));
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
    assert(icon_notifications == 4);
    assets_ready = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && writes == 2 && redraws == 2);
    disk_record = DARK_MODE_ON_RECORD;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
    assets_ready = 1;
    fail_write = 1;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && redraws == 2 && notifications == 2);
    assert(icon_notifications == 4 && !icon_invalidations);
    assert(closes == 6);
    disk_record = DARK_MODE_ON_RECORD;
    disk_size = 2;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
    disk_size = 4;
    disk_record = 0;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
    fail_write = 0;
    now_playing_available = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled() && redraws == 3 && notifications == 3);
    assert(icon_notifications == 4);
    int previous_writes = writes;
    assets_ready = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled() && disk_record == DARK_MODE_ON_RECORD);
    assert(writes == previous_writes && redraws == 3 && notifications == 3);
    assets_ready = 1;
    fail_write = 1;
    int previous_cancellations = asset_cancellations;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled() && disk_record == DARK_MODE_ON_RECORD);
    assert(asset_cancellations == previous_cancellations + 1);
    assert(redraws == 3 && notifications == 3);
    fail_write = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && disk_record == DARK_MODE_OFF_RECORD);
    assert(redraws == 4 && notifications == 4);
    assert(!preview_invalidations);
    check_clicker();
}
