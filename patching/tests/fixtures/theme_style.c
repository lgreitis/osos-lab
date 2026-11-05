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

int main(void)
{
    /* Defaults. */
    assert(!cfw_theme_style_custom() &&
           cfw_theme_accent_get() == CFW_THEME_ACCENT_DEFAULT);
    check_classic();
    assert(CFW_THEME_ACCENT_COUNT == 10);
    const char *labels[] = {"Theme Default", "Blue",   "Purple", "Pink",
                            "Red",           "Orange", "Green", "Teal", "Yellow", "Graphite"};
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
        }
    }
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_GREEN));
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 0), 30, 128, 58);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_FILL, 1), 32, 134, 62);
    check_color(cfw_theme_color(CFW_THEME_ACCENT_TEXT, 1), 48, 209, 88);

    /* Custom styles are copied with their palettes and ignore the appearance.
     * Theme Default keeps the author's colors; an accent replaces only palette roles. */
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT));
    struct cfw_theme_style style;
    for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++) {
        style.colors[role][0] = 250 - role;
        style.colors[role][1] = 240 - role;
        style.colors[role][2] = 220 - role;
        style.colors[role][3] = 200 + role;
    }
    struct cfw_theme_options options = {0};
    options.flags = CFW_THEME_FLAT_SELECTION | CFW_THEME_TINT_PREVIEWS;
    for (unsigned a = 0; a < CFW_THEME_ACCENT_COUNT - 1; a++)
        for (unsigned slot = 0; slot < CFW_PALETTE_COUNT; slot++) {
            options.accents[a][slot][0] = a;
            options.accents[a][slot][1] = slot;
            options.accents[a][slot][3] = 255;
        }
    options.icon[0] = 42;
    options.icon[3] = 255;
    cfw_theme_style_set(&style, &options);
    struct cfw_theme_style original = style;
    memset(&style, 0, sizeof(style));
    memset(&options, 0, sizeof(options));
    assert(cfw_theme_style_custom());
    assert(cfw_theme_render_flags() == 3);
    assert(cfw_theme_icon_color(0)[0] == 42);
    for (int dark = 0; dark < 2; dark++)
        for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
            assert(!memcmp(cfw_theme_color(role, dark), original.colors[role], 4));
    static const struct {
        enum cfw_theme_role role;
        enum cfw_theme_palette_role slot;
    } overrides[] = {
        {CFW_THEME_ACCENT_FILL, CFW_PALETTE_FILL},
        {CFW_THEME_ACCENT_TEXT, CFW_PALETTE_TEXT},
        {CFW_THEME_SELECTION, CFW_PALETTE_SELECTION},
        {CFW_THEME_ON_ACCENT, CFW_PALETTE_ON_ACCENT},
        {CFW_THEME_EQ_ACCENT, CFW_PALETTE_EQ},
        {CFW_THEME_EQ_SELECTION, CFW_PALETTE_EQ_SELECTION},
        {CFW_THEME_EQ_ON_ACCENT, CFW_PALETTE_EQ_ON_ACCENT},
    };
    for (unsigned accent = CFW_THEME_ACCENT_BLUE; accent < CFW_THEME_ACCENT_COUNT;
         accent++) {
        assert(cfw_theme_accent_set(accent));
        unsigned overridden = 0;
        for (unsigned i = 0; i < sizeof(overrides) / sizeof(*overrides); i++)
            for (int dark = 0; dark < 2; dark++) {
                check_color(cfw_theme_color(overrides[i].role, dark), accent - 1,
                            overrides[i].slot, 0);
                overridden |= 1u << overrides[i].role;
            }
        for (unsigned role = 0; role < CFW_THEME_COLOR_COUNT; role++)
            if (!(overridden & (1u << role)))
                assert(!memcmp(cfw_theme_color(role, 1), original.colors[role], 4));
        check_color(cfw_theme_icon_color(0), accent - 1, CFW_PALETTE_ICON, 0);
    }
    assert(cfw_theme_accent_set(CFW_THEME_ACCENT_TEAL));
    unsigned char bar[3][4];
    assert(cfw_theme_selection_bar_colors(bar));
    for (unsigned stop = 0; stop < 3; stop++)
        assert(!memcmp(bar[stop], cfw_theme_color(CFW_THEME_SELECTION, 0), 4));

    /* Reset restores Classic and retains the separately chosen accent. */
    cfw_theme_style_reset();
    assert(!cfw_theme_style_custom() && !cfw_theme_render_flags() &&
           cfw_theme_accent_get() == CFW_THEME_ACCENT_TEAL);
    check_color(cfw_theme_color(CFW_THEME_EQ_SELECTION, 1), 31, 55, 56);
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
    check_classic();
    assert(!cfw_theme_selection_bar_colors(bar));
    cfw_theme_accent_set(CFW_THEME_ACCENT_BLUE);
    assert(!cfw_theme_selection_bar_colors(bar));
    for (int dark = 0; dark < 2; dark++) {
        cfw_theme_appearance_set(dark);
        cfw_theme_accent_set(CFW_THEME_ACCENT_PINK);
        assert(cfw_theme_selection_bar_colors(bar));
        const unsigned char *fill = cfw_theme_color(CFW_THEME_SELECTION, dark);
        assert(!memcmp(bar[1], fill, 4));
        for (unsigned i = 0; i < 3; i++)
            assert(bar[0][i] >= fill[i] && bar[2][i] >= bar[0][i]);
        assert(bar[0][3] == 255 && bar[2][3] == 255);
    }
    cfw_theme_style_reset();
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
    return 0;
}
