/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_style.h"

#define RGB(r, g, b) {r, g, b, 255}
static const struct cfw_theme_style classic[2] = {
    {{RGB(255, 255, 255), RGB(32, 32, 32), RGB(85, 85, 85), RGB(35, 108, 197),
      RGB(49, 133, 208), RGB(255, 255, 255), RGB(35, 108, 197), RGB(238, 238, 238),
      RGB(143, 149, 154), RGB(231, 231, 231), RGB(181, 181, 181), RGB(214, 214, 214),
      RGB(126, 129, 131), RGB(20, 121, 213), RGB(237, 245, 252), RGB(255, 255, 255)}},
    {{RGB(24, 24, 24), RGB(232, 232, 232), RGB(168, 168, 168), RGB(0, 161, 255),
      RGB(0, 161, 255), RGB(255, 255, 255), RGB(0, 161, 255), RGB(44, 44, 44),
      RGB(125, 140, 152), RGB(48, 48, 48), RGB(100, 100, 100), RGB(60, 60, 60),
      RGB(56, 56, 56), RGB(0, 161, 255), RGB(26, 46, 63), RGB(16, 16, 16)}}};

enum { ACCENT_FILL, ACCENT_TEXT, ACCENT_EQ, ACCENT_EQ_SELECTION, ACCENT_SLOTS };

/* EQ selections tint the accent over Classic backgrounds by these /255 weights. */
static const unsigned char selection_weight[2] = {20, 43};
/* Light colors keep >= 5:1 against white. Dark fills keep >= 4.5:1 under white text;
 * dark text and EQ accents keep >= 6:1 on the background and under EQ on-accent text.
 * Blue is Classic verbatim, including its lighter light-mode text. */
static const unsigned char accents[CFW_THEME_ACCENT_COUNT - 1][2][ACCENT_SLOTS][4] = {
    {{RGB(35, 108, 197), RGB(49, 133, 208), RGB(20, 121, 213), RGB(237, 245, 252)},
     {RGB(0, 161, 255), RGB(0, 161, 255), RGB(0, 161, 255), RGB(26, 46, 63)}},
    {{RGB(137, 68, 171), RGB(137, 68, 171), RGB(137, 68, 171), RGB(246, 240, 248)},
     {RGB(150, 78, 188), RGB(218, 143, 255), RGB(218, 143, 255), RGB(57, 44, 63)}},
    {{RGB(196, 30, 86), RGB(196, 30, 86), RGB(196, 30, 86), RGB(250, 237, 242)},
     {RGB(206, 38, 96), RGB(255, 100, 145), RGB(255, 100, 145), RGB(63, 37, 44)}},
    {{RGB(208, 32, 40), RGB(208, 32, 40), RGB(208, 32, 40), RGB(251, 238, 238)},
     {RGB(212, 40, 48), RGB(255, 105, 97), RGB(255, 105, 97), RGB(63, 38, 36)}},
    {{RGB(184, 72, 0), RGB(184, 72, 0), RGB(184, 72, 0), RGB(249, 241, 235)},
     {RGB(190, 76, 0), RGB(255, 159, 10), RGB(255, 159, 10), RGB(63, 47, 22)}},
    {{RGB(30, 128, 58), RGB(30, 128, 58), RGB(30, 128, 58), RGB(237, 245, 240)},
     {RGB(32, 134, 62), RGB(48, 209, 88), RGB(48, 209, 88), RGB(28, 55, 35)}},
};
#undef RGB
static const char *const accent_labels[CFW_THEME_ACCENT_COUNT] = {
    "Theme Default", "Blue", "Purple", "Pink", "Red", "Orange", "Green",
};
static struct cfw_theme_style selected_style;
static unsigned char eq_selection_tint[4];
static int has_custom_style, custom_background_dark;
static unsigned selected_accent;

int cfw_theme_color_dark(const unsigned char *color)
{
    return 54 * color[0] + 183 * color[1] + 19 * color[2] < 128 * 256;
}

/* Custom backgrounds are arbitrary, so their EQ selection is tinted at runtime. */
static void refresh_eq_selection_tint(void)
{
    if (!has_custom_style || !selected_accent)
        return;
    const unsigned char *background = selected_style.colors[CFW_THEME_BACKGROUND];
    const unsigned char *color =
        accents[selected_accent - 1][custom_background_dark][ACCENT_EQ];
    int weight = selection_weight[custom_background_dark];
    for (unsigned i = 0; i < 3; i++) {
        int delta = (color[i] - background[i]) * weight;
        eq_selection_tint[i] =
            background[i] + (delta >= 0 ? (delta + 127) / 255 : -((127 - delta) / 255));
    }
    eq_selection_tint[3] = 255;
}

const unsigned char *cfw_theme_color(enum cfw_theme_role role, int dark)
{
    if ((unsigned)role >= CFW_THEME_COLOR_COUNT)
        role = CFW_THEME_TEXT;
    if (selected_accent) {
        const unsigned char (*preset)[4] =
            accents[selected_accent - 1]
                   [has_custom_style ? custom_background_dark : !!dark];
        switch (role) {
        case CFW_THEME_ON_ACCENT:
        case CFW_THEME_EQ_ON_ACCENT:
            return classic[has_custom_style ? custom_background_dark : !!dark]
                .colors[role];
        case CFW_THEME_ACCENT_FILL:
        case CFW_THEME_SELECTION:
            return preset[ACCENT_FILL];
        case CFW_THEME_ACCENT_TEXT:
            return preset[ACCENT_TEXT];
        case CFW_THEME_EQ_ACCENT:
            return preset[ACCENT_EQ];
        case CFW_THEME_EQ_SELECTION:
            return has_custom_style ? eq_selection_tint : preset[ACCENT_EQ_SELECTION];
        default:
            break;
        }
    }
    return (has_custom_style ? &selected_style : &classic[!!dark])->colors[role];
}

void cfw_theme_style_set(const struct cfw_theme_style *style)
{
    for (unsigned i = 0; i < CFW_THEME_COLOR_COUNT; i++)
        for (unsigned j = 0; j < 4; j++)
            selected_style.colors[i][j] = style->colors[i][j];
    has_custom_style = 1;
    custom_background_dark =
        cfw_theme_color_dark(selected_style.colors[CFW_THEME_BACKGROUND]);
    refresh_eq_selection_tint();
}

void cfw_theme_style_reset(void)
{
    has_custom_style = 0;
}

int cfw_theme_style_custom(void)
{
    return has_custom_style;
}

int cfw_theme_accent_set(unsigned value)
{
    if (value >= CFW_THEME_ACCENT_COUNT)
        return 0;
    selected_accent = value;
    refresh_eq_selection_tint();
    return 1;
}

unsigned cfw_theme_accent_get(void)
{
    return selected_accent;
}

const char *cfw_theme_accent_label(unsigned value)
{
    return value < CFW_THEME_ACCENT_COUNT ? accent_labels[value] : 0;
}
