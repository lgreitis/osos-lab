/* SPDX-License-Identifier: GPL-3.0-only */

/* Checks eapp_math.h against the C library's maths: the fixed-point helpers,
 * sine and cosine over a turn, the square root over its range and the
 * curves' ends and shape. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "eapp_math.h"

int main(void)
{
    assert(EAPP_FIXED(1.5) == 98304 && EAPP_FIXED_FROM_INT(-2) == -131072);
    assert(EAPP_FIXED_TO_INT(EAPP_FIXED(-0.5)) == -1 && EAPP_FIXED_ROUND(EAPP_FIXED(2.5)) == 3);
    assert(eapp_fixed_mul(EAPP_FIXED(1.5), EAPP_FIXED(-2)) == EAPP_FIXED(-3));
    assert(eapp_fixed_div(EAPP_FIXED(3), EAPP_FIXED(2)) == EAPP_FIXED(1.5));
    assert(eapp_fixed_div(EAPP_FIXED(-3), EAPP_FIXED(2)) == EAPP_FIXED(-1.5));
    assert(eapp_fixed_div(EAPP_FIXED(3), EAPP_FIXED(-2)) == EAPP_FIXED(-1.5));
    assert(eapp_fixed_div(EAPP_FIXED(-30000), 1) == INT32_MIN);
    assert(eapp_fixed_div(1, 0) == INT32_MAX && eapp_fixed_div(-1, 0) == INT32_MIN);
    assert(eapp_fixed_div(EAPP_FIXED(30000), 1) == INT32_MAX);

    /* Within 1/4096 of the true value over the whole turn, exact at the
     * quarters. */
    for (uint32_t angle = 0; angle < 0x20000; angle += 7) {
        double radians = (angle & 0xffff) * 2 * M_PI / 65536;
        assert(fabs(eapp_sin16(angle) - sin(radians) * 65536) <= 16);
        assert(fabs(eapp_cos16(angle) - cos(radians) * 65536) <= 16);
    }
    assert(eapp_sin16(0) == 0 && eapp_sin16(0x4000) == EAPP_FIXED_ONE);
    assert(eapp_sin16(0x8000) == 0 && eapp_sin16(0xc000) == -EAPP_FIXED_ONE);
    assert(eapp_cos16(0) == EAPP_FIXED_ONE && eapp_cos16(0x8000) == -EAPP_FIXED_ONE);
    assert(EAPP_ANGLE_DEGREES(90) == 0x4000 && EAPP_ANGLE_DEGREES(360) == EAPP_ANGLE_TURN);

    for (uint32_t value = 0; value < 70000; ++value)
        assert(eapp_isqrt(value) == (uint32_t)floor(sqrt(value)));
    assert(eapp_isqrt(0xffffffffu) == 65535 && eapp_isqrt(65536u * 65536u - 1) == 65535);
    for (unsigned i = 0; i < 100000; ++i) {
        uint32_t value = (uint32_t)rand() * 2654435761u;
        assert(eapp_isqrt(value) == (uint32_t)floor(sqrt(value)));
    }

    assert(eapp_lerp(10, 20, 0) == 10 && eapp_lerp(10, 20, EAPP_FIXED_ONE) == 20);
    assert(eapp_lerp(10, 20, EAPP_FIXED(0.5)) == 15 && eapp_lerp(10, 20, -5) == 10);
    assert(eapp_lerp(10, 20, 2 * EAPP_FIXED_ONE) == 20);
    assert(eapp_smoothstep(0) == 0 && eapp_smoothstep(EAPP_FIXED_ONE) == EAPP_FIXED_ONE);
    assert(eapp_smoothstep(EAPP_FIXED(0.5)) == EAPP_FIXED(0.5));
    assert(eapp_smoothstep(EAPP_FIXED(0.25)) < EAPP_FIXED(0.25));
    assert(eapp_ease_out(0) == 0 && eapp_ease_out(EAPP_FIXED_ONE) == EAPP_FIXED_ONE);
    assert(eapp_ease_out(EAPP_FIXED(0.5)) == EAPP_FIXED(0.75));
    assert(eapp_ease_in(EAPP_FIXED(0.5)) == EAPP_FIXED(0.25));
    assert(eapp_ease_in(EAPP_FIXED(3)) == EAPP_FIXED_ONE);
    int32_t last = -1;
    for (int32_t t = 0; t <= EAPP_FIXED_ONE; t += 97) {
        int32_t value = eapp_smoothstep(t);
        assert(value >= last);
        last = value;
    }
    assert(eapp_min(1, 2) == 1 && eapp_max(1, 2) == 2 && eapp_abs(-3) == 3);
    assert(eapp_clamp(5, 0, 3) == 3 && eapp_clamp(-1, 0, 3) == 0 && eapp_clamp(2, 0, 3) == 2);
    puts("math_test passed");
    return 0;
}
