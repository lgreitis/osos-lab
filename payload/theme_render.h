/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

void cfw_theme_fill(void *surface, const void *rect, const unsigned char *color,
                    uint32_t mode, void *clip);
void cfw_theme_clear_color(int red, int green, int blue, int alpha);
uint32_t cfw_theme_reflection_blend(uint32_t pixel, uint32_t background, int weight);
void cfw_theme_text_color(void *context, const unsigned char *color, const void *view);
void cfw_theme_background_fill(void *context, const int32_t *rect, const void *view);
int cfw_theme_selection_bar_prepare(int native_ram);
