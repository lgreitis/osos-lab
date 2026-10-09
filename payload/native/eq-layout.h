/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/* Shared native three-band EQ layout. */
enum {
    OSOS_EQ_STATE_BYTES = 0xb8,
    OSOS_EQ_RATE_OFFSET = 4,
    OSOS_EQ_ENABLED_OFFSET = 8,
    OSOS_EQ_PRESET_OFFSET = 0xb4,
    OSOS_EQ_STAGE_STRIDE = 0x38,
    OSOS_EQ_A1_OFFSET = 0x0c,
    OSOS_EQ_A2_OFFSET = 0x10,
    OSOS_EQ_B0_OFFSET = 0x14,
    OSOS_EQ_B1_OFFSET = 0x18,
    OSOS_EQ_B2_OFFSET = 0x1c,
    OSOS_EQ_BYPASS_OFFSET = 0x40,
};

static inline uint32_t osos_eq_preset(const void *state)
{
    return *(const uint32_t *)((const unsigned char *)state + OSOS_EQ_PRESET_OFFSET);
}

static inline void osos_eq_set_stage(void *state, unsigned band, const int32_t c[5])
{
    unsigned char *stage = (unsigned char *)state + band * OSOS_EQ_STAGE_STRIDE;
    *(int32_t *)(stage + OSOS_EQ_B0_OFFSET) = c[0];
    *(int32_t *)(stage + OSOS_EQ_B1_OFFSET) = c[1];
    *(int32_t *)(stage + OSOS_EQ_B2_OFFSET) = c[2];
    *(int32_t *)(stage + OSOS_EQ_A1_OFFSET) = c[3];
    *(int32_t *)(stage + OSOS_EQ_A2_OFFSET) = c[4];
    stage[OSOS_EQ_BYPASS_OFFSET] =
        c[0] == (1 << 24) && !c[1] && !c[2] && !c[3] && !c[4];
}
