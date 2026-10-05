/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include "map-check.h"
#include "compat/nor/startup.h"
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

void after_dxe(uint32_t import_status) __attribute__((noreturn));

void after_dxe(uint32_t import_status)
{
    RECORD[3] = 6;
    RECORD[28] = import_status;
    require(import_status == 0);
    uint32_t base = RECORD[24];
    require(base >= 0x08000000 && base <= STAGE_START - 0xa000 && !(base & 0xfff));
    uint32_t bytes = MAP_CAPACITY, key = 0, stride = 0, version = 0;
    uint32_t status = apple_get_memory_map(base, &bytes, MAP, &key, &stride, &version);
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
    uint32_t base = apple_validate_dxe(entry, hob, stack, zero);
    RECORD[24] = base;
    RECORD[25] = entry;
    RECORD[26] = hob;
    RECORD[27] = stack;
    apple_intercept_import(base, (uint32_t)import_capture);
    probe_flush();
    apple_enter_dxe(entry, hob, stack, zero);
    probe_finish(4);
}
