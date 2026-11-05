/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* On-disk RTH4 role order. Append only with a new package version. */
enum cfw_theme_role {
    CFW_THEME_BACKGROUND,
    CFW_THEME_TEXT,
    CFW_THEME_MUTED,
    CFW_THEME_ACCENT_FILL,
    CFW_THEME_ACCENT_TEXT,
    CFW_THEME_ON_ACCENT,
    CFW_THEME_SELECTION,
    CFW_THEME_TRACK,
    CFW_THEME_INACTIVE,
    CFW_THEME_GRID,
    CFW_THEME_ZERO,
    CFW_THEME_BORDER,
    CFW_THEME_COVER_BORDER,
    CFW_THEME_EQ_ACCENT,
    CFW_THEME_EQ_SELECTION,
    CFW_THEME_EQ_ON_ACCENT,
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
    CFW_THEME_ACCENT_COUNT
};

struct cfw_theme_style {
    unsigned char colors[CFW_THEME_COLOR_COUNT][4];
};

/* The dark argument is ignored while a custom style is selected. */
const unsigned char *cfw_theme_color(enum cfw_theme_role role, int dark);
/* Copies the style; the caller's storage need not outlive the call. */
void cfw_theme_style_set(const struct cfw_theme_style *style);
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

/* The switcher supplies appearance; reset uses the caller-provided fallback. */
void cfw_theme_appearance_set(int dark);
int cfw_theme_appearance_get(int fallback);
