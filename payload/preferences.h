/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Names are volume-0 paths such as "iPod_Control\\Device\\CFWPreferences".
 * Record validation belongs to the caller. Short transfers are failures. */
int cfw_preferences_read(const char *name, void *record, uint32_t size);
int cfw_preferences_write(const char *name, const void *record, uint32_t size);
void cfw_preferences_delete(const char *name);
