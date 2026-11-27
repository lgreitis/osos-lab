# Maths

`eapp_math.h` is integer only.

## Fixed point

Fixed-point values are `int32_t` with 16 fraction bits. `EAPP_FIXED_ONE` is 1.0.

| | |
|---|---|
| `EAPP_FIXED(1.5)` | A constant |
| `EAPP_FIXED_FROM_INT`, `EAPP_FIXED_TO_INT`, `EAPP_FIXED_ROUND` | Convert. `TO_INT` rounds down |
| `eapp_fixed_mul`, `eapp_fixed_div` | Multiply and divide. A division by zero gives the largest value with the dividend's sign |

`eapp_blend_triangle` takes 16.16 coordinates, so `EAPP_FIXED_FROM_INT` feeds it.

## Angles

Angles are 16 bits per turn. `EAPP_ANGLE_DEGREES(90)` is a quarter. `eapp_sin16` and `eapp_cos16` answer 16.16 from a table, within 1/4096.

## Other calls

| | |
|---|---|
| `eapp_isqrt` | The integer square root |
| `eapp_lerp(a, b, t)` | Mixes two values by a 16.16 `t` from 0 to `EAPP_FIXED_ONE` |
| `eapp_smoothstep`, `eapp_ease_out`, `eapp_ease_in` | Map such a `t` to an eased `t` |
| `eapp_min`, `eapp_max`, `eapp_clamp`, `eapp_abs` | Minimum, maximum, clamp and absolute value |

## Random

`eapp_random` is xorshift32, 32 bits per call. `eapp_random_below(count)` answers 0 to `count - 1` without bias. The runtime seeds from the clock at init; `eapp_random_seed` gives a fixed stream.
