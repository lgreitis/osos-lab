/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "theme_switch.h"

/* RTP1: magic, version, pending, id[32], dark, accent, CRC32; little endian.
 * This record is deliberately independent of C layout and package versions. */
#define CFW_THEME_PREFERENCE_BYTES 56u
int cfw_theme_preferences_encode(const struct cfw_theme_preferences *state,
                                 unsigned char out[CFW_THEME_PREFERENCE_BYTES]);
int cfw_theme_preferences_decode(const unsigned char *data, uint32_t size,
                                 struct cfw_theme_preferences *out);
/* Only a native file-not-found result reports ABSENT; other errors are INVALID. */
enum cfw_theme_preferences_status
cfw_theme_preferences_load(struct cfw_theme_preferences *out);
/* Checks write length, explicit file sync, and full-record readback.
 * Returns UNKNOWN on storage errors and UNCHANGED for invalid input. */
enum cfw_theme_store_result
cfw_theme_preferences_store(const struct cfw_theme_preferences *state);
