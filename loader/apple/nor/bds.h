/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include "s5l8702/layout.h"

static inline uint32_t apple_check_image_version(void *self, uint32_t hardware,
                                                 uint32_t image)
{
    typedef uint32_t (*fn)(void *, uint32_t, uint32_t);
    return ((fn)(APPLE_VERSION_BASE + 1))(self, hardware, image);
}

/* Ordering matches the protocol tables supplied by the companion. */
static inline const void *apple_bds_protocol_guid(unsigned index)
{
    static const uint32_t offsets[] = {0x136c, 0x135c, 0x130c, 0x13ac, 0x127c, 0x133c};
    return (const void *)(APPLE_BDS_BASE + offsets[index]);
}

static inline void apple_bds_bind(void *boot_services)
{
    *(volatile uint32_t *)(APPLE_BDS_BASE + 0x13d0) = (uint32_t)boot_services;
}

static inline void apple_bds_load(void *file)
{
    typedef uint32_t (*fn)(void *, uint32_t, uint32_t);
    ((fn)(APPLE_BDS_BASE + 0x2e7))(file, 0, 1);
}
