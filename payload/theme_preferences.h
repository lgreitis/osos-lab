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
/* Reads the settings store's theme record; ABSENT only when neither it nor a
 * Dark Mode setting from before themes exists. */
enum cfw_theme_preferences_status
cfw_theme_preferences_load(struct cfw_theme_preferences *out);
/* Sets the store record and reads it back; the disk sees it at the next store
 * flush. Returns UNKNOWN if the record was not kept and UNCHANGED for invalid input. */
enum cfw_theme_store_result
cfw_theme_preferences_store(const struct cfw_theme_preferences *state);
