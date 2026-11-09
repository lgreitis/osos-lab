/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
void cfw_theme_settings_load(void);
/* Shows the Theme row when a custom package is known; probes /themes only after
 * cfw_theme_settings_invalidate. */
void cfw_theme_settings_refresh(void);
/* Marks the custom package probe stale, for when the volume may have changed. */
void cfw_theme_settings_invalidate(void);
/* Probes /themes if the result is stale. Runs on the music loading task, which
 * reads the volume itself; the probe only reads files and stores two flags. */
void cfw_theme_settings_probe_stale(void);
/* Releases the picker list; called when the Settings main menu is destroyed. */
void cfw_theme_settings_close(void);
int cfw_theme_settings_action(void *controller, const char *action, uint32_t argument);
int cfw_theme_settings_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_theme_settings_indexed_property(uint32_t type, uint32_t property,
                                        uint32_t index, uintptr_t *out);
void cfw_theme_settings_toggle(void *controller);
/* Reset All Settings: Classic light with the default accent. */
void cfw_theme_settings_reset(void);
const char *cfw_theme_settings_error(void);

uint32_t cfw_theme_settings_selected(void);
uint32_t cfw_theme_settings_accent_selected(void);
void cfw_theme_settings_register(void);
