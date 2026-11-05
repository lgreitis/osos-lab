/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "../../../payload/theme_style.c"

#define RGB(r, g, b) {r, g, b, 255}
/* Classic values predate the shared style and must never drift. */
static const unsigned char expected_classic[2][CFW_THEME_COLOR_COUNT][4] = {
    {RGB(255, 255, 255), RGB(32, 32, 32), RGB(85, 85, 85), RGB(35, 108, 197),
     RGB(49, 133, 208), RGB(255, 255, 255), RGB(35, 108, 197), RGB(238, 238, 238),
     RGB(143, 149, 154), RGB(231, 231, 231), RGB(181, 181, 181), RGB(214, 214, 214),
     RGB(126, 129, 131), RGB(20, 121, 213), RGB(237, 245, 252), RGB(255, 255, 255)},
    {RGB(24, 24, 24), RGB(232, 232, 232), RGB(168, 168, 168), RGB(0, 161, 255),
     RGB(0, 161, 255), RGB(255, 255, 255), RGB(0, 161, 255), RGB(44, 44, 44),
     RGB(125, 140, 152), RGB(48, 48, 48), RGB(100, 100, 100), RGB(60, 60, 60),
     RGB(56, 56, 56), RGB(0, 161, 255), RGB(26, 46, 63), RGB(16, 16, 16)},
};
#undef RGB

static const enum cfw_theme_role accent_roles[] = {
    CFW_THEME_ACCENT_FILL, CFW_THEME_ACCENT_TEXT,  CFW_THEME_SELECTION,
    CFW_THEME_EQ_ACCENT,   CFW_THEME_EQ_SELECTION,
};

static int accent_role(enum cfw_theme_role role)
{
    for (unsigned i = 0; i < sizeof(accent_roles) / sizeof(accent_roles[0]); i++)
        if (accent_roles[i] == role)
            return 1;
    return 0;
}

static void check_color(const unsigned char *color, unsigned char red,
                        unsigned char green, unsigned char blue)
{
    const unsigned char expected[] = {red, green, blue, 255};
    assert(!memcmp(color, expected, 4));
}

static void check_classic(void)
{
    for (int dark = 0; dark < 2; dark++) {
        for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
            assert(
                !memcmp(cfw_theme_color(role, dark), expected_classic[dark][role], 4));
        assert(cfw_theme_color(CFW_THEME_COLOR_COUNT, dark) ==
               cfw_theme_color(CFW_THEME_TEXT, dark));
        assert(cfw_theme_color((enum cfw_theme_role) - 1, dark) ==
               cfw_theme_color(CFW_THEME_TEXT, dark));
    }
    assert(cfw_theme_color(CFW_THEME_BACKGROUND, 7) ==
           cfw_theme_color(CFW_THEME_BACKGROUND, 1));
}

static struct cfw_theme_style classic_copy(int dark)
{
    struct cfw_theme_style style;
    memcpy(style.colors, expected_classic[dark], sizeof(style.colors));
    return style;
}

