/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_CUSTOM_EQ_H
#define CFW_CUSTOM_EQ_H
#include <stdint.h>
#include "custom_eq_ui.h"

#define CFW_EQ_DSP_PRESET 23

int cfw_settings_action(void *controller, const char *action, uint32_t argument);
void cfw_eq_load_preferences(void);
int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS]);
void cfw_eq_load(void *state);
void cfw_eq_process(void *state, int16_t *samples, uint32_t frames, int16_t **output,
                    uint32_t *output_frames);
#endif
