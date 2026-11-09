/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
static void fill(void *, const void *, const unsigned char *, uint32_t, void *);
#undef OSOS_SURFACE_FILL_RECT
#define OSOS_SURFACE_FILL_RECT ((uintptr_t)fill)
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

static uint32_t disk_record, disk_size = 4, stored_record;
static int missing = 1, legacy_reads, stored, migrating, store_sets, redraws,
           notifications;
static int controller, surface;

static struct {
    unsigned char reserved[0x378];
    void *preferences;
} model;

static int now_playing, now_playing_available = 1, icon_notifications;
static unsigned icon_invalidations;
static unsigned preview_invalidations;
static unsigned char drawn[4];
static int direct_fill;
static unsigned char *drawing_context;
static unsigned char bitmap[32];
static uint32_t ordinary_view[0xa4 / 4];

#include "../../../payload/store.h"
#include "../../../payload/preferences.h"

uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity)
{
    assert(key == CFW_STORE_DARK && capacity == 4);
    if (!stored)
        return 0;
    memcpy(data, &stored_record, 4);
    return 4;
}

void cfw_store_set(uint32_t key, const void *data, uint32_t size)
{
    assert(key == CFW_STORE_DARK && size == 4);
    memcpy(&stored_record, data, 4);
    stored = 1;
    store_sets++;
}

int cfw_store_migrating(void)
{
    return migrating;
}

