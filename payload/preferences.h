/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Names are volume-0 paths such as "iPod_Control\\Device\\CFWPreferences".
 * Record validation belongs to the caller. Short transfers are failures. */
enum cfw_preferences_read_result {
    CFW_PREFERENCES_IO_ERROR = -2,
    CFW_PREFERENCES_SHORT = -1,
    CFW_PREFERENCES_MISSING,
    CFW_PREFERENCES_OK,
};

int cfw_preferences_read_status(const char *name, void *record, uint32_t size);
int cfw_preferences_write(const char *name, const void *record, uint32_t size);
void cfw_preferences_delete(const char *name);
