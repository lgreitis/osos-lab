/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

void cfw_theme_load(void);
int cfw_theme_action(void *controller, const char *action, uint32_t argument);
