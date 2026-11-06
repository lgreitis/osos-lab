/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Upper bound on catalog assets; the generated catalog asserts it. */
#define CFW_THEME_MAX_ASSETS 640u
#define CFW_THEME_BITMAP_WORDS (CFW_THEME_MAX_ASSETS / 32)

/* On-disk RTH4 role order. Append only with a new package version. */
enum cfw_theme_role {
    CFW_THEME_BACKGROUND,
    CFW_THEME_TEXT,
    CFW_THEME_ACCENT_FILL,
    CFW_THEME_ACCENT_TEXT,
    CFW_THEME_ON_ACCENT,
    CFW_THEME_SELECTION,
    CFW_THEME_COVER_BORDER,
    CFW_THEME_COLOR_COUNT
};

/* Named accents override fills, text, selection and matching foreground roles. Blue is Classic. */
enum cfw_theme_accent {
    CFW_THEME_ACCENT_DEFAULT,
    CFW_THEME_ACCENT_BLUE,
    CFW_THEME_ACCENT_PURPLE,
    CFW_THEME_ACCENT_PINK,
    CFW_THEME_ACCENT_RED,
    CFW_THEME_ACCENT_ORANGE,
    CFW_THEME_ACCENT_GREEN,
    CFW_THEME_ACCENT_TEAL,
    CFW_THEME_ACCENT_YELLOW,
    CFW_THEME_ACCENT_GRAPHITE,
    CFW_THEME_ACCENT_COUNT
};

struct cfw_theme_style {
    unsigned char colors[CFW_THEME_COLOR_COUNT][4];
};

/* The dark argument is ignored while a custom style is selected. */
const unsigned char *cfw_theme_color(enum cfw_theme_role role, int dark);
/* Restores Classic colors and the default appearance; retains the accent. */
void cfw_theme_style_reset(void);
int cfw_theme_style_custom(void);
/* Returns 0 without changes for an unknown accent. */
int cfw_theme_accent_set(unsigned accent);
unsigned cfw_theme_accent_get(void);
/* Returns NULL for an unknown accent. */
const char *cfw_theme_accent_label(unsigned accent);
/* Whether a color reads as a dark surface, by weighted RGB brightness. */
int cfw_theme_color_dark(const unsigned char *color);
/* Weighted RGB brightness scaled by 256; cfw_theme_color_dark compares it to 128 * 256. */
int cfw_theme_brightness(const unsigned char *color);
/* Moves each RGB channel of from toward to by weight/255 (0-255), rounding to nearest;
 * out is opaque. */
void cfw_theme_mix(unsigned char out[4], const unsigned char *from,
                   const unsigned char *to, unsigned weight);

/* The switcher supplies appearance; reset uses the caller-provided fallback. */
void cfw_theme_appearance_set(int dark);
int cfw_theme_appearance_get(int fallback);

/* RTH4 palette slot order is independent of the RTH4 base role order. */
enum cfw_theme_palette_role {
    CFW_PALETTE_FILL,
    CFW_PALETTE_TEXT,
    CFW_PALETTE_SELECTION,
    CFW_PALETTE_ON_ACCENT,
    CFW_PALETTE_ICON,
    CFW_PALETTE_COUNT
};

enum { CFW_THEME_FLAT_SELECTION = 1 };

/* Tint role byte of a catalog asset: untinted, the icon tint, or a base role
 * offset by CFW_TINT_ROLE_BASE (accent palettes override roles as usual). */
enum cfw_theme_tint {
    CFW_TINT_NONE,
    CFW_TINT_ICON,
    CFW_TINT_ROLE_BASE,
    CFW_TINT_COUNT = CFW_TINT_ROLE_BASE + CFW_THEME_COLOR_COUNT
};

/* RTH4 options that follow the base colors: render flags, the Theme Default icon
 * tint, one palette per named accent and one tint role byte per catalog asset
 * (masks are colored at draw time, paletted bitmaps by rewriting their palette and
 * ARGB bitmaps by rewriting their pixels). */
struct cfw_theme_options {
    uint32_t flags;
    unsigned char icon[4];
    unsigned char accents[CFW_THEME_ACCENT_COUNT - 1][CFW_PALETTE_COUNT][4];
    unsigned char tint[CFW_THEME_MAX_ASSETS];
};

/* Copies both; the caller's storage need not outlive the call. */
void cfw_theme_style_set(const struct cfw_theme_style *style,
                         const struct cfw_theme_options *options);
unsigned cfw_theme_render_flags(void);
const unsigned char *cfw_theme_icon_color(int dark);
/* Whether a catalog asset is tinted; always 0 for Classic. */
int cfw_theme_tinted(unsigned index);
/* The color a tinted catalog asset follows: the icon tint or its role. Untinted
 * assets return the icon tint. */
const unsigned char *cfw_theme_tint_color(unsigned index, int dark);

/* Selection bar colors: gradient start, gradient end, top highlight line.
 * Returns 0 when the native Classic bar should be drawn unchanged. */
int cfw_theme_selection_bar_colors(unsigned char out[3][4]);
