/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "osos.h"
#include "theme_switch.h"

struct cfw_theme_boot_request {
    struct cfw_theme_selection selection;
    struct osos_path pack;
    uint32_t appearance_mask;
    int migrate;
    char name[48];
};

/* Resolve preferences without activating artwork or writing storage. Returns 1
 * when Classic-light recovery must be persisted. Empty pack.text means stock.
 * A resolved pack still needs package preparation and application
 * before its selection may be reported as active. */
int cfw_theme_boot_resolve(struct cfw_theme_boot_request *request);
