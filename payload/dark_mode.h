/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

void cfw_dark_mode_load(void);
int cfw_dark_mode_enabled(void);
int cfw_dark_mode_action(void *controller, const char *action, uint32_t argument);
int cfw_dark_mode_property(uint32_t type, uint32_t property, uintptr_t *out);
