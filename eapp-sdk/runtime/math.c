/* SPDX-License-Identifier: MIT */

#include "eapp_math.h"

int32_t eapp_fixed_div(int32_t a, int32_t b)
{
    if (b == 0)
        return a < 0 ? INT32_MIN : INT32_MAX;
    int64_t quotient = (int64_t)a * 65536 / b;
    return quotient > INT32_MAX ? INT32_MAX : quotient < INT32_MIN ? INT32_MIN : (int32_t)quotient;
}

/* sin of 0 to a quarter turn in 257 steps, 16.16; the rest is symmetry, and
 * the 6 low bits of the angle interpolate between steps. */
static const int32_t quarter_sine[257] = {
    0, 402, 804, 1206, 1608, 2010, 2412, 2814,
    3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
    6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218,
    9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391,
    12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534,
    15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639,
    19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699,
    22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708,
    25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656,
    28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
    30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347,
    33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075,
    36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716,
    39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264,
    41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713,
    44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056,
    46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288,
    48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404,
    50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398,
    52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
    54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004,
    56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607,
    57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071,
    59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392,
    60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568,
    61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596,
    62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473,
    63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197,
    64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766,
    64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
    65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436,
    65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535,
    65536,
};

int32_t eapp_sin16(uint32_t angle)
{
    angle &= 0xffff;
    int negative = angle >= 0x8000;
    angle &= 0x7fff;
    if (angle > 0x4000)
        angle = 0x8000 - angle;
    unsigned index = angle >> 6, fraction = angle & 63;
    int32_t low = quarter_sine[index];
    int32_t high = index < 256 ? quarter_sine[index + 1] : low;
    int32_t value = low + ((high - low) * (int32_t)fraction >> 6);
    return negative ? -value : value;
}

int32_t eapp_cos16(uint32_t angle)
{
    return eapp_sin16(angle + 0x4000);
}

uint32_t eapp_isqrt(uint32_t value)
{
    uint32_t root = 0, bit = 1u << 30;
    while (bit > value)
        bit >>= 2;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else
            root >>= 1;
        bit >>= 2;
    }
    return root;
}

static int32_t unit(int32_t t)
{
    return t < 0 ? 0 : t > EAPP_FIXED_ONE ? EAPP_FIXED_ONE : t;
}

int32_t eapp_lerp(int32_t a, int32_t b, int32_t t)
{
    return a + eapp_fixed_mul(b - a, unit(t));
}

int32_t eapp_smoothstep(int32_t t)
{
    t = unit(t);
    int32_t square = eapp_fixed_mul(t, t);
    return eapp_fixed_mul(square, 3 * EAPP_FIXED_ONE - 2 * t);
}

int32_t eapp_ease_out(int32_t t)
{
    t = EAPP_FIXED_ONE - unit(t);
    return EAPP_FIXED_ONE - eapp_fixed_mul(t, t);
}

int32_t eapp_ease_in(int32_t t)
{
    t = unit(t);
    return eapp_fixed_mul(t, t);
}
