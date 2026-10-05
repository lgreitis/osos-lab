/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include "s5l8702/layout.h"

/* Addresses refer to the module slots reserved by the companion linker. */
static inline uint32_t apple_cpu_state(void *self, unsigned char *enabled)
{
    typedef uint32_t (*fn)(void *, unsigned char *);
    return ((fn)(APPLE_CPU_BASE + 0x26f))(self, enabled);
}

static inline uint32_t apple_cpu_disable(void *self)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)(APPLE_CPU_BASE + 0x25d))(self);
}

static inline uint32_t apple_clock_frequency(void *self, uint32_t selector)
{
    typedef uint32_t (*fn)(void *, uint32_t);
    return ((fn)(APPLE_CLOCK_BASE + 0x2cb))(self, selector);
}

static inline uint32_t apple_clock_gates(void *self, uint64_t mask, uint32_t enable)
{
    typedef uint32_t (*fn)(void *, uint64_t, uint32_t);
    return ((fn)(APPLE_CLOCK_BASE + 0x2cf))(self, mask, enable);
}

static inline uint32_t apple_irq_mapping(void *self, uint32_t irq, uint32_t *bank,
                                         uint32_t *bit)
{
    typedef uint32_t (*fn)(void *, uint32_t, uint32_t *, uint32_t *);
    return ((fn)(APPLE_IRQ_BASE + 0x221))(self, irq, bank, bit);
}

static inline void apple_rom_bind(void *cpu, void *clock, void *irq, void *allocator)
{
    *(volatile uint32_t *)(APPLE_ROM_BASE + 0x13b4) = (uint32_t)cpu;
    *(volatile uint32_t *)(APPLE_ROM_BASE + 0x13b8) = (uint32_t)clock;
    *(volatile uint32_t *)(APPLE_ROM_BASE + 0x13bc) = (uint32_t)irq;
    *(volatile uint32_t *)(APPLE_ROM_BASE + 0x13c0) = (uint32_t)allocator;
}

static inline uint32_t apple_rom_prepare(const void *header, const void *body)
{
    typedef uint32_t (*fn)(void *, const void *, const void *);
    return ((fn)(APPLE_ROM_BASE + 0x7f5))((void *)(APPLE_ROM_BASE + 0x13cc), header,
                                          body);
}
