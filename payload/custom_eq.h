/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_CUSTOM_EQ_H
#define CFW_CUSTOM_EQ_H
#include <stdint.h>
#include "custom_eq_ui.h"
#include "eq_coefficients.h"

#define CFW_EQ_DSP_PRESET 23
/* Legacy import: pre-store EQ file. */
#define CFW_EQ_LEGACY_FILE "iPod_Control\\Device\\CFWEQ"
/* Precut choice index computed from the response; manual decibels follow it. */
#define CFW_EQ_PRECUT_AUTO 0

uint32_t cfw_irq_save(void);
void cfw_irq_restore(uint32_t flags);

extern const struct cfw_ui_model cfw_eq_model;
/* Returns the bank version; precut is the applied attenuation in tenths of a dB. */
uint32_t cfw_eq_snapshot(int32_t coefficients[CFW_EQ_BANDS][5], uint32_t *rate,
                         uint32_t *precut);

void cfw_eq_load_preferences(void);
/* Reset All Settings: restores and stores the default Custom EQ. */
void cfw_eq_reset_preferences(void);
int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS]);
void cfw_eq_load(void *state);
void cfw_eq_reset(void *state);
void cfw_eq_process(void *state, int16_t *samples, uint32_t frames, int16_t **output,
                    uint32_t *output_frames);
#endif
