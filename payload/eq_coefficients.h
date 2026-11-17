/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_EQ_COEFFICIENTS_H
#define CFW_EQ_COEFFICIENTS_H
#include <stdint.h>

enum { CFW_EQ_BANDS = 5, CFW_EQ_RESPONSE_POINTS = 261 };

enum cfw_eq_type { CFW_EQ_OFF, CFW_EQ_PEAKING, CFW_EQ_LOW_SHELF, CFW_EQ_HIGH_SHELF };

/* Output order: b0, b1, b2, a1, a2; signed integers with 24 fractional bits. */
int cfw_eq_coefficients(int32_t out[5], unsigned int type, unsigned int hz,
                        unsigned int q_tenths, int gain_tenths_db,
                        unsigned int precut_tenths_db, unsigned int rate);
/* Combined response at logarithmic points from 20 Hz to 20 kHz, in tenths of dB. */
void cfw_eq_response_curve(const int32_t coefficients[CFW_EQ_BANDS][5],
                           unsigned int rate, int16_t out[CFW_EQ_RESPONSE_POINTS]);
/* Scale b0, b1, b2 by a precut of 0..100 tenths of a decibel. */
void cfw_eq_attenuate(int32_t coefficients[3], unsigned int precut_tenths_db);
/* Peak of a quantized cascade over 20 Hz..20 kHz in tenths of a decibel, rounded up. */
int cfw_eq_peak(const int32_t (*coefficients)[5], unsigned int count,
                unsigned int rate);
#endif
