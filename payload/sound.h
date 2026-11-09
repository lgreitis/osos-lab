/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Effects on the native output, after EQ, speed and the post-effects gain. */
void cfw_sound_process(int16_t *samples, uint32_t frames, const void *effects);

void cfw_crossfeed_load(void);
/* Reset All Settings: turns Crossfeed off and stores it. */
void cfw_crossfeed_reset(void);
void cfw_crossfeed_process(int16_t *samples, uint32_t frames, uint32_t rate);
int cfw_crossfeed_action(void *controller, const char *action, uint32_t argument);
int cfw_crossfeed_property(uint32_t type, uint32_t property, uintptr_t *out);
