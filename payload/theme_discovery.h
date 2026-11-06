/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "theme_package.h"

struct cfw_theme_choice {
    char id[32], name[48];
};

/* Builds "themes/<id>/<file>" on volume 0. Fails for unsafe ids or long names. */
int cfw_theme_discovery_path(const char *id, const char *file, struct osos_path *out);
/* Scans /themes into a caller-owned array, sorted by id. Keeps no state.
 * When more valid themes exist than capacity, the smallest ids are kept and
 * *overflow is set (overflow may be NULL). Returns the number stored.
 * Capacity 0 is an existence probe: entries may be NULL, and the scan stops at
 * the first valid theme with *overflow set. */
uint32_t cfw_theme_discovery_scan(struct cfw_theme_choice *entries, uint32_t capacity,
                                  int *overflow);

/* Revalidate a custom theme and choose an appearance. prefer_dark is 0 or 1.
 * allow_fallback selects the sole supported appearance if the preference is
 * unavailable; boot restoration passes 0. Classic and legacy ids are rejected.
 * All outputs are required and remain unchanged on failure. The switcher checks
 * the pack prefix before saving the selection and its records while applying. */
int cfw_theme_discovery_select(const char *id, uint32_t prefer_dark, int allow_fallback,
                               struct cfw_theme_metadata *metadata,
                               struct osos_path *pack, uint32_t *dark);