int cfw_preferences_read(const char *name, void *record, uint32_t size)
{
    assert(!strcmp(name, "iPod_Control\\Device\\CFWDark") && size == 4);
    legacy_reads++;
    if (missing)
        return 0;
    memcpy(record, &disk_record, 4);
    return disk_size == 4;
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

static int resetting;

static void invalidate(void)
{
    assert(icon_invalidations == (now_playing_available ? 3u : 0u));
    /* Reset All Settings has no Settings model to notify. */
    assert(preview_invalidations == (resetting ? 0u : 3u));
    icon_invalidations = 0;
    preview_invalidations = 0;
    redraws++;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    if (value == &now_playing) {
        assert(type == OSOS_RESOURCE_DRAW);
        assert(property == 0x7f0c || property == 0x7f0d);
        assert(cfw_dark_mode_enabled() == (stored_record == 0x314b5244));
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
    if (direct_fill) {
        const int32_t expected[] = {22, 8, 42, 28};
        assert(target == bitmap + 4 && mode == 0x22 && clip == drawing_context + 0x34);
        assert(!memcmp(rect, expected, sizeof(expected)));
    } else {
        assert(target == &surface && rect == &model && mode == 7 &&
               clip == &controller);
    }
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

static void graphics_fill(void *context, const int32_t *rect);
#define osos_graphics_fill_rect graphics_fill
#define osos_ui_string resource
#define osos_ui_bitmap resource
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

static void graphics_fill(void *context, const int32_t *rect)
{
    (void)rect;
    direct_fill = 0;
    cfw_dark_mode_fill(&surface, &model, (unsigned char *)context + 0x11, 7,
                       &controller);
}

static void check_cached_colors(void)
{
    const uint32_t text_views[] = {0x0dad030e, 0x0dad0607, 0x0dad061e, 0x0dad0ddc};
    const uint32_t backgrounds[] = {0x0dad08c5, 0x0dad0925, 0x0dad092d, 0x0dad09ae};
    const unsigned char cached_black[] = {0, 0, 0, 123};
    const unsigned char selected_white[] = {255, 255, 255, 255};
    uint32_t view[0xa4 / 4] = {0};
    /* Keep the native pointer field aligned on a 64-bit host. */
    uint64_t storage[16] = {0};
    drawing_context = (unsigned char *)storage + 4;
    *(unsigned char **)(drawing_context + 0x1c) = bitmap;
    *(int32_t *)(drawing_context + 0x2c) = -3;
    *(int32_t *)(drawing_context + 0x30) = 12;
    drawing_context[0x10] = 0x22;
    const unsigned char cached_white[] = {255, 255, 255, 173};
    memcpy(drawing_context + 0x11, cached_white, 4);
    const int32_t rect[] = {10, 11, 30, 31};
    int was_active = assets_active;
    const int modes[] = {0, 1, 0, 1, 1, 0};
    for (unsigned cycle = 0; cycle < sizeof(modes) / sizeof(modes[0]); cycle++) {
        assets_active = modes[cycle];
        for (unsigned i = 0; i < 4; i++) {
            view[0x4c / 4] = text_views[i];
            view[0x44 / 4] =
                0x1234 + cycle; /* Binding changes must not change identity. */
            cfw_dark_mode_text_color(&surface, cached_black, view);
            assert(!memcmp(drawn, cached_black, 4));
            cfw_dark_mode_text_color(&surface, selected_white, view);
            assert(!memcmp(drawn, selected_white, 4));
            view[0x4c / 4] = backgrounds[i];
            direct_fill = 1;
            cfw_dark_mode_background_fill(drawing_context, rect, view);
            assert(direct_fill && !memcmp(drawn, cached_white, 4));
            assert(!memcmp(drawing_context + 0x11, cached_white, 4));
        }
        cfw_dark_mode_text_color(&surface, cached_black, ordinary_view);
        assert(drawn[0] == (assets_active ? 0xe8 : 0) && drawn[3] == 123);
        view[0x4c / 4] = 0x0dad092a; /* Lyrics remains themed. */
        cfw_dark_mode_background_fill(drawing_context, rect, view);
        assert(!direct_fill && drawn[0] == (assets_active ? 0x18 : 255) &&
               drawn[3] == 173);
    }
    assets_active = was_active;
}

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

static char boot_calls[8];
static unsigned boot_count, native_flushes, store_flushes;
static const char *deleted[4];
static unsigned delete_count;
static int native_preferences_object;

static void boot_step(char step)
{
    assert(boot_count < sizeof(boot_calls) - 1);
    boot_calls[boot_count++] = step;
}

void cfw_store_load(void)
{
    boot_step('S');
}

void cfw_eq_load_preferences(void)
{
    boot_step('E');
}

void cfw_ui_visibility_load_preferences(void)
{
    boot_step('V');
}

void cfw_store_flush(void)
{
    boot_step('F');
    store_flushes++;
}

void cfw_preferences_delete(const char *name)
{
    assert(delete_count < 4);
    deleted[delete_count++] = name;
}

void cfw_ui_visibility_delete_legacy(void)
{
    cfw_preferences_delete("menus");
}

static int reset_model;

static void native_reset(void *value)
{
    assert(value == &reset_model);
    boot_step('N');
}

void cfw_eq_reset_preferences(void)
{
    boot_step('Q');
}

static void native_flush(void *preferences)
{
    assert(preferences == &native_preferences_object);
    native_flushes++;
}

int cfw_file_info_action(const char *action, uint32_t argument)
{
    (void)action;
    (void)argument;
    return 0;
}

int cfw_file_info_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    (void)type;
    (void)property;
    (void)out;
    return 0;
}

int cfw_file_info_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                                   uintptr_t *out)
{
    (void)type;
    (void)property;
    (void)index;
    (void)out;
    return 0;
}

#define osos_settings_action settings_action
#define osos_settings_property settings_property
#define osos_preferences_flush native_flush
#define osos_reset_all_settings native_reset
#include "../../../payload/settings.c"

static void check_settings_lifecycle(void)
{
    boot_count = 0;
    cfw_settings_boot(&native_preferences_object);
    assert(!strcmp(boot_calls, "SEVF"));
    cfw_settings_sleep();
    assert(native_flushes == 1 && store_flushes == 2);
    cfw_store_delete_legacy();
    assert(delete_count == 3 && !strcmp(deleted[0], "iPod_Control\\Device\\CFWEQ") &&
           !strcmp(deleted[1], "iPod_Control\\Device\\CFWDark") &&
           !strcmp(deleted[2], "menus"));
}

static void check_reset_all_settings(void)
{
    now_playing_available = 1;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled());
    resetting = 1;
    int previous_redraws = redraws, previous_notifications = notifications;
    int previous_icons = icon_notifications;
    boot_count = 0;
    memset(boot_calls, 0, sizeof(boot_calls));
    cfw_settings_reset(&reset_model);
    assert(!strcmp(boot_calls, "NQ"));
    assert(!cfw_dark_mode_enabled() && stored_record == DARK_MODE_OFF_RECORD);
    assert(redraws == previous_redraws + 1 && notifications == previous_notifications);
    assert(icon_notifications == previous_icons + 2);

    /* Already light: only the record is written. */
    stored = 0;
    stored_record = DARK_MODE_ON_RECORD;
    cfw_settings_reset(&reset_model);
    assert(stored && stored_record == DARK_MODE_OFF_RECORD);
    assert(redraws == previous_redraws + 1);

    /* Without loadable light assets, Off is still saved for the next boot. */
    assets_active = 1;
    stored_record = DARK_MODE_ON_RECORD;
    assets_ready = 0;
    cfw_settings_reset(&reset_model);
    assert(cfw_dark_mode_enabled() && stored_record == DARK_MODE_OFF_RECORD);
    assert(redraws == previous_redraws + 1);
    assets_ready = 1;
    assets_active = 0;
    resetting = 0;
}

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
    check_cached_colors();
    uintptr_t value = 0;
    const uint32_t icons[] = {CFW_EQ_Icon, CFW_Info_Icon};
    for (unsigned i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP, icons[i], &value));
        assert(value == icons[i] && !stock_properties);
    }
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !legacy_reads);
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
    assert(stock_actions == 1 && !store_sets && !redraws && !icon_notifications);

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
            cfw_dark_mode_text_color(&surface, texts[i].light, ordinary_view);
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
    assert(store_sets == 2 && redraws == 2 && notifications == 2 && !legacy_reads);
    assert(icon_notifications == 4);
    assets_ready = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && store_sets == 2 && redraws == 2);
    stored_record = DARK_MODE_ON_RECORD;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled());
    assets_ready = 1;
    stored_record = DARK_MODE_OFF_RECORD;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !legacy_reads);

    /* Legacy CFWDark is imported only while the store migrates. */
    stored = 0;
    missing = 0;
    disk_record = DARK_MODE_ON_RECORD;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !legacy_reads && !stored);
    migrating = 1;
    cfw_dark_mode_load();
    assert(cfw_dark_mode_enabled() && legacy_reads == 1 && stored);
    assert(stored_record == DARK_MODE_ON_RECORD && store_sets == 3);
    stored_record = DARK_MODE_OFF_RECORD;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && legacy_reads == 1);
    stored = 0;
    disk_record = 0x12345678;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !stored);
    disk_record = DARK_MODE_ON_RECORD;
    disk_size = 2;
    cfw_dark_mode_load();
    assert(!cfw_dark_mode_enabled() && !stored);
    disk_size = 4;
    migrating = 0;

    now_playing_available = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled() && redraws == 3 && notifications == 3);
    assert(icon_notifications == 4 && stored_record == DARK_MODE_ON_RECORD);
    int previous_sets = store_sets;
    assets_ready = 0;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(cfw_dark_mode_enabled() && stored_record == DARK_MODE_ON_RECORD);
    assert(store_sets == previous_sets && redraws == 3 && notifications == 3);
    assets_ready = 1;
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(!cfw_dark_mode_enabled() && stored_record == DARK_MODE_OFF_RECORD);
    assert(redraws == 4 && notifications == 4 && !asset_cancellations);
    assert(!preview_invalidations);
    check_clicker();
    check_settings_lifecycle();
    check_reset_all_settings();
}
