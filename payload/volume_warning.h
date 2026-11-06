/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

void cfw_volume_warning_load(void);
int cfw_volume_warning_action(void *controller, const char *action, uint32_t argument);
int cfw_volume_warning_property(uint32_t type, uint32_t property, uintptr_t *out);
