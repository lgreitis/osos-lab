/* SPDX-License-Identifier: MIT */

/* Integer maths for an app without a C library: 16.16 fixed point, sine
 * and cosine from a table, an integer square root, and the mixing and
 * easing curves of animation. Fixed-point values are int32_t with 16
 * fraction bits; EAPP_FIXED_ONE is 1.0. */

#ifndef EAPP_MATH_H
#define EAPP_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EAPP_FIXED_ONE 65536
/* A constant, such as EAPP_FIXED(1.5) or EAPP_FIXED(3 / 4.0), to 16.16. */
#define EAPP_FIXED(x) ((int32_t)((x) * 65536))
#define EAPP_FIXED_FROM_INT(i) ((int32_t)(i) * 65536)
/* Rounds down, so -0.5 becomes -1. */
#define EAPP_FIXED_TO_INT(f) ((int32_t)(f) >> 16)
#define EAPP_FIXED_ROUND(f) (((int32_t)(f) + 32768) >> 16)

/* The product wraps when it leaves the 32-bit range. */
static inline int32_t eapp_fixed_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 16);
}

/* a / b; b of 0 gives the largest value with a's sign. */
int32_t eapp_fixed_div(int32_t a, int32_t b);

/* Angles are 16 bits per turn: 0 is 0 degrees, 0x4000 a quarter turn,
 * 0x8000 a half; a value beyond 0xffff wraps. The results are 16.16, from
 * -EAPP_FIXED_ONE to EAPP_FIXED_ONE. */
#define EAPP_ANGLE_TURN 0x10000
#define EAPP_ANGLE_DEGREES(d) ((uint32_t)((d) * 65536 / 360))
int32_t eapp_sin16(uint32_t angle);
int32_t eapp_cos16(uint32_t angle);

/* The largest integer whose square is at most `value`. */
uint32_t eapp_isqrt(uint32_t value);

/* `t` is 16.16 from 0 to EAPP_FIXED_ONE and is clamped to that. lerp mixes
 * a and b, the curves map t to t: smoothstep eases both ends, ease_out starts
 * fast and settles, ease_in the reverse. */
int32_t eapp_lerp(int32_t a, int32_t b, int32_t t);
int32_t eapp_smoothstep(int32_t t);
int32_t eapp_ease_out(int32_t t);
int32_t eapp_ease_in(int32_t t);

static inline int32_t eapp_min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static inline int32_t eapp_max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static inline int32_t eapp_clamp(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

static inline int32_t eapp_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

#ifdef __cplusplus
}
#endif
#endif
