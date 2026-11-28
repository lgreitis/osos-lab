/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

void cfw_ui_visibility_adjust_property(uint32_t type, uint32_t property,
                                       uintptr_t *out);
int cfw_ui_visibility_property(void *model, uint32_t type, uint32_t property,
                               uint32_t index, uintptr_t *out);
