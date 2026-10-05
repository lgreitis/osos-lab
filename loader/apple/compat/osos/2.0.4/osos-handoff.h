/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "../../../../../payload/layout.h"

#if CFW_PAYLOAD_FILE_OFFSET - OSOS_HEADER_BYTES + CFW_PAYLOAD_BYTES > OSOS_BODY_CAPACITY
#error Expanded OSOS exceeds the staging area
#endif

static void copy(void *dest, const void *source, uint32_t bytes);
void probe_finish(uint32_t status) __attribute__((noreturn));

static inline void osos_prepare_entry(void)
{
    if (BOOT_BODY_BYTES !=
        CFW_PAYLOAD_FILE_OFFSET - OSOS_HEADER_BYTES + CFW_PAYLOAD_BYTES)
        probe_finish(3);
    /* Install code, initialized data and zero-filled BSS before OSOS starts. */
    copy((void *)CFW_PAYLOAD_BASE,
         (const void *)((OSOS_STAGE | 0x80000000u) + CFW_PAYLOAD_FILE_OFFSET -
                        OSOS_HEADER_BYTES),
         CFW_PAYLOAD_BYTES);
}
