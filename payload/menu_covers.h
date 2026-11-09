/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

int cfw_menu_covers_enabled(void);
void cfw_menu_covers_load(void);
/* Reset All Settings: returns to the theme default and stores it. */
void cfw_menu_covers_reset(void);
int cfw_menu_covers_action(void *controller, const char *action, uint32_t argument);
int cfw_menu_covers_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_menu_covers_query(void *provider, uint32_t flags);
int cfw_menu_covers_query_original(void *provider, uint32_t flags);
