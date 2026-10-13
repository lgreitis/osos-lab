/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

int cfw_theme_enabled(void);
const void *cfw_theme_color(uint32_t id, const void *color);
