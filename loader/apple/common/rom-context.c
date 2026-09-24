/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"

#define ROM 0x2201c300u
#define CLOCK 0x2201d900u
#define CPU 0x2201e180u
#define IRQ 0x2201eb80u
#define HEADER 0x22036000u
#define ARENA_BYTES 0x5000u
#define FIRST_BYTES 0x73cu
#define SECOND_BYTES 0x2764u
#define REG(a) (*(volatile uint32_t *)(a))

#ifdef STARTUP_CONTEXT
#define ARENA 0x8ae00000u
#define BODY 0x8b000000u
#else
#define ARENA 0x8ad00000u
#define BODY 0x88000000u
#endif
#define FIRST (ARENA + 0x100u)
#define SECOND (ARENA + 0x1100u)

extern void probe_finish(uint32_t) __attribute__((noreturn));
extern void probe_flush(void);

struct context {
    uint32_t cpu[4], clock[3], irq[1], allocator[5];
    uint32_t allocations, usb_context, eint;
};

#define C ((struct context *)0x22035000u)
typedef char context_fits[sizeof(struct context) <= 0x1000 ? 1 : -1];

static uint32_t unsupported(void)
{
    return 0x80000003u;
}

static uint32_t alloc(void *self, uint32_t bytes)
{
    (void)self;
    if (C->allocations >= 2 || bytes != (C->allocations ? SECOND_BYTES : FIRST_BYTES))
        probe_finish(9);
    return C->allocations++ ? SECOND : FIRST;
}

/* BootROM pointers and DMA registers retain references to these buffers. */
static uint32_t retain(void *self, void *block)
{
    (void)self;
    (void)block;
    return 0;
}

static uint32_t cpu_state(void *self, unsigned char *enabled)
{
    typedef uint32_t (*fn)(void *, unsigned char *);
    return ((fn)(CPU + 0x26f))(self, enabled);
}

static uint32_t cpu_disable(void *self)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)(CPU + 0x25d))(self);
}

static uint32_t frequency(void *self, uint32_t selector)
{
    typedef uint32_t (*fn)(void *, uint32_t);
    return ((fn)(CLOCK + 0x2cb))(self, selector);
}

static uint32_t gates(void *self, uint64_t mask, uint32_t enable)
{
    typedef uint32_t (*fn)(void *, uint64_t, uint32_t);
    return ((fn)(CLOCK + 0x2cf))(self, mask, enable);
}

static uint32_t mapping(void *self, uint32_t irq, uint32_t *bank, uint32_t *bit)
{
    typedef uint32_t (*fn)(void *, uint32_t, uint32_t *, uint32_t *);
    return ((fn)(IRQ + 0x221))(self, irq, bank, bit);
}

/* The relocated ROM module enters here after installing its hardware context. */
void rom_plaintext_gate(const void *header, const void *body, uint32_t mode,
                        uint32_t target)
{
#ifdef STARTUP_CONTEXT
    if (REG(0x22039010) == 2) {
        typedef void (*fn)(const void *, const void *, uint32_t, uint32_t);
        ((fn)REG(0x2203900c))(header, body, mode, target);
        return;
    }
#endif
    (void)header;
    (void)body;
    (void)mode;
    (void)target;
    probe_flush();
}

#ifdef STARTUP_CONTEXT
void prepare_startup(uint32_t dxe_base)
#else
void prepare_loaded(uint32_t dxe_base)
#endif
{
#ifdef STARTUP_CONTEXT
    REG(0x22039010) = 0;
#endif
    for (unsigned i = 0; i < sizeof(*C); i++)
        ((unsigned char *)C)[i] = 0;
    typedef uint32_t (*pages_fn)(uint32_t, uint32_t, uint32_t, uint64_t *);
    uint64_t address = ARENA & 0x7fffffffu;
    uint32_t result =
        ((pages_fn)(dxe_base + 0x220d))(2, 4, ARENA_BYTES / 0x1000, &address);
    if (result || address != (ARENA & 0x7fffffffu))
        probe_finish(4);
    probe_flush();
    for (unsigned i = 0; i < FIRST_BYTES; i++)
        *(volatile unsigned char *)(FIRST + i) = 0;
    for (unsigned i = 0; i < SECOND_BYTES; i++)
        *(volatile unsigned char *)(SECOND + i) = 0;
#ifdef STARTUP_CONTEXT
    for (unsigned i = 0; i < OSOS_HEADER_BYTES; i++)
        *(unsigned char *)(HEADER + i) = 0;
    REG(HEADER) = 0x32303738;
    REG(HEADER + 4) = 0x02302e31;
    REG(HEADER + 12) = REG(HEADER + 16) = REG(HEADER + 20) = BOOT_BODY_BYTES;
#endif
    C->cpu[0] = C->cpu[1] = (uint32_t)unsupported;
    C->cpu[2] = (uint32_t)cpu_disable;
    C->cpu[3] = (uint32_t)cpu_state;
    C->clock[0] = (uint32_t)frequency;
    C->clock[1] = (uint32_t)gates;
    C->clock[2] = (uint32_t)unsupported;
    C->irq[0] = (uint32_t)mapping;
    for (unsigned i = 0; i < 5; i++)
        C->allocator[i] = (uint32_t)unsupported;
    C->allocator[1] = (uint32_t)alloc;
    C->allocator[4] = (uint32_t)retain;
    REG(ROM + 0x13b4) = (uint32_t)C->cpu;
    REG(ROM + 0x13b8) = (uint32_t)C->clock;
    REG(ROM + 0x13bc) = (uint32_t)C->irq;
    REG(ROM + 0x13c0) = (uint32_t)C->allocator;
    C->usb_context = REG(0x2203fffc);
    C->eint = REG(0x39a000c0);
    probe_flush();
#ifndef STARTUP_CONTEXT
    REG(0x2203900c) = (uint32_t)rom_plaintext_gate;
    REG(0x22039010) = 2;
#endif
    typedef uint32_t (*prepare_fn)(void *, const void *, const void *);
    result = ((prepare_fn)(ROM + 0x7f5))((void *)(ROM + 0x13cc), (void *)HEADER,
                                         (void *)BODY);
#ifndef STARTUP_CONTEXT
    REG(0x22039010) = 0;
#endif
    if (result)
        probe_finish(4);
    REG(0x39a000c0) = C->eint;
    REG(0x2203fffc) = C->usb_context;
    probe_flush();
#ifdef STARTUP_CONTEXT
    REG(0x22039000) = (uint32_t)probe_finish;
    REG(0x22039004) = (uint32_t)probe_flush;
    probe_flush();
    ((void (*)(uint32_t))0x0bb28001)(dxe_base);
    probe_finish(9);
#endif
}
