/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include "fingerprints.h"
#ifndef PTR
#define PTR(a) ((void *)(uintptr_t)(a))
#endif
#define REC ((volatile uint32_t *)PTR(0x0bb30000))
extern void probe_finish(uint32_t) __attribute__((noreturn));
extern void probe_flush(void);
extern void native_image_entry(void);
extern void native_stop(void);
extern void enter_osos(uint32_t) __attribute__((noreturn));
extern void restore_import(void) __attribute__((noreturn));

static uint32_t rd(uint32_t address)
{
    return *(volatile uint32_t *)PTR(address);
}

static void require(int ok)
{
    if (!ok)
        probe_finish(4);
}

static void match(uint32_t address, const unsigned char *bytes, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        require(*(volatile unsigned char *)PTR(address + i) == bytes[i]);
}

static void jump(uint32_t address, uint32_t target)
{
    require(!(address & 3));
    *(volatile uint32_t *)PTR(address) = 0x47184b00; /* ldr r3; bx r3 */
    *(volatile uint32_t *)PTR(address + 4) = target;
}

/* The trampoline calls this before each original module entry. */
void native_prepare_image(uint32_t image)
{
    require(image >= 0x08000000 && image < 0x0affff00 && !(image & 3));
    uint32_t base = rd(image + 0x38), entry = rd(image + 0x10);
    require(base >= 0x08000000 && base < 0x0aff0000 && !(base & 0xfff));
    require(rd(base + 0x3c) == 0x80 && rd(base + 0x80) == 0x4550);
    uint32_t size = rd(base + 0xd0), rva = rd(base + 0xa8);
    require(size >= 0x220 && size <= 0x10000 && rva < size);
    require(entry == base + rva && (entry & 1));
    if (rva == SST_ENTRY && size == SST_SIZE) {
        match(base + 0x522, sst_unlock, sizeof(sst_unlock));
        /* Keep SPI setup/read-status and protocol installation. Skip EWSR
         * and WRSR(0), which otherwise clear flash block protection. */
        *(volatile uint16_t *)PTR(base + 0x522) = 0xe02f; /* b base+0x584 */
        probe_flush();
    }
}

/* Continue to OSOS after native driver dispatch returns at DXE+380. */
void native_report(void)
{
    enter_osos(REC[0]);
}

void native_resume(uint32_t base)
{
    require(base >= 0x08000000 && base < 0x0aff0000 && !(base & 0xfff));
    const volatile uint32_t *snapshot = (const volatile uint32_t *)0x0bb31000;
    require(snapshot[15] == base + 0x290 && (snapshot[16] & 0xff) == 0xf3);
    require(snapshot[13] >= 0x0afd8000 && snapshot[13] < 0x0aff8000);
    match(base + 0x380, dxe_stop, sizeof(dxe_stop));
    match(base + 0x5898, dxe_image, sizeof(dxe_image));
    REC[0] = base;
    REC[7] = base + 0x58a1;
    jump(base + 0x380, (uint32_t)(uintptr_t)native_stop);
    jump(base + 0x5898, (uint32_t)(uintptr_t)native_image_entry);
    for (unsigned i = 0; i < sizeof(dxe_import); ++i)
        *(volatile unsigned char *)(uintptr_t)(base + 0x290 + i) = dxe_import[i];
    probe_flush();
    restore_import();
}
