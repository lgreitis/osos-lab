/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

void cfw_playback_log_load(void);
int cfw_playback_log_action(void *controller, const char *action, uint32_t argument);
int cfw_playback_log_property(uint32_t type, uint32_t property, uintptr_t *out);
