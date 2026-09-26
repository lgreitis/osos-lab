/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_PANIC_H
#define CFW_PANIC_H

/* Shared with panic.S. Offsets are bytes from cfw_panic_frame. */
#define PANIC_EXLR 52
#define PANIC_TYPE 56
#define PANIC_CPSR 60
#define PANIC_SPSR 64
#define PANIC_DFSR 68
#define PANIC_DFAR 72
#define PANIC_IFSR 76
#define PANIC_VALID 80
#define PANIC_SP 84
#define PANIC_STACK_COUNT 88
#define PANIC_STACK 92
#define PANIC_STACK_WORDS 12
#define PANIC_FRAME_BYTES (PANIC_STACK + PANIC_STACK_WORDS * 4)

#ifndef __ASSEMBLER__
#include <stdint.h>

struct panic_frame {
    uint32_t registers[13];
    uint32_t exception_lr;
    uint32_t type;
    uint32_t cpsr;
    uint32_t spsr;
    uint32_t dfsr;
    uint32_t dfar;
    uint32_t ifsr;
    uint32_t valid;
    uint32_t sp;
    uint32_t stack_count;
    uint32_t stack[PANIC_STACK_WORDS];
};

void cfw_panic_screen(const struct panic_frame *frame) __attribute__((noreturn));
#endif
#endif
