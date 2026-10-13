/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

void cfw_night_shift_load(void);
int cfw_night_shift_action(void *controller, const char *action, uint32_t argument);
int cfw_night_shift_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_night_shift_init_gamma(const uint32_t *red, const uint32_t *green,
                               const uint32_t *blue);
