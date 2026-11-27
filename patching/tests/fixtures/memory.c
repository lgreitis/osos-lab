/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include "../../../payload/rvx/memory.h"

#define MIB (1024u * 1024u)

int main(void)
{
    uint32_t heap = 54316592u;

    /* Hard disks: Apple's rule, max(heap - import - 14 MiB, 14 MiB). */
    assert(rvx_block_cache_size(heap, 10 * MIB, 0) == heap - 24 * MIB);
    assert(rvx_block_cache_size(heap, 30 * MIB, 0) == 14 * MIB);
    assert(rvx_block_cache_size(heap, heap, 0) == 14 * MIB);
    assert(rvx_block_cache_size(heap, heap + MIB, 0) == 14 * MIB);

    /* Flash: a 24 MiB reserve and a 6 MiB floor. */
    assert(rvx_block_cache_size(heap, 10 * MIB, 1) == heap - 34 * MIB);
    assert(rvx_block_cache_size(heap, 30 * MIB, 1) == 6 * MIB);
    assert(rvx_block_cache_size(heap, heap, 1) == 6 * MIB);

    /* Read-ahead: Apple's max(cache - 10 MiB, 6 MiB) while a game's 8 MiB still
     * fits beside the minimum, else the minimum shrinks down to 2 MiB. */
    assert(rvx_media_read_ahead(20 * MIB) == 10 * MIB);
    assert(rvx_media_read_ahead(14 * MIB) == 6 * MIB);
    assert(rvx_media_read_ahead(13 * MIB) == 5 * MIB);
    assert(rvx_media_read_ahead(10 * MIB) == 2 * MIB);
    assert(rvx_media_read_ahead(6 * MIB) == 2 * MIB);
    assert(rvx_media_read_ahead(0) == 2 * MIB);

    /* Growing for a game: the shortfall, capped by the heap above a 1 MiB margin. */
    uint32_t block = 512 * 1024;
    assert(rvx_block_cache_shortfall(28, 10, 16, 40 * MIB, block) == 0);
    assert(rvx_block_cache_shortfall(12, 10, 16, 40 * MIB, block) == 14);
    assert(rvx_block_cache_shortfall(12, 10, 16, 4 * MIB, block) == 6);
    assert(rvx_block_cache_shortfall(12, 10, 16, MIB, block) == 0);
    assert(rvx_block_cache_shortfall(12, 10, 16, 40 * MIB, 0) == 0);
    return 0;
}
