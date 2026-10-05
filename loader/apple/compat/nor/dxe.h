/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include "s5l8702/layout.h"
#include <nor-target.h>

static inline uint32_t apple_dxe_base(uint32_t entry)
{
    return entry - 0x243;
}

static inline uint32_t apple_get_memory_map(uint32_t base, uint32_t *bytes, void *map,
                                            uint32_t *key, uint32_t *stride,
                                            uint32_t *version)
{
    typedef uint32_t (*fn)(uint32_t *, void *, uint32_t *, uint32_t *, uint32_t *);
    return ((fn)(base + 0x2465))(bytes, map, key, stride, version);
}

static inline uint32_t apple_allocate_pages(uint32_t base, uint32_t type,
                                            uint32_t memory_type, uint32_t pages,
                                            uint64_t *address)
{
    typedef uint32_t (*fn)(uint32_t, uint32_t, uint32_t, uint64_t *);
    return ((fn)(base + 0x220d))(type, memory_type, pages, address);
}

static inline void apple_enter_dxe(uint32_t entry, uint32_t hob, uint32_t stack,
                                   uint32_t zero)
{
    typedef void (*fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    ((fn)APPLE_DXE_ENTER)(entry, hob, stack, zero);
}
