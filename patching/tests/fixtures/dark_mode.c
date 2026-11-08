/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
static void fill(void *, const void *, const unsigned char *, uint32_t, void *);
#undef OSOS_SURFACE_FILL_RECT
#define OSOS_SURFACE_FILL_RECT ((uintptr_t)fill)
/* Stands in for the native system palette's white entry. */
static const unsigned char palette_white[4] = {255, 255, 255, 255};
#undef OSOS_PALETTE_WHITE
#define OSOS_PALETTE_WHITE ((uintptr_t)palette_white)
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

static int notifications, toggles;
static int controller, surface;

static struct {
    unsigned char reserved[0x378];
    void *preferences;
} model;

static unsigned char drawn[4];
static int direct_fill;
static unsigned char *drawing_context;
static unsigned char bitmap[32];
static uint32_t ordinary_view[0xa4 / 4];

static void *settings_model(void *value)
{
    assert(value == &controller);
    return &model;
}

static uintptr_t on_off(int value)
{
    return value ? 2 : 1;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    assert(value == &model && type == OSOS_NOTIFY_ALL);
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

static int clear_rgb[3], clear_alpha;

static void clear_color(int red, int green, int blue, int alpha)
{
    clear_rgb[0] = red;
    clear_rgb[1] = green;
    clear_rgb[2] = blue;
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
#define osos_surface_fill_rect fill
#define osos_graphics_set_color text_color
static int renderer_draws, edge_line_draws;
static uint16_t render_target[320 * 240], album_art[128 * 128];

/* The native image bitmap keeps its pixel pointer at +4. */
static unsigned char album_art_image[4 + sizeof(uint16_t *)];

#include "../../../payload/theme_render.h"

/* Stands in for the native view draw, which reaches the cover wrapper. */
static void album_art_view_draw(void *view)
{
    cfw_theme_album_art_draw(view);
    cfw_theme_album_art_edge_lines(view);
}

static const void *album_art_view_image(const void *view)
{
    assert(view == &surface);
    const uint16_t *pixels = album_art;
    memcpy(album_art_image + 4, &pixels, sizeof(pixels));
    return album_art_image;
}

static void artwork_renderer_draw(void *renderer)
{
    assert(renderer == &surface);
    renderer_draws++;
}

static void artwork_edge_lines(const void *bitmap)
{
    assert(bitmap == &surface);
    edge_line_draws++;
}

static uint16_t *gles_render_target(void)
{
    return render_target;
}

#define osos_gles_clear_color clear_color
#define osos_rgb565_blend blend
#define osos_album_art_view_draw album_art_view_draw
#define osos_album_art_view_image album_art_view_image
#define osos_artwork_renderer_draw artwork_renderer_draw
#define osos_artwork_edge_lines artwork_edge_lines
#define osos_gles_render_target gles_render_target
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"

void cfw_theme_settings_toggle(void *value)
{
    assert(value == &controller);
    toggles++;
}

const char *cfw_theme_settings_error(void)
{
    return NULL;
}

int cfw_theme_settings_action(void *c, const char *a, uint32_t v)
{
    (void)c;
    (void)a;
    (void)v;
    return 0;
}

int cfw_theme_settings_property(uint32_t t, uint32_t p, uintptr_t *o)
{
    (void)t;
    (void)p;
    (void)o;
    return 0;
}

int cfw_theme_settings_indexed_property(uint32_t t, uint32_t p, uint32_t i,
                                        uintptr_t *o)
{
    (void)t;
    (void)p;
    (void)i;
    (void)o;
    return 0;
}

#include "../../../payload/dark_mode.c"
#include "../../../payload/theme_render.c"

/* Bitmap tinting is covered by the theme_switch fixture. */
const unsigned char *cfw_theme_highlight_tint(const void *view)
{
    (void)view;
    return 0;
}

/* Well letter colors are derived in theme_tint.c; the hook takes what it gets. */
static const unsigned char *well_letter;
static unsigned well_styles_asked;

const unsigned char *cfw_theme_well_letter_color(unsigned style)
{
    well_styles_asked |= 1u << style;
    return well_letter;
}

/* Field text colors are derived in theme_tint.c as well. */
static const unsigned char *field_text;
static unsigned field_asked;

const unsigned char *cfw_theme_field_text_color(void)
{
    field_asked++;
    return field_text;
}

/* So are the colors of labels over listed art, and of calendar labels. */
static const unsigned char *art_text;
static uint32_t art_view;
static unsigned art_asked;

const unsigned char *cfw_theme_contrast_text_color(uint32_t view)
{
    art_asked++;
    art_view = view;
    return art_text;
}

static const unsigned char *calendar_text;
static unsigned calendar_place;

const unsigned char *cfw_theme_calendar_text_color(unsigned place, unsigned char out[4])
{
    (void)out;
    calendar_place = place;
    return calendar_text;
}

/* Both bar hooks recognize the view's own label colors by pointer. */
static void check_search_key(int key, const unsigned char *expected)
{
    static unsigned char view[0x1d8];
    static const unsigned char native[] = {255, 255, 255, 201};
    memcpy(view + 0x1cd, native, 4);
    memcpy(view + 0x1d1, native, 4);
    cfw_theme_search_key_color(&surface, view + (key ? 0x1d1 : 0x1cd), view);
    const unsigned char want[] = {expected[0], expected[1], expected[2], native[3]};
    assert(!memcmp(drawn, want, 4));
}

static void check_option_bar(unsigned style, int key, const unsigned char *expected)
{
    static unsigned char view[0x1d8];
    static const unsigned char native[] = {255, 255, 255, 202};
    view[0x121] = style;
    memcpy(view + 0x1cc, native, 4);
    memcpy(view + 0x1d0, native, 4);
    cfw_theme_option_bar_color(&surface, view + (key ? 0x1d0 : 0x1cc), view);
    const unsigned char want[] = {expected[0], expected[1], expected[2], native[3]};
    assert(!memcmp(drawn, want, 4));
}

static void graphics_fill(void *context, const int32_t *rect)
{
    (void)rect;
    direct_fill = 0;
    cfw_theme_fill(&surface, &model, (unsigned char *)context + 0x11, 7, &controller);
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
    int was_active = cfw_dark_mode_enabled();
    const int modes[] = {0, 1, 0, 1, 1, 0};
    for (unsigned cycle = 0; cycle < sizeof(modes) / sizeof(modes[0]); cycle++) {
        cfw_theme_appearance_set(modes[cycle]);
        for (unsigned i = 0; i < 4; i++) {
            view[0x4c / 4] = text_views[i];
            view[0x44 / 4] =
                0x1234 + cycle; /* Binding changes must not change identity. */
            cfw_theme_text_color(&surface, cached_black, view);
            assert(!memcmp(drawn, cached_black, 4));
            cfw_theme_text_color(&surface, selected_white, view);
            assert(!memcmp(drawn, selected_white, 4));
            view[0x4c / 4] = backgrounds[i];
            direct_fill = 1;
            cfw_theme_background_fill(drawing_context, rect, view);
            assert(direct_fill && !memcmp(drawn, cached_white, 4));
            assert(!memcmp(drawing_context + 0x11, cached_white, 4));
        }
        cfw_theme_text_color(&surface, cached_black, ordinary_view);
        assert(drawn[0] == (cfw_dark_mode_enabled() ? 0xe8 : 0) && drawn[3] == 123);
        view[0x4c / 4] = 0x0dad092a; /* Lyrics remains themed. */
        cfw_theme_background_fill(drawing_context, rect, view);
        assert(!direct_fill && drawn[0] == (cfw_dark_mode_enabled() ? 0x18 : 255) &&
               drawn[3] == 173);
    }
    cfw_theme_appearance_set(was_active);
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

int cfw_menu_covers_action(void *c, const char *a, uint32_t v)
{
    (void)c;
    (void)a;
    (void)v;
    return 0;
}

int cfw_menu_covers_property(uint32_t t, uint32_t p, uintptr_t *o)
{
    (void)t;
    (void)p;
    (void)o;
    return 0;
}

static char boot_calls[16];
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

void cfw_theme_settings_load(void)
{
    boot_step('T');
}

void cfw_menu_covers_load(void)
{
    boot_step('M');
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

#define osos_settings_action settings_action
#define osos_settings_property settings_property
#define osos_preferences_flush native_flush
#define osos_reset_all_settings native_reset
#include "../../../payload/settings.c"

static void check_settings_lifecycle(void)
{
    boot_count = 0;
    cfw_settings_boot(&native_preferences_object);
    assert(!strcmp(boot_calls, "SETMVF"));
    cfw_settings_sleep();
    assert(native_flushes == 1 && store_flushes == 2);
    cfw_store_delete_legacy();
    assert(delete_count == 3 && !strcmp(deleted[0], "iPod_Control\\Device\\CFWEQ") &&
           !strcmp(deleted[1], "iPod_Control\\Device\\CFWDark") &&
           !strcmp(deleted[2], "menus"));
    boot_count = 0;
    memset(boot_calls, 0, sizeof(boot_calls));
    cfw_settings_reset(&reset_model);
    assert(!strcmp(boot_calls, "NQ"));
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

static void check_fill(const unsigned char *native, unsigned char red,
                       unsigned char green, unsigned char blue)
{
    cfw_theme_fill(&surface, &model, native, 7, &controller);
    const unsigned char expected[] = {red, green, blue, native[3]};
    assert(!memcmp(drawn, expected, 4));
}

static void check_text(const void *view, const unsigned char *native, unsigned char red,
                       unsigned char green, unsigned char blue)
{
    cfw_theme_text_color(&surface, native, view);
    const unsigned char expected[] = {red, green, blue, native[3]};
    assert(!memcmp(drawn, expected, 4));
}

static void check_background(int red, int green, int blue, uint32_t rgb565)
{
    cfw_theme_clear_color(0x10000, 0x10000, 0x10000, 0x4321);
    assert(clear_rgb[0] == red && clear_rgb[1] == green && clear_rgb[2] == blue);
    assert(clear_alpha == 0x4321);
    cfw_theme_reflection_blend(0xabcd, 0xffff, 0x8000);
    assert(blend_background == rgb565);
}

static const unsigned char white[] = {255, 255, 255, 173},
                           light_gray[] = {0xee, 0xee, 0xee, 9},
                           pale_gray[] = {0xb0, 0xb0, 0xb0, 10},
                           below_gray[] = {0xaf, 0xaf, 0xaf, 11},
                           cover_border[] = {0x7e, 0x81, 0x83, 127},
                           accent_fill[] = {0x23, 0x6c, 0xc5, 91},
                           accent_text[] = {0x31, 0x85, 0xd0, 77},
                           black[] = {0, 0, 0, 123}, dim_text[] = {0x80, 0x80, 0x80, 5},
                           light_text[] = {0x81, 0x81, 0x81, 6};

/* A flat cover copies the artwork unscaled to (18, 38) of the color buffer and
 * skips the GLES cover and its edge lines; otherwise both native draws run. */
static void check_artwork(int flat)
{
    for (unsigned i = 0; i < 128 * 128; i++)
        album_art[i] = (uint16_t)(i + 1);
    memset(render_target, 0, sizeof(render_target));
    renderer_draws = edge_line_draws = 0;
    cfw_theme_album_art_view_draw(&surface);
    assert(renderer_draws == !flat && edge_line_draws == !flat);
    assert(render_target[38 * 320 + 18] == (flat ? 1 : 0));
    assert(render_target[165 * 320 + 145] == (flat ? 128 * 128 : 0));
    assert(!render_target[38 * 320 + 17] && !render_target[37 * 320 + 18] &&
           !render_target[166 * 320 + 145] && !render_target[165 * 320 + 146]);
}

/* Classic with Theme Default or Blue is exact in both appearances. */
static void check_classic_theme(void)
{
    check_artwork(0);
    for (unsigned accent = CFW_THEME_ACCENT_DEFAULT; accent <= CFW_THEME_ACCENT_BLUE;
         accent++) {
        assert(cfw_theme_accent_set(accent));
        for (int dark = 0; dark < 2; dark++) {
            cfw_theme_appearance_set(dark);
            for (unsigned c = 0; c < 256; c++) {
                const unsigned char native_gray[] = {c, c, c, 33};
                unsigned char fill_shade = dark && c >= 0xb0 ? 0x18 + (255 - c) / 4 : c;
                unsigned char text_shade = dark && c <= 0x80 ? 0xe8 - c / 2 : c;
                check_fill(native_gray, fill_shade, fill_shade, fill_shade);
                check_text(ordinary_view, native_gray, text_shade, text_shade,
                           text_shade);
            }
            check_text(ordinary_view, palette_white, 255, 255, 255);
            check_text(ordinary_view, white, 255, 255, 255);
            unsigned char cover = dark ? 0x38 : 0;
            if (dark)
                check_fill(cover_border, cover, cover, cover);
            else
                check_fill(cover_border, 0x7e, 0x81, 0x83);
            if (dark) {
                check_fill(accent_fill, 0, 161, 255);
                check_text(ordinary_view, accent_fill, 0, 161, 255);
                check_text(ordinary_view, accent_text, 0, 161, 255);
                check_background(0x18 * 0x10000 / 255, 0x18 * 0x10000 / 255,
                                 0x18 * 0x10000 / 255, 0x18c3);
            } else {
                check_fill(accent_fill, 0x23, 0x6c, 0xc5);
                check_text(ordinary_view, accent_fill, 0x23, 0x6c, 0xc5);
                check_text(ordinary_view, accent_text, 0x31, 0x85, 0xd0);
                check_background(0x10000, 0x10000, 0x10000, 0xffff);
            }
        }
    }
}

static void check_accent_presets(void)
{
    /* Purple recolors accents but leaves Classic light surfaces and backgrounds alone. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_PURPLE));
    cfw_theme_appearance_set(0);
    check_fill(white, 255, 255, 255);
    check_fill(light_gray, 0xee, 0xee, 0xee);
    check_fill(cover_border, 0x7e, 0x81, 0x83);
    check_text(ordinary_view, black, 0, 0, 0);
    check_text(ordinary_view, dim_text, 0x80, 0x80, 0x80);
    check_text(ordinary_view, palette_white, 255, 255, 255);
    check_fill(accent_fill, 137, 68, 171);
    check_text(ordinary_view, accent_fill, 137, 68, 171);
    check_text(ordinary_view, accent_text, 137, 68, 171);
    check_background(0x10000, 0x10000, 0x10000, 0xffff);
    cfw_theme_appearance_set(1);
    check_fill(white, 0x18, 0x18, 0x18);
    check_fill(accent_fill, 150, 78, 188);
    check_text(ordinary_view, accent_fill, 150, 78, 188);
    check_text(ordinary_view, accent_text, 218, 143, 255);
    check_background(0x18 * 0x10000 / 255, 0x18 * 0x10000 / 255, 0x18 * 0x10000 / 255,
                     0x18c3);

    /* Native-color views keep their colors under every accent. */
    uint32_t view[0xa4 / 4] = {0};
    view[0x4c / 4] = 0x0dad0ddc; /* Search_Input_Text */
    check_text(view, accent_text, 0x31, 0x85, 0xd0);
    check_text(view, black, 0, 0, 0);

    const unsigned char light[][3] = {
        {196, 30, 86}, {208, 32, 40}, {184, 72, 0}, {30, 128, 58}};
    const unsigned char dark[][3] = {
        {255, 100, 145}, {255, 105, 97}, {255, 159, 10}, {48, 209, 88}};
    for (unsigned i = 0; i < 4; i++) {
        assert(cfw_theme_accent_set(CFW_THEME_ACCENT_PINK + i));
        cfw_theme_appearance_set(0);
        check_text(ordinary_view, accent_text, light[i][0], light[i][1], light[i][2]);
        cfw_theme_appearance_set(1);
        check_text(ordinary_view, accent_text, dark[i][0], dark[i][1], dark[i][2]);
    }
    assert(!cfw_theme_accent_set(CFW_THEME_ACCENT_COUNT));
    assert(cfw_theme_accent_get() == CFW_THEME_ACCENT_GREEN);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
}

static void check_custom_theme(void)
{
    struct cfw_theme_style style = {{{0}}};
    const unsigned char sepia[][3] = {
        [CFW_THEME_BACKGROUND] = {250, 240, 220},
        [CFW_THEME_TEXT] = {60, 40, 20},
        [CFW_THEME_ACCENT_FILL] = {160, 80, 30},
        [CFW_THEME_ACCENT_TEXT] = {140, 70, 20},
        [CFW_THEME_ON_ACCENT] = {20, 10, 0},
        [CFW_THEME_COVER_BORDER] = {120, 100, 80},
    };
    for (unsigned role = 0; role < sizeof(sepia) / sizeof(sepia[0]); role++) {
        memcpy(style.colors[role], sepia[role], 3);
        style.colors[role][3] = 255;
    }
    struct cfw_theme_options options = {0};
    options.flags = CFW_THEME_FLAT_ARTWORK;
    const unsigned char orange[][3] = {
        [CFW_PALETTE_FILL] = {190, 76, 0},
        [CFW_PALETTE_TEXT] = {255, 159, 10},
    };
    for (unsigned slot = 0; slot < sizeof(orange) / sizeof(orange[0]); slot++)
        memcpy(options.accents[CFW_THEME_ACCENT_ORANGE - 1][slot], orange[slot], 3);
    cfw_theme_style_set(&style, &options);
    check_artwork(1);
    /* Custom styles theme light mode too, independent of the dark-mode switch. */
    for (int dark = 0; dark < 2; dark++) {
        cfw_theme_appearance_set(dark);
        check_fill(white, 250, 240, 220);
        check_fill(light_gray, 250 - 17, 240 - 17, 220 - 17);
        check_fill(pale_gray, 250 - 79, 240 - 79, 220 - 79);
        check_fill(below_gray, 0xaf, 0xaf, 0xaf);
        check_fill(cover_border, 120, 100, 80);
        check_fill(accent_fill, 160, 80, 30);
        check_text(ordinary_view, black, 60 - 32, 40 - 32, 0);
        check_text(ordinary_view, dim_text, 60 + 96, 40 + 96, 20 + 96);
        check_text(ordinary_view, light_text, 0x81, 0x81, 0x81);
        check_text(ordinary_view, accent_fill, 160, 80, 30);
        check_text(ordinary_view, accent_text, 140, 70, 20);
        /* Selected-row text follows on_accent by pointer; other white text takes
         * the text role on this light background. */
        check_text(ordinary_view, palette_white, 20, 10, 0);
        check_text(ordinary_view, white, 60, 40, 20);
        check_text(ordinary_view, light_gray, 0xee, 0xee, 0xee);
        check_background(250 * 0x10000 / 255, 240 * 0x10000 / 255, 220 * 0x10000 / 255,
                         (31u << 11) | (60u << 5) | 27u);
        /* Retained-light views stay unmapped. */
        uint32_t view[0xa4 / 4] = {0};
        view[0x4c / 4] = 0x0dad0607; /* Settings_DateTime_NumericDay */
        check_text(view, black, 0, 0, 0);
        check_text(view, palette_white, 255, 255, 255);
    }

    /* A dark custom theme inverts native shades and clamps role channels. */
    const unsigned char night[][3] = {
        [CFW_THEME_BACKGROUND] = {250, 10, 40},
        [CFW_THEME_TEXT] = {255, 250, 100},
    };
    for (unsigned role = 0; role < 2; role++)
        memcpy(style.colors[role], night[role], 3);
    cfw_theme_style_set(&style, &options);
    memset(&style, 0, sizeof(style)); /* The setter copied it. */
    cfw_theme_appearance_set(0);
    check_fill(white, 250, 10, 40);
    check_fill(pale_gray, 255, 10 + 19, 40 + 19);
    check_text(ordinary_view, black, 255, 250, 100);
    check_text(ordinary_view, dim_text, 255 - 64, 250 - 64, 100 - 64);
    check_text(ordinary_view, white, 255, 255, 255); /* dark background keeps white */
    check_background(250 * 0x10000 / 255, 10 * 0x10000 / 255, 40 * 0x10000 / 255,
                     (31u << 11) | (2u << 5) | 5u);

    /* Accents on a custom theme use its authored palette. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_ORANGE));
    check_fill(accent_fill, 190, 76, 0);
    check_text(ordinary_view, accent_text, 255, 159, 10);
    check_fill(white, 250, 10, 40);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    check_fill(accent_fill, 160, 80, 30);

    cfw_theme_style_reset();
    cfw_theme_appearance_set(0);
    check_artwork(0);
}

/* Bar labels: the highlighted one takes on_accent whenever anything is themed;
 * the others take the well color derived for the bar's well art; the Now Playing
 * bar and other colors pass through. */
static void check_search_key_hook(void)
{
    static const unsigned char native_white[] = {255, 255, 255},
                               sepia_on_accent[] = {20, 10, 0},
                               well_color[] = {7, 8, 9, 255};
    cfw_theme_appearance_set(0);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    well_letter = NULL;
    check_search_key(0, native_white);
    check_search_key(1, native_white);
    assert(well_styles_asked == 2); /* the keyboard uses the Black well */
    for (unsigned style = 0; style < 3; style++)
        for (int key = 0; key < 2; key++)
            check_option_bar(style, key, native_white);
    assert(well_styles_asked == 3);
    /* Classic with an accent keeps white on_accent; the well stays native. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_PURPLE));
    check_search_key(1, native_white);
    check_search_key(0, native_white);
    check_option_bar(1, 1, native_white);
    /* The Now Playing style keeps its native colors on the stock slices. */
    check_option_bar(2, 1, native_white);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));

    struct cfw_theme_style style = {{{0}}};
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
        style.colors[role][3] = 255;
    memcpy(style.colors[CFW_THEME_ON_ACCENT], sepia_on_accent, 3);
    struct cfw_theme_options options = {0};
    cfw_theme_style_set(&style, &options);
    well_letter = well_color;
    well_styles_asked = 0;
    check_search_key(1, sepia_on_accent);
    check_search_key(0, well_color);
    assert(well_styles_asked == 2);
    check_option_bar(0, 1, sepia_on_accent);
    check_option_bar(1, 1, sepia_on_accent);
    check_option_bar(0, 0, well_color);
    check_option_bar(1, 0, well_color);
    assert(well_styles_asked == 3);
    check_option_bar(2, 1, sepia_on_accent);
    check_option_bar(2, 0, well_color);
    assert(well_styles_asked == 7);
    /* A color that is neither letter slot is passed through untouched. */
    cfw_theme_search_key_color(&surface, white, ordinary_view);
    assert(!memcmp(drawn, white, 4));
    cfw_theme_style_reset();
    cfw_theme_appearance_set(0);
}

/* The Search input text keeps its native colors for Classic (covered with the
 * accent presets); under a custom style its dark shades take the color derived
 * from the field art, or the text role when none is derived, and the caret's
 * accent blue takes accent_text like any other text. */
static void check_search_input_hook(void)
{
    static const unsigned char on_field[] = {7, 8, 9};
    uint32_t view[0xa4 / 4] = {0};
    view[0x4c / 4] = 0x0dad0ddc; /* Search_Input_Text */
    struct cfw_theme_style style = {{{0}}};
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
        style.colors[role][3] = 255;
    const unsigned char text_role[] = {200, 201, 202}, accent_role[] = {9, 8, 7};
    memcpy(style.colors[CFW_THEME_TEXT], text_role, 3);
    memcpy(style.colors[CFW_THEME_ACCENT_TEXT], accent_role, 3);
    struct cfw_theme_options options = {0};
    cfw_theme_style_set(&style, &options);
    cfw_theme_appearance_set(1);
    field_text = on_field;
    field_asked = 0;
    check_text(view, black, 7, 8, 9);
    check_text(view, dim_text, 7, 8, 9);
    check_text(view, accent_text, 9, 8, 7);
    assert(field_asked == 3);
    field_text = NULL;
    check_text(view, black, 200, 201, 202);
    check_text(view, dim_text, 136, 137, 138);
    check_text(view, accent_text, 9, 8, 7);
    /* Other views never ask for the field color. */
    field_asked = 0;
    check_text(ordinary_view, black, 200, 201, 202);
    assert(field_asked == 0);
    cfw_theme_style_reset();
    cfw_theme_appearance_set(0);
    /* Classic light passes everything through. */
    field_asked = 0;
    check_text(view, black, 0, 0, 0);
    check_text(view, accent_text, 0x31, 0x85, 0xd0);
    assert(field_asked == 0);
}

/* Labels over listed art take the color derived from that art under a custom
 * style and keep their native color otherwise. */
static void check_contrast_text_hook(void)
{
    static const unsigned char native[] = {0xd3, 0xd3, 0xd3, 255}, on_art[] = {7, 8, 9};
    uint32_t view[0xa4 / 4] = {0};
    view[0x4c / 4] = 0x0dad020e; /* NowPlaying_StatusBar_Text_View */
    struct cfw_theme_style style = {{{0}}};
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
        style.colors[role][3] = 255;
    struct cfw_theme_options options = {0};
    cfw_theme_style_set(&style, &options);
    cfw_theme_appearance_set(1);
    art_text = on_art;
    art_asked = 0;
    check_text(view, native, 7, 8, 9);
    assert(art_asked == 1 && art_view == 0x0dad020e);
    /* It also wins over the white-text mapping (tumbler and picker digits). */
    check_text(view, white, 7, 8, 9);
    art_text = NULL;
    check_text(view, native, 0xd3, 0xd3, 0xd3);
    cfw_theme_style_reset();
    cfw_theme_appearance_set(0);
}

static void check_calendar(void (*hook)(void *, const unsigned char *),
                           const unsigned char *native, unsigned char red,
                           unsigned char green, unsigned char blue)
{
    hook(&surface, native);
    const unsigned char expected[] = {red, green, blue, native[3]};
    assert(!memcmp(drawn, expected, 4));
}

/* Calendar days ask for the color of their cell, weekday names for the page
 * color; the selected day takes on_accent; Classic keeps every native color. */
static void check_calendar_hooks(void)
{
    static const unsigned char inactive[] = {0x66, 0x66, 0x66, 255}, themed[] = {7, 8, 9};
    calendar_text = NULL;
    check_calendar(cfw_theme_calendar_color, black, 0, 0, 0);
    check_calendar(cfw_theme_calendar_color, white, 255, 255, 255);
    check_calendar(cfw_theme_calendar_header_color, inactive, 0x66, 0x66, 0x66);
    struct cfw_theme_style style = {{{0}}};
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
        style.colors[role][3] = 255;
    style.colors[CFW_THEME_ON_ACCENT][0] = 11;
    struct cfw_theme_options options = {0};
    cfw_theme_style_set(&style, &options);
    calendar_text = themed;
    check_calendar(cfw_theme_calendar_color, black, 7, 8, 9);
    assert(calendar_place == CFW_CALENDAR_ACTIVE);
    check_calendar(cfw_theme_calendar_color, inactive, 7, 8, 9);
    assert(calendar_place == CFW_CALENDAR_INACTIVE);
    check_calendar(cfw_theme_calendar_header_color, inactive, 7, 8, 9);
    assert(calendar_place == CFW_CALENDAR_HEADER);
    check_calendar(cfw_theme_calendar_color, white, 11, 0, 0);
    calendar_text = NULL;
    cfw_theme_style_reset();
}

static void check_theme(void)
{
    int was_active = cfw_dark_mode_enabled();
    check_classic_theme();
    check_accent_presets();
    check_custom_theme();
    check_search_key_hook();
    check_search_input_hook();
    check_contrast_text_hook();
    check_calendar_hooks();
    check_classic_theme();
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    cfw_theme_appearance_set(was_active);
}

int main(void)
{
    check_cached_colors();
    check_theme();
    uintptr_t value = 0;
    const uint32_t icons[] = {CFW_EQ_Icon, CFW_Info_Icon};
    for (unsigned i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP, icons[i], &value));
        assert(value == icons[i] && !stock_properties);
    }
    cfw_theme_appearance_set(0);
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
    assert(stock_actions == 1 && !toggles);

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
        cfw_theme_input_drained();
        int dark = cycle == 1;
        cfw_theme_appearance_set(dark);
        assert(cfw_dark_mode_enabled() == dark);
        assert(cfw_settings_property(&model, OSOS_RESOURCE_BMAP,
                                     CFW_DarkMode_PreviewImage, &value));
        assert(value == (dark ? CFW_DarkMode_Moon : IMG_SUN));
        assert(cfw_settings_property(&model, OSOS_PROPERTY_STRING,
                                     CFW_DarkMode_PreviewText, &value));
        assert(value == (dark ? CFW_DarkMode_DarkLabel : CFW_DarkMode_LightLabel));
        for (unsigned i = 0; i < sizeof(fills) / sizeof(fills[0]); i++) {
            cfw_theme_fill(&surface, &model, fills[i].light, 7, &controller);
            assert(
                !memcmp(drawn, dark ? fills[i].dark : fills[i].light, sizeof(drawn)));
        }
        for (unsigned i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
            cfw_theme_text_color(&surface, texts[i].light, ordinary_view);
            assert(
                !memcmp(drawn, dark ? texts[i].dark : texts[i].light, sizeof(drawn)));
        }
        cfw_theme_clear_color(0x10000, 0x10000, 0x10000, 0x4321);
        for (unsigned i = 0; i < 3; i++)
            assert(clear_rgb[i] == (dark ? 0x18 * 0x10000 / 255 : 0x10000));
        assert(clear_alpha == 0x4321);
        assert(cfw_theme_reflection_blend(0xabcd, 0xffff, 0x8000) == 0x1234);
        assert(blend_background == (dark ? 0x18c3u : 0xffffu));
        assert(cfw_dark_mode_enabled() == dark);
    }
    assert(cfw_dark_mode_action(&controller, "CFW_DarkMode_Toggle", 0));
    assert(toggles == 1);
    cfw_theme_style_reset();
    check_clicker();
    check_settings_lifecycle();
}
