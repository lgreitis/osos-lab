/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"

extern void probe_finish(uint32_t) __attribute__((noreturn));

/* Sources follow destinations when the OSOS body overlaps its file header. */
void assemble_files(unsigned char *stage, const unsigned char *companion,
                    uint32_t body_bytes)
{
    unsigned char header[OSOS_HEADER_BYTES];
    for (unsigned i = 0; i < sizeof(header); i++)
        header[i] = stage[i];
    for (unsigned i = 0; i < body_bytes; i++)
        stage[i] = stage[i + OSOS_HEADER_BYTES];
    for (unsigned i = body_bytes; i < OSOS_BODY_CAPACITY; i++)
        stage[i] = 0;
    for (unsigned i = 0; i < EXTENSION_BYTES; i++)
        stage[OSOS_BODY_CAPACITY + i] = companion[BOOTSTRAP_BYTES + i];
    for (unsigned i = 0; i < sizeof(header); i++)
        stage[STAGED_HEADER - OSOS_STAGE + i] = header[i];
}

int valid_files(uint32_t osos, uint32_t osos_bytes, uint32_t companion,
                uint32_t companion_bytes)
{
    return osos == OSOS_STAGE && companion == COMPANION_STAGE &&
           osos_bytes > OSOS_HEADER_BYTES &&
           osos_bytes - OSOS_HEADER_BYTES <= OSOS_BODY_CAPACITY &&
           companion_bytes >= BOOTSTRAP_BYTES + EXTENSION_BYTES &&
           companion_bytes <= 0x0bffc000u - COMPANION_STAGE;
}

void prepare_files(uint32_t osos, uint32_t osos_bytes, uint32_t companion,
                   uint32_t companion_bytes)
{
    if (!valid_files(osos, osos_bytes, companion, companion_bytes))
        probe_finish(3);
    BOOT_BODY_BYTES = osos_bytes - OSOS_HEADER_BYTES;
    assemble_files((unsigned char *)osos, (const unsigned char *)companion,
                   BOOT_BODY_BYTES);
}