int main(void)
{
    /* Defaults. */
    assert(!cfw_theme_style_custom() &&
           cfw_theme_accent_get() == CFW_THEME_ACCENT_DEFAULT);
    check_classic();
    assert(CFW_THEME_ACCENT_COUNT == 7);
    const char *labels[] = {"Theme Default", "Blue",   "Purple", "Pink",
                            "Red",           "Orange", "Green"};
    for (unsigned i = 0; i < CFW_THEME_ACCENT_COUNT; i++)
        assert(!strcmp(cfw_theme_accent_label(i), labels[i]));
    assert(!cfw_theme_accent_label(CFW_THEME_ACCENT_COUNT));
    assert(!cfw_theme_accent_label(UINT_MAX));

    /* Invalid presets are rejected without changing the selection. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_RED));
    assert(!cfw_theme_accent_set(CFW_THEME_ACCENT_COUNT));
    assert(!cfw_theme_accent_set(UINT_MAX));
    assert(cfw_theme_accent_get() == CFW_THEME_ACCENT_RED);

    /* Blue is Classic exactly, including the distinct light text and EQ accents. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_BLUE));
    check_classic();
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 0), 35, 108, 197);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_TEXT, 0), 49, 133, 208);
    check_color(cfw_theme_color(CFW_THEME_EQ_ACCENT, 0), 20, 121, 213);

    /* Presets override accent roles only. Classic EQ selections are tinted backgrounds. */
    for (unsigned accent = CFW_THEME_ACCENT_PURPLE; accent < CFW_THEME_ACCENT_COUNT;
         accent++) {
        assert(cfw_theme_accent_set(accent) && cfw_theme_accent_get() == accent);
        for (int dark = 0; dark < 2; dark++) {
            for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++) {
                int same = !memcmp(cfw_theme_color(role, dark),
                                   expected_classic[dark][role], 4);
                assert(same == !accent_role(role));
            }
            assert(cfw_theme_color(CFW_THEME_SELECTION, dark) ==
                   cfw_theme_color(CFW_THEME_ACCENT_FILL, dark));
            /* A custom copy of Classic yields the same tint at runtime. */
            unsigned char table[4];
            memcpy(table, cfw_theme_color(CFW_THEME_EQ_SELECTION, dark), 4);
            struct cfw_theme_style copy = classic_copy(dark);
            cfw_theme_style_set(&copy);
            assert(!memcmp(cfw_theme_color(CFW_THEME_EQ_SELECTION, !dark), table, 4));
            cfw_theme_style_reset();
        }
    }
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 0), 30, 128, 58);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 1), 32, 134, 62);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_TEXT, 1), 48, 209, 88);

    /* Custom styles are copied, ignore the appearance and keep Theme Default verbatim. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    struct cfw_theme_style style;
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++) {
        style.colors[role][0] = 250 - role;
        style.colors[role][1] = 240 - role;
        style.colors[role][2] = 220 - role;
        style.colors[role][3] = 200 + role;
    }
    cfw_theme_style_set(&style);
    struct cfw_theme_style original = style;
    memset(&style, 0, sizeof(style));
    assert(cfw_theme_style_custom());
    for (int dark = 0; dark < 2; dark++)
        for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
            assert(!memcmp(cfw_theme_color(role, dark), original.colors[role], 4));

    /* A light custom background selects light accents and tints its own selection. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_GREEN));
    for (int dark = 0; dark < 2; dark++) {
        check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, dark), 30, 128, 58);
        check_color(cfw_theme_color(CFW_THEME_EQ_SELECTION, dark), 233, 231, 207);
        check_color(cfw_theme_color(CFW_THEME_ON_ACCENT, dark), 255, 255, 255);
        check_color(cfw_theme_color(CFW_THEME_EQ_ON_ACCENT, dark), 255, 255, 255);
    }

    /* A dark custom background selects dark accents; accent changes retint. */
    original.colors[CFW_THEME_BACKGROUND][0] = 10;
    original.colors[CFW_THEME_BACKGROUND][1] = 20;
    original.colors[CFW_THEME_BACKGROUND][2] = 40;
    cfw_theme_style_set(&original);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_TEXT, 0), 48, 209, 88);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_PURPLE));
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 0), 150, 78, 188);
    check_color(cfw_theme_color(CFW_THEME_EQ_ACCENT, 0), 218, 143, 255);
    check_color(cfw_theme_color(CFW_THEME_EQ_SELECTION, 0), 45, 41, 76);

    /* Every preset supplies foregrounds even if the theme uses opposite colors.
     * Theme Default restores the author's original values in both appearances. */
    for (int dark = 0; dark < 2; dark++) {
        struct cfw_theme_style contrast = classic_copy(dark);
        memset(contrast.colors[CFW_THEME_ON_ACCENT], 0, 3);
        memset(contrast.colors[CFW_THEME_EQ_ON_ACCENT], dark ? 255 : 0, 3);
        cfw_theme_style_set(&contrast);
        for (unsigned preset = CFW_THEME_ACCENT_BLUE; preset < CFW_THEME_ACCENT_COUNT;
             preset++) {
            assert(cfw_theme_accent_set(preset));
            check_color(cfw_theme_color(CFW_THEME_ON_ACCENT, !dark), 255, 255, 255);
            unsigned char foreground = dark ? 16 : 255;
            check_color(cfw_theme_color(CFW_THEME_EQ_ON_ACCENT, !dark), foreground,
                        foreground, foreground);
        }
        assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
        assert(!memcmp(cfw_theme_color(CFW_THEME_ON_ACCENT, !dark),
                       contrast.colors[CFW_THEME_ON_ACCENT], 4));
        assert(!memcmp(cfw_theme_color(CFW_THEME_EQ_ON_ACCENT, !dark),
                       contrast.colors[CFW_THEME_EQ_ON_ACCENT], 4));
    }
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_PURPLE));

    /* Reset restores Classic and retains the separately chosen accent. */
    cfw_theme_style_reset();
    assert(!cfw_theme_style_custom() &&
           cfw_theme_accent_get() == CFW_THEME_ACCENT_PURPLE);
    check_color(cfw_theme_color(CFW_THEME_EQ_SELECTION, 1), 57, 44, 63);
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    check_classic();
    return 0;
}
