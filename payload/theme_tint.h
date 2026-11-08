/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Tint color for a bitmap about to be drawn from its pixel data, or NULL to keep
 * the native color: tinted catalog masks draw in their tint role. */
const unsigned char *cfw_theme_bitmap_tint(const unsigned char *pixels);
/* Same as cfw_theme_bitmap_tint for the view's bitmap, except that a highlighted
 * view takes the on-accent role so tinted icons stay visible on the selection bar. */
const unsigned char *cfw_theme_highlight_tint(const void *view);
/* Wrap one paletted blit of a bitmap context by a view: a tinted asset in a
 * highlighted view draws with the on-accent palette, restored by the end call. */
void cfw_theme_bitmap_blit_begin(const void *view, const void *context);
/* Same for one alarm or event marker blit of a calendar cell: tinted markers on
 * the selected day (Calendar_Hilite_Cell) draw with the on-accent palette. */
void cfw_theme_calendar_marker_begin(const void *cell, const void *context);
void cfw_theme_bitmap_blit_end(void);
/* Rewrite the resident palettes of tinted paletted bitmaps and the pixels of
 * tinted ARGB bitmaps with their tint role. Call after a style or accent change;
 * the caller redraws. */
void cfw_theme_tint_refresh(void);
/* Recolor the stock accent artwork (cfw_bitmap_accent_art) with each entry's
 * role when cfw_theme_accent_art_active, skipping assets set in skip (replaced
 * by the active pack). Expects stock pixels; sets each recolored asset in
 * touched so the caller restores it from the baseline before recoloring again. */
void cfw_theme_accent_art_apply(const uint32_t *skip, uint32_t *touched);
/* Color for labels drawn on an option bar well (style 0 White, 1 Black, 2 Now
 * Playing): the
 * text or background role, whichever contrasts more with the resident well
 * center art. NULL keeps the native color: Classic, other styles, or
 * transparent well art. */
const unsigned char *cfw_theme_well_letter_color(unsigned style);
/* Color for the text typed into the Search input field: the text or background
 * role, whichever contrasts more with the resident field center art
 * (System_Input_Field_Middle_Image). NULL keeps the native color: Classic, or
 * transparent field art. */
const unsigned char *cfw_theme_field_text_color(void);
/* Color for a native text view listed in cfw_bitmap_contrast_text: the text or
 * background role, whichever contrasts more with the resident art behind it.
 * NULL keeps the native color: Classic, unlisted views, or transparent art. */
const unsigned char *cfw_theme_contrast_text_color(uint32_t view);
/* Where the calendar month view draws a label. */
enum cfw_calendar_place {
    CFW_CALENDAR_ACTIVE,   /* day of the shown month, on Calendar_Active_Cell */
    CFW_CALENDAR_INACTIVE, /* day of a neighboring month, on Calendar_Inactive_Cell */
    CFW_CALENDAR_HEADER,   /* weekday name, on the page */
};
/* Color for a calendar label: the text or background role, whichever contrasts
 * more with the center pixel of the cell art, or with the background role for
 * the header and for a clear cell center. Inactive days and headers are mixed
 * halfway toward what is behind them; such results are written to out. NULL
 * keeps the native color: Classic. */
const unsigned char *cfw_theme_calendar_text_color(unsigned place, unsigned char out[4]);
