/* SPDX-License-Identifier: GPL-3.0-only */

#include "game_api.h"
#include "native.h"

static uint32_t last_us, elapsed_ms, remainder_us;
static int clock_started;

uint32_t game_ticks_ms(void)
{
    uint32_t now = osos_ticks_us();
    if (!clock_started) {
        last_us = now;
        clock_started = 1;
    }

    uint32_t delta = now - last_us;
    last_us = now;
    elapsed_ms += delta / 1000;
    remainder_us += delta % 1000;
    elapsed_ms += remainder_us / 1000;
    remainder_us %= 1000;

    return elapsed_ms;
}

void game_sleep_ms(uint32_t milliseconds)
{
    uint32_t start = game_ticks_ms();

    do {
        osos_yield();
    } while (game_ticks_ms() - start < milliseconds);
}
