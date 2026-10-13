/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <string.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static int dark;
static uint32_t accent;
static unsigned char drawn[4];
static int clear_rgb, clear_alpha;
static uint32_t blend_background;

int cfw_assets_enabled(void)
{
    return dark;
}

uint32_t cfw_assets_accent(void)
{
    return accent;
}

static void fill(void *target, const void *rect, const unsigned char *color,
                 uint32_t mode, void *clip)
{
    (void)target;
    (void)rect;
    (void)mode;
    (void)clip;
    memcpy(drawn, color, 4);
}

static void text_color(void *context, const unsigned char *color)
{
    (void)context;
    memcpy(drawn, color, 4);
}

static void clear_color(int red, int green, int blue, int alpha)
{
    assert(red == green && green == blue);
    clear_rgb = red;
    clear_alpha = alpha;
}

static uint32_t blend(uint32_t pixel, uint32_t background, int weight)
{
    assert(pixel == 0xabcd && weight == 0x8000);
    blend_background = background;
    return 0x1234;
}

static void string_init(struct osos_string *string, const char *name)
{
    (void)string;
    assert(!strcmp(name, "NowPlaying_Idle_Background"));
}

static uint32_t *name_slot(struct osos_string *string)
{
    (void)string;
    static uint32_t slot = 100;
    return &slot;
}

static void string_destroy(struct osos_string *string)
{
    (void)string;
}

#define osos_surface_fill_rect fill
#define osos_graphics_set_color text_color
#define osos_gles_clear_color clear_color
#define osos_rgb565_blend blend
#define osos_string_init string_init
#define osos_resource_name_slot name_slot
#define osos_string_destroy string_destroy
#include "../../../payload/theme_render.c"

int main(void)
{
    const unsigned char fills[][4] = {
        {255, 255, 255, 173}, {0x7e, 0x81, 0x83, 127}, {35, 108, 196, 91}};
    for (dark = 0; dark <= 1; dark++) {
        for (unsigned i = 0; i < 3; i++) {
            cfw_theme_fill(NULL, NULL, fills[i], 7, NULL);
            if (dark && i < 2) {
                unsigned char shade = i ? 0x38 : 0x18;
                assert(drawn[0] == shade && drawn[1] == shade && drawn[2] == shade);
                assert(drawn[3] == fills[i][3]);
            } else {
                assert(!memcmp(drawn, fills[i], 4));
            }
        }

        const unsigned char black[] = {0, 0, 0, 123};
        cfw_theme_text_color(NULL, black);
        assert(drawn[0] == (dark ? 0xe8 : 0) && drawn[3] == 123);

        cfw_theme_clear_color(0x10000, 0x10000, 0x10000, 7);
        assert(clear_rgb == (dark ? 0x18 * 0x10000 / 255 : 0x10000));
        assert(clear_alpha == 7);

        assert(cfw_theme_reflection_blend(0xabcd, 0xffff, 0x8000) == 0x1234);
        assert(blend_background == (dark ? 0x18c3u : 0xffffu));
    }

    dark = 1;
    uint32_t white = 0xffffff, blue = 0x236cc4;
    assert(*(const uint32_t *)cfw_theme_color(100, &white) == 0xfffffe);
    assert(cfw_theme_color(101, &white) == &white);

    int rect[] = {0, 0, 20, 100};
    assert(!cfw_theme_selection_changed(rect, rect));

    const uint32_t accents[] = {0xb57be8, 0xe3b944, 0x42c8bb, 0xeb686d};
    for (unsigned i = 0; i < 4; i++) {
        accent = accents[i];
        assert(cfw_theme_selection_changed(rect, rect));
        assert(!cfw_theme_selection_changed(rect, rect));

        cfw_theme_text_color(NULL, fills[2]);
        assert(drawn[0] == accent >> 16 && drawn[1] == ((accent >> 8) & 255) &&
               drawn[2] == (accent & 255) && drawn[3] == 91);
        assert(*(const uint32_t *)cfw_theme_color(0x0dad00cf, &blue) == accent);

        cfw_theme_selection_color(NULL, fills[2]);
        assert(memcmp(drawn, fills[2], 3) && drawn[3] == 91);
        cfw_theme_selection_color(NULL, fills[0]);
        assert(!memcmp(drawn, fills[0], 4));

        const unsigned char ramp[][4] = {{150, 197, 218, 255},
                                         {9, 164, 225, 255},
                                         {0, 99, 211, 255},
                                         {0, 67, 134, 255}};
        unsigned previous = 100000;
        for (unsigned j = 0; j < 4; j++) {
            cfw_theme_selection_color(NULL, ramp[j]);
            unsigned brightness = 77 * drawn[0] + 150 * drawn[1] + 29 * drawn[2];
            assert(brightness + 2000 < previous);
            assert(drawn[3] == 255);
            assert(drawn[0] != drawn[1] || drawn[1] != drawn[2]);
            previous = brightness;
        }
    }

    accent = 0;
    dark = 0;
    assert(cfw_theme_selection_changed(rect, rect));
    cfw_theme_selection_color(NULL, fills[2]);
    assert(!memcmp(drawn, fills[2], 4));
    assert(cfw_theme_color(100, &white) == &white);
}
