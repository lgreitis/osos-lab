/* SPDX-License-Identifier: GPL-3.0-only */

/* The other module's code but for the immediate in the mov, so both load into
 * blocks of one size, the heap hands back the address just freed, and the
 * only difference sits in an instruction, which the instruction fetch reads. */

#include "value.h"

static uint32_t get(void)
{
    return 2;
}

EAPP_MODULE(struct value) = {get};
