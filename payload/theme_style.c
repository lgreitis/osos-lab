/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_style.h"

#define RGB(r, g, b) {r, g, b, 255}
static const struct cfw_theme_style classic[2] = {
    {{RGB(255, 255, 255), RGB(32, 32, 32), RGB(85, 85, 85), RGB(35, 108, 197),
      RGB(49, 133, 208), RGB(255, 255, 255), RGB(35, 108, 197), RGB(238, 238, 238),
      RGB(143, 149, 154), RGB(231, 231, 231), RGB(181, 181, 181), RGB(214, 214, 214),
      RGB(126, 129, 131)}},
    {{RGB(24, 24, 24), RGB(232, 232, 232), RGB(168, 168, 168), RGB(0, 161, 255),
      RGB(0, 161, 255), RGB(255, 255, 255), RGB(0, 161, 255), RGB(44, 44, 44),
      RGB(125, 140, 152), RGB(48, 48, 48), RGB(100, 100, 100), RGB(60, 60, 60),
      RGB(56, 56, 56)}}};

enum { ACCENT_FILL, ACCENT_TEXT, ACCENT_SLOTS };

/* Built-in Classic accents. Custom styles carry their own palettes.
 * Light colors keep >= 5:1 against white. Dark fills keep >= 4.5:1 under white text;
 * dark text keeps >= 6:1 on the background. Blue is Classic verbatim and below these
 * ratios for its light text and its dark fill (2.78:1 under white text). */
static const unsigned char accents[CFW_THEME_ACCENT_COUNT - 1][2][ACCENT_SLOTS][4] = {
    {{RGB(35, 108, 197), RGB(49, 133, 208)}, {RGB(0, 161, 255), RGB(0, 161, 255)}},
    {{RGB(137, 68, 171), RGB(137, 68, 171)}, {RGB(150, 78, 188), RGB(218, 143, 255)}},
    {{RGB(196, 30, 86), RGB(196, 30, 86)}, {RGB(206, 38, 96), RGB(255, 100, 145)}},
    {{RGB(208, 32, 40), RGB(208, 32, 40)}, {RGB(212, 40, 48), RGB(255, 105, 97)}},
    {{RGB(184, 72, 0), RGB(184, 72, 0)}, {RGB(190, 76, 0), RGB(255, 159, 10)}},
    {{RGB(30, 128, 58), RGB(30, 128, 58)}, {RGB(32, 134, 62), RGB(48, 209, 88)}},
    {{RGB(0, 115, 120), RGB(0, 115, 120)}, {RGB(0, 120, 125), RGB(64, 210, 215)}},
    {{RGB(130, 101, 0), RGB(130, 101, 0)}, {RGB(132, 103, 0), RGB(255, 214, 10)}},
    {{RGB(92, 96, 102), RGB(92, 96, 102)}, {RGB(100, 104, 110), RGB(190, 194, 200)}},
};
#undef RGB
static const char *const accent_labels[CFW_THEME_ACCENT_COUNT] = {
    "Theme Default", "Blue", "Purple", "Pink", "Red", "Orange", "Green", "Teal", "Yellow", "Graphite",
};
/* Palette slot + 1 for each role an accent overrides; 0 keeps the base role. */
static const unsigned char palette_slot[CFW_THEME_COLOR_COUNT] = {
    [CFW_THEME_ACCENT_FILL] = CFW_PALETTE_FILL + 1,
    [CFW_THEME_ACCENT_TEXT] = CFW_PALETTE_TEXT + 1,
    [CFW_THEME_SELECTION] = CFW_PALETTE_SELECTION + 1,
    [CFW_THEME_ON_ACCENT] = CFW_PALETTE_ON_ACCENT + 1,
};
static struct cfw_theme_style selected_style;
static struct cfw_theme_options selected_options;
static int has_custom_style;
static unsigned selected_accent;
static int selected_appearance = -1;

int cfw_theme_brightness(const unsigned char *color)
{
    return 54 * color[0] + 183 * color[1] + 19 * color[2];
}

int cfw_theme_color_dark(const unsigned char *color)
{
    return cfw_theme_brightness(color) < 128 * 256;
}

void cfw_theme_mix(unsigned char out[4], const unsigned char *from,
                   const unsigned char *to, unsigned weight)
{
    for (unsigned i = 0; i < 3; i++)
        out[i] = (from[i] * (255 - weight) + to[i] * weight + 127) / 255;
    out[3] = 255;
}

const unsigned char *cfw_theme_color(enum cfw_theme_role role, int dark)
{
    if ((unsigned)role >= CFW_THEME_COLOR_COUNT)
        role = CFW_THEME_TEXT;
    if (has_custom_style) {
        unsigned slot = palette_slot[role];
        return selected_accent && slot
                   ? selected_options.accents[selected_accent - 1][slot - 1]
                   : selected_style.colors[role];
    }
    if (selected_accent) {
        const unsigned char (*preset)[4] = accents[selected_accent - 1][!!dark];
        switch (role) {
        case CFW_THEME_ACCENT_FILL:
        case CFW_THEME_SELECTION:
            return preset[ACCENT_FILL];
        case CFW_THEME_ACCENT_TEXT:
            return preset[ACCENT_TEXT];
        default:
            break;
        }
    }
    return classic[!!dark].colors[role];
}

static void style_copy(void *out, const void *in, unsigned bytes)
{
    unsigned char *o = out;
    const unsigned char *i = in;
    while (bytes--)
        *o++ = *i++;
}

void cfw_theme_style_set(const struct cfw_theme_style *style,
                         const struct cfw_theme_options *options)
{
    style_copy(&selected_style, style, sizeof(selected_style));
    style_copy(&selected_options, options, sizeof(selected_options));
    has_custom_style = 1;
}

void cfw_theme_style_reset(void)
{
    has_custom_style = 0;
    selected_appearance = -1;
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

void cfw_theme_appearance_set(int dark)
{
    selected_appearance = !!dark;
}

int cfw_theme_appearance_get(int fallback)
{
    return selected_appearance < 0 ? fallback : selected_appearance;
}

unsigned cfw_theme_render_flags(void)
{
    return has_custom_style ? selected_options.flags : 0;
}

const unsigned char *cfw_theme_icon_color(int dark)
{
    if (has_custom_style)
        return selected_accent ? selected_options.accents[selected_accent - 1][CFW_PALETTE_ICON]
                               : selected_options.icon;
    return cfw_theme_color(CFW_THEME_ACCENT_TEXT, dark);
}

/* Native gradient stops are lightened toward white by these /255 weights. */
static const unsigned char selection_lift[3] = {64, 0, 140};

int cfw_theme_selection_bar_colors(unsigned char out[3][4])
{
    int flat = cfw_theme_render_flags() & CFW_THEME_FLAT_SELECTION;
    if (!flat && !has_custom_style && selected_accent <= CFW_THEME_ACCENT_BLUE)
        return 0;
    static const unsigned char white[3] = {255, 255, 255};
    const unsigned char *base =
        cfw_theme_color(CFW_THEME_SELECTION, cfw_theme_appearance_get(0));
    for (unsigned stop = 0; stop < 3; stop++)
        cfw_theme_mix(out[stop], base, white, flat ? 0 : selection_lift[stop]);
    return 1;
}
