/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

void probe_finish(uint32_t status) __attribute__((noreturn));

/* Initial NOR bring-up enters the original decrypted OSOS without a CFW payload. */
static inline void osos_prepare_entry(void)
{
    if (BOOT_BODY_BYTES != 0xa06640)
        probe_finish(3);
}
