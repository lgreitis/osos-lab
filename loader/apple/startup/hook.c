/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include "map-check.h"
#include "fingerprints.h"
#define RECORD ((volatile uint32_t *)0x2203c000u)
#define MAP ((unsigned char *)0x22030000u)
extern void probe_finish(uint32_t status) __attribute__((noreturn));
extern void probe_flush(void);
extern void import_capture(void);
extern void prepare_startup(uint32_t dxe_base);

static void require(int condition)
{
    if (!condition)
        probe_finish(9);
}

static void match(uint32_t address, const unsigned char *expected, unsigned count)
{
    const volatile unsigned char *actual = (const volatile unsigned char *)address;
    for (unsigned i = 0; i < count; ++i)
        require(actual[i] == expected[i]);
}

void after_dxe(uint32_t import_status) __attribute__((noreturn));

void after_dxe(uint32_t import_status)
{
    RECORD[3] = 6;
    RECORD[28] = import_status;
    require(import_status == 0);
    uint32_t base = RECORD[24];
    require(base >= 0x08000000 && base <= STAGE_START - 0xa000 && !(base & 0xfff));
    uint32_t bytes = MAP_CAPACITY, key = 0, stride = 0, version = 0;
    typedef uint32_t (*get_map_fn)(uint32_t *, void *, uint32_t *, uint32_t *,
                                   uint32_t *);
    uint32_t status =
        ((get_map_fn)(base + 0x2465))(&bytes, MAP, &key, &stride, &version);
    RECORD[29] = status;
    RECORD[30] = bytes;
    RECORD[31] = stride;
    RECORD[32] = version;
    require(status == 0 && valid_map(MAP, bytes, stride, version));
    prepare_startup(base);
    probe_finish(4);
}

void before_dxe(uint32_t entry, uint32_t hob, uint32_t stack, uint32_t zero)
    __attribute__((noreturn));

void before_dxe(uint32_t entry, uint32_t hob, uint32_t stack, uint32_t zero)
{
    RECORD[3] = 5;
    require((entry & 1) && entry >= 0x08000243);
    uint32_t base = entry - 0x243;
    require(!(base & 0xfff) && base <= STAGE_START - 0xa000);
    /* Original stack pointer is allocation base + 0x1fff0, leaving 16 bytes. */
    require(hob == 0x08000000 && stack == 0x0aff7ff0 && zero == 0);
    require(word((void *)base, 0x3c) == 0x80);
    require(word((void *)base, 0x80) == 0x4550);
    require(word((void *)base, 0xa8) == 0x243);
    require(word((void *)base, 0xd0) == 0x92a0);
    match(base + 0x242, dxe_entry, sizeof(dxe_entry));
    match(base + 0x290, dxe_stop, sizeof(dxe_stop));
    match(base + 0x2464, dxe_get_map, sizeof(dxe_get_map));
    RECORD[24] = base;
    RECORD[25] = entry;
    RECORD[26] = hob;
    RECORD[27] = stack;
    /* Aligned Thumb-1 absolute branch: ldr r3,[pc,#0]; bx r3; literal.
     * Replaces the first two loads and next call after map import returns.
     * import_capture saves the state used to resume DXE later.
     */
    volatile uint16_t *patch = (volatile uint16_t *)(base + 0x290);
    patch[0] = 0x4b00;
    patch[1] = 0x4718;
    *(volatile uint32_t *)(base + 0x294) = (uint32_t)import_capture;
    probe_flush();
    typedef void (*enter_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    ((enter_fn)0x220106c8)(entry, hob, stack, zero);
    probe_finish(4);
}
