/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/wheel.c (8fc840d) by lgreitis. */

#include "eapp.h"

static const uint8_t directions[8] = {
    EAPP_DIRECTION_UP,    EAPP_DIRECTION_UP | EAPP_DIRECTION_RIGHT,
    EAPP_DIRECTION_RIGHT, EAPP_DIRECTION_RIGHT | EAPP_DIRECTION_DOWN,
    EAPP_DIRECTION_DOWN,  EAPP_DIRECTION_DOWN | EAPP_DIRECTION_LEFT,
    EAPP_DIRECTION_LEFT,  EAPP_DIRECTION_LEFT | EAPP_DIRECTION_UP,
};

uint8_t eapp_wheel_directions(const struct eapp_wheel *wheel, uint8_t top_position)
{
    if (!wheel || !wheel->touched)
        return 0;
    unsigned position = (uint8_t)(top_position - wheel->position + 16);
    return directions[position / 32];
}
