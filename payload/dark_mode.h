/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Legacy import: pre-store Dark Mode file. */
#define CFW_DARK_MODE_LEGACY_FILE "iPod_Control\\Device\\CFWDark"

void cfw_dark_mode_load(void);
int cfw_dark_mode_enabled(void);
int cfw_dark_mode_action(void *controller, const char *action, uint32_t argument);
/* Reset All Settings: turns Dark Mode off. */
void cfw_dark_mode_reset(void);
int cfw_dark_mode_property(uint32_t type, uint32_t property, uintptr_t *out);
