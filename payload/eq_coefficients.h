/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_EQ_COEFFICIENTS_H
#define CFW_EQ_COEFFICIENTS_H
#include <stdint.h>

enum cfw_eq_type { CFW_EQ_OFF, CFW_EQ_PEAKING, CFW_EQ_LOW_SHELF, CFW_EQ_HIGH_SHELF };

/* Output order: b0, b1, b2, a1, a2; signed integers with 24 fractional bits. */
int cfw_eq_coefficients(int32_t out[5], unsigned int type, unsigned int hz,
                        unsigned int q_tenths, int gain_half_db,
                        unsigned int precut_half_db, unsigned int rate);
#endif
