/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

int cfw_clicker_action(void *controller, const char *action, uint32_t argument);
int cfw_clicker_property(void *model, uint32_t type, uint32_t property, uintptr_t *out);
