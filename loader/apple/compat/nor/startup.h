/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "dxe.h"
static const unsigned char dxe_entry[] = {0x31, 0xb5, 0x88, 0xb0, 0x79, 0x4c,
                                          0x8,  0x98, 0x2,  0xaa, 0xe0, 0x63,
                                          0x4,  0xa9, 0x8,  0xa8};
static const unsigned char dxe_stop[] = {0x67, 0x48, 0xa1, 0x6a, 0x4, 0xf0, 0x15, 0xfe};
static const unsigned char dxe_get_map[] = {0xff, 0xb5, 0x83, 0xb0, 0x3, 0x98,
                                            0xc,  0x0,  0x0,  0x28, 0x1, 0xd1,
                                            0x9b, 0x48, 0xc9, 0xe6};

void probe_finish(uint32_t status) __attribute__((noreturn));

static inline void apple_startup_require(int valid)
{
    if (!valid)
        probe_finish(9);
}

static inline uint32_t apple_startup_word(const void *p, unsigned offset)
{
    const unsigned char *bytes = p;
    return (uint32_t)bytes[offset] | (uint32_t)bytes[offset + 1] << 8 |
           (uint32_t)bytes[offset + 2] << 16 | (uint32_t)bytes[offset + 3] << 24;
}

static inline void apple_startup_match(uint32_t address, const unsigned char *expected,
                                       unsigned count)
{
    const volatile unsigned char *actual = (const volatile unsigned char *)address;
    for (unsigned i = 0; i < count; ++i)
        apple_startup_require(actual[i] == expected[i]);
}

static inline uint32_t apple_validate_dxe(uint32_t entry, uint32_t hob, uint32_t stack,
                                          uint32_t zero)
{
    apple_startup_require((entry & 1) && entry >= 0x08000243);
    uint32_t base = apple_dxe_base(entry);
    apple_startup_require(!(base & 0xfff) && base <= 0x0b000000u - 0xa000);
    /* Original stack pointer is allocation base + 0x1fff0, leaving 16 bytes. */
    apple_startup_require(hob == 0x08000000 && stack == 0x0aff7ff0 && zero == 0);
    apple_startup_require(apple_startup_word((void *)base, 0x3c) == 0x80);
    apple_startup_require(apple_startup_word((void *)base, 0x80) == 0x4550);
    apple_startup_require(apple_startup_word((void *)base, 0xa8) == 0x243);
    apple_startup_require(apple_startup_word((void *)base, 0xd0) == 0x92a0);
    apple_startup_match(base + 0x242, dxe_entry, sizeof(dxe_entry));
    apple_startup_match(base + APPLE_DXE_IMPORT_OFFSET, dxe_stop, sizeof(dxe_stop));
    apple_startup_match(base + 0x2464, dxe_get_map, sizeof(dxe_get_map));
    return base;
}

static inline void apple_intercept_import(uint32_t base, uint32_t callback)
{
    /* Aligned Thumb-1 absolute branch: ldr r3,[pc,#0]; bx r3; literal.
     * Replaces the first two loads and next call after map import returns.
     * import_capture saves the state used to resume DXE later.
     */
    volatile uint16_t *patch = (volatile uint16_t *)(base + APPLE_DXE_IMPORT_OFFSET);
    patch[0] = 0x4b00;
    patch[1] = 0x4718;
    *(volatile uint32_t *)(base + APPLE_DXE_IMPORT_OFFSET + 4) = callback;
}
