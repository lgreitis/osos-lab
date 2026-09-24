/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#define SERVICE ((volatile uint32_t *)0x22039000u)

void probe_finish(uint32_t status) __attribute__((noreturn));

void probe_finish(uint32_t status)
{
    ((void (*)(uint32_t))SERVICE[0])(status);
    for (;;) {
    }
}

void probe_flush(void)
{
    ((void (*)(void))SERVICE[1])();
}
