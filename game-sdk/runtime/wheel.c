/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_api.h"

static const uint8_t directions[8] = {
    GAME_DIRECTION_UP,    GAME_DIRECTION_UP | GAME_DIRECTION_RIGHT,
    GAME_DIRECTION_RIGHT, GAME_DIRECTION_RIGHT | GAME_DIRECTION_DOWN,
    GAME_DIRECTION_DOWN,  GAME_DIRECTION_DOWN | GAME_DIRECTION_LEFT,
    GAME_DIRECTION_LEFT,  GAME_DIRECTION_LEFT | GAME_DIRECTION_UP,
};

uint8_t game_wheel_directions(const struct game_wheel *wheel, uint8_t top_position)
{
    if (!wheel || !wheel->touched)
        return 0;
    unsigned position = (uint8_t)(top_position - wheel->position + 16);
    return directions[position / 32];
}
