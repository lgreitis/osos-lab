/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#define RVX_HOOK_SLOTS 224

#ifndef __ASSEMBLER__
#include "runtime.h"

extern void *rvx_chains[RVX_HOOK_SLOTS];
extern uint32_t rvx_insert_slot;
uint32_t rvx_dispatch(uint32_t *registers, unsigned slot);

struct rvx_host {
    void (*boot)(void), (*register_ui)(void *), (*flush)(void), (*reset)(void);
    void (*hold)(int);
    void (*row)(void *, uint32_t, uint32_t, const void *, uint32_t, uint32_t);
    void (*events)(void *, uint32_t), (*video)(void *, void *, int, const void *, int);
    int (*property)(uint32_t, uint32_t, uintptr_t *);
    unsigned (*visible)(unsigned);
    int (*visibility)(unsigned, unsigned, uint32_t, int, uintptr_t *);
    void (*toggle)(unsigned, unsigned);
    uint32_t (*dispatch)(uint32_t *, unsigned);
};
#endif
