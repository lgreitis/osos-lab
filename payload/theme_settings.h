/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
void cfw_theme_settings_load(void);
void cfw_theme_settings_refresh(void);
/* Releases the picker list; called when the Settings main menu is destroyed. */
void cfw_theme_settings_close(void);
int cfw_theme_settings_action(void *controller, const char *action, uint32_t argument);
int cfw_theme_settings_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_theme_settings_indexed_property(uint32_t type, uint32_t property,
                                        uint32_t index, uintptr_t *out);
void cfw_theme_settings_toggle(void *controller);
const char *cfw_theme_settings_error(void);

uint32_t cfw_theme_settings_selected(void);
uint32_t cfw_theme_settings_accent_selected(void);
void cfw_theme_settings_register(void);
