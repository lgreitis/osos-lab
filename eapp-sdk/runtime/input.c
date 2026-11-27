/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/input.c (8fc840d) by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

struct eapp_wheel eapp_input_read_wheel(void)
{
    int32_t delta = 0;
    uint32_t flags = 0;

    GameInput_ReadWheel(&delta, &flags);

    return (struct eapp_wheel){
        .delta = delta,
        .position = flags & 0xff,
        .touched = (flags & WheelTouch_Down) != 0,
    };
}

int eapp_hold_locked(void)
{
    uint8_t locked = 0;
    uint32_t size = sizeof locked;
    return GameSettings_GetValue("HoldSwitch", &locked, &size) == 0 && locked != 0;
}
