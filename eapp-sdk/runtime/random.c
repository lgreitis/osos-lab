/* SPDX-License-Identifier: MIT */

#include "eapp.h"

/* xorshift32 never leaves zero, so zero stands in for the default seed. A
 * seed is mixed first (the splitmix finaliser), since xorshift32 gives small
 * numbers for a while after a seed with few set bits. */
#define DEFAULT_SEED 0x2545f491u

static uint32_t state = DEFAULT_SEED;

static uint32_t mix(uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return seed;
}

void eapp_random_seed(uint32_t seed)
{
    state = seed ? mix(seed) : DEFAULT_SEED;
}

uint32_t eapp_random(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint32_t eapp_random_below(uint32_t count)
{
    if (count < 2)
        return 0;
    /* Draw again above the largest multiple of count, so every value is
     * equally likely. */
    uint32_t limit = -count % count;
    uint32_t value;
    do
        value = eapp_random();
    while (value < limit);
    return value % count;
}
