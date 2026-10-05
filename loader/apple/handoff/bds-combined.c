/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"
#include "compat/nor/bds.h"
#include "compat/nor/dxe.h"
#include "compat/osos/handoff.h"

#define HEADER ((unsigned char *)0x22036000u)
#define SYSINFO ((unsigned char *)0x22034000u)
#define VERSION 0x00015000u
extern void probe_finish(uint32_t) __attribute__((noreturn));
extern void probe_flush(void);
extern void prepare_loaded(uint32_t);

struct context {
    uint32_t bs[50], file[16], sys[2], version[2], allocator[5], cpu[4], irq[2], rom[3];
    uint32_t dxe, position;
};

#define C ((struct context *)0x22037000u)
typedef char context_fits[sizeof(struct context) <= 0x1000 ? 1 : -1];

static uint32_t unsupported(void)
{
    return 0x80000003u;
}

static void copy(void *dest, const void *source, uint32_t bytes)
{
    unsigned char *out = dest;
    const unsigned char *in = source;
    while (bytes--)
        *out++ = *in++;
}

static int equal(const void *a, const void *b, uint32_t bytes)
{
    const unsigned char *x = a, *y = b;
    while (bytes--)
        if (*x++ != *y++)
            return 0;
    return 1;
}

static uint32_t version(void *self, uint32_t hardware, uint32_t image)
{
    return apple_check_image_version(self, hardware, image);
}

static uint32_t header_alloc(void *self, uint32_t bytes, uint32_t align)
{
    (void)self;
    (void)align;
    return bytes <= OSOS_HEADER_BYTES ? (uint32_t)HEADER : 0;
}

static uint32_t flush(void *self, uint64_t address, uint64_t length, uint32_t operation)
{
    /* Bds passes uninitialized address/length values in its first two calls. */
    (void)self;
    (void)address;
    (void)length;
    (void)operation;
    probe_flush();
    return 0;
}

static uint32_t irq_disable(void *self)
{
    (void)self;
    return 0; /* IRQ/FIQ are masked by enter_osos. */
}

static uint32_t locate(const void *guid, void *registration, void **out)
{
    (void)registration;
    void *tables[] = {C->sys, C->version, C->allocator, C->cpu, C->irq, C->rom};
    for (unsigned i = 0; i < 6; i++) {
        if (equal(guid, apple_bds_protocol_guid(i), 16)) {
            *out = tables[i];
            return 0;
        }
    }
    *out = 0;
    return 0x8000000eu;
}

static uint32_t info(void *self, const void *guid, uint32_t *size, void *output)
{
    (void)self;
    (void)guid;
    uint32_t metadata[] = {0, VERSION, 0x200, 0x08000000,
                           OSOS_HEADER_BYTES + BOOT_BODY_BYTES};
    if (*size < sizeof(metadata))
        return 0x80000005u;
    copy(output, metadata, sizeof(metadata));
    *size = sizeof(metadata);
    return 0;
}

static uint32_t seek(void *self, uint64_t position)
{
    (void)self;
    if (position > OSOS_HEADER_BYTES + BOOT_BODY_BYTES)
        return 0x80000002u;
    C->position = position;
    return 0;
}

static uint32_t read_file(void *self, uint32_t *size, void *dest)
{
    (void)self;
    uint32_t available = OSOS_HEADER_BYTES + BOOT_BODY_BYTES - C->position;
    if (*size > available)
        *size = available;
    unsigned char *out = dest;
    for (unsigned i = 0; i < *size; i++) {
        uint32_t pos = C->position + i;
        uint32_t source = pos < OSOS_HEADER_BYTES
                              ? (STAGED_HEADER | 0x80000000u) + pos
                              : (OSOS_STAGE | 0x80000000u) + pos - OSOS_HEADER_BYTES;
        out[i] = *(const volatile unsigned char *)source;
    }
    C->position += *size;
    return 0;
}

static uint32_t header_check(void *self, const void *header, uint32_t key)
{
    (void)self;
    (void)header;
    (void)key;
    return 0; /* The supplied image is already decrypted. */
}

static uint32_t allocate(uint32_t type, uint32_t memory_type, uint32_t pages,
                         uint64_t *address)
{
    return apple_allocate_pages(C->dxe, type, memory_type, pages, address);
}

static uint32_t body_ready(void *self, const void *header, const void *body)
{
    (void)self;
    (void)header;
    (void)body;
    probe_flush();
    prepare_loaded(C->dxe);
    return 0;
}

static void *copy_bootinfo(void *dest, const void *source, uint32_t bytes)
{
    copy(dest, source, bytes);
    return dest;
}

void after_bds(uint32_t entry)
{
    (void)entry;
    osos_prepare_entry();
    probe_flush();
}

void load_osos(uint32_t dxe_base)
{
    for (unsigned i = 0; i < sizeof(*C); i++)
        ((unsigned char *)C)[i] = 0;
    C->dxe = dxe_base;
    for (unsigned i = 0; i < 50; i++)
        C->bs[i] = (uint32_t)unsupported;
    for (unsigned i = 0; i < 16; i++)
        C->file[i] = (uint32_t)unsupported;
    C->bs[0x20 / 4] = (uint32_t)allocate;
    C->bs[0xac / 4] = (uint32_t)locate;
    C->bs[0xbc / 4] = (uint32_t)copy_bootinfo;
    C->file[0] = C->file[1] = 0;
    C->file[0x14 / 4] = (uint32_t)read_file;
    C->file[0x20 / 4] = (uint32_t)seek;
    C->file[0x24 / 4] = (uint32_t)info;
    C->sys[0] = (uint32_t)SYSINFO;
    C->sys[1] = 0x120;
    C->version[0] = (uint32_t)version;
    C->allocator[3] = (uint32_t)header_alloc;
    C->allocator[4] = (uint32_t)unsupported;
    C->cpu[0] = (uint32_t)flush;
    C->irq[0] = (uint32_t)irq_disable;
    C->irq[1] = (uint32_t)unsupported;
    C->rom[0] = (uint32_t)unsupported;
    C->rom[1] = (uint32_t)body_ready;
    C->rom[2] = (uint32_t)header_check;
    copy(SYSINFO, (const void *)0x8bb25900, 0x120);
    apple_bds_bind(C->bs);
    probe_flush();
    apple_bds_load(C->file);
    probe_finish(4);
}
