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
/* Color for labels drawn on an option bar well (style 0 White, 1 Black): the
 * text or background role, whichever contrasts more with the resident well
 * center art. NULL keeps the native color: Classic, other styles, or
 * transparent well art. */
const unsigned char *cfw_theme_well_letter_color(unsigned style);
/* Color for the text typed into the Search input field: the text or background
 * role, whichever contrasts more with the resident field center art
 * (System_Input_Field_Middle_Image). NULL keeps the native color: Classic, or
 * transparent field art. */
const unsigned char *cfw_theme_field_text_color(void);
/* Color for the status bar title: the text or background role,
 * whichever contrasts more with the resident bar art
 * (NowPlaying_StatusBar_Image). NULL keeps the native color: Classic, or
 * transparent bar art. */
const unsigned char *cfw_theme_status_bar_text_color(void);
