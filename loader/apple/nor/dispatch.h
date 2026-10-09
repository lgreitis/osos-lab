/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include "s5l8702/layout.h"
#define SST_ENTRY 0x4cbu
#define SST_SIZE 0x9c0u
static const unsigned char dxe_import[] = {0x67, 0x48, 0xa1, 0x6a,
                                           0x4,  0xf0, 0x15, 0xfe};
static const unsigned char dxe_stop[] = {0x20, 0x6a, 0x1, 0x68, 0x88, 0x47, 0x1, 0x20};
static const unsigned char dxe_image[] = {0x20, 0x73, 0x22, 0x69,
                                          0x21, 0x6a, 0x2,  0x98};
static const unsigned char sst_unlock[] = {0x20, 0x68, 0xe, 0x22,
                                           0x43, 0x68, 0x0, 0x21};

void probe_finish(uint32_t status) __attribute__((noreturn));
void probe_flush(void);

static uint32_t apple_dispatch_word(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)(address);
}

static void apple_dispatch_require(int ok)
{
    if (!ok)
        probe_finish(4);
}

static void apple_dispatch_match(uint32_t address, const unsigned char *bytes,
                                 unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        apple_dispatch_require(*(volatile unsigned char *)(uintptr_t)(address + i) ==
                               bytes[i]);
}

static void apple_dispatch_jump(uint32_t address, uint32_t target)
{
    apple_dispatch_require(!(address & 3));
    *(volatile uint32_t *)(uintptr_t)(address) = 0x47184b00; /* ldr r3; bx r3 */
    *(volatile uint32_t *)(uintptr_t)(address + 4) = target;
}

/* The trampoline calls this before each original module entry. */
static inline void apple_prepare_driver(uint32_t image)
{
    apple_dispatch_require(image >= 0x08000000 && image < 0x0affff00 && !(image & 3));
    uint32_t base = apple_dispatch_word(image + 0x38),
             entry = apple_dispatch_word(image + 0x10);
    apple_dispatch_require(base >= 0x08000000 && base < 0x0aff0000 && !(base & 0xfff));
    apple_dispatch_require(apple_dispatch_word(base + 0x3c) == 0x80 &&
                           apple_dispatch_word(base + 0x80) == 0x4550);
    uint32_t size = apple_dispatch_word(base + 0xd0),
             rva = apple_dispatch_word(base + 0xa8);
    apple_dispatch_require(size >= 0x220 && size <= 0x10000 && rva < size);
    apple_dispatch_require(entry == base + rva && (entry & 1));
    if (rva == SST_ENTRY && size == SST_SIZE) {
        apple_dispatch_match(base + 0x522, sst_unlock, sizeof(sst_unlock));
        /* Keep SPI setup/read-status and protocol installation. Skip EWSR
         * and WRSR(0), which otherwise clear flash block protection. */
        *(volatile uint16_t *)(uintptr_t)(base + 0x522) = 0xe02f; /* b base+0x584 */
        probe_flush();
    }
}

static inline void apple_validate_dispatch(uint32_t base)
{
    const volatile uint32_t *snapshot = (const volatile uint32_t *)0x0bb31000;
    apple_dispatch_require(base >= 0x08000000 && base < 0x0aff0000 && !(base & 0xfff));
    apple_dispatch_require(snapshot[15] == base + APPLE_DXE_IMPORT_OFFSET &&
                           (snapshot[16] & 0xff) == 0xf3);
    apple_dispatch_require(snapshot[13] >= 0x0afd8000 && snapshot[13] < 0x0aff8000);
    apple_dispatch_match(base + 0x380, dxe_stop, sizeof(dxe_stop));
    apple_dispatch_match(base + 0x5898, dxe_image, sizeof(dxe_image));
}

static inline uint32_t apple_driver_resume(uint32_t base)
{
    return base + 0x58a1;
}

static inline void apple_intercept_dispatch(uint32_t base, uint32_t stop,
                                            uint32_t image_entry)
{
    apple_dispatch_jump(base + 0x380, stop);
    apple_dispatch_jump(base + 0x5898, image_entry);
    for (unsigned i = 0; i < sizeof(dxe_import); ++i)
        *(volatile unsigned char *)(uintptr_t)(base + APPLE_DXE_IMPORT_OFFSET + i) =
            dxe_import[i];
}
