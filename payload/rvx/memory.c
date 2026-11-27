/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */

/* After the library import Apple grows the shared block cache (audio read-ahead,
 * eApp memory, block-backed heaps) to max(heap total - import cost - 14 MiB,
 * 14 MiB) and never shrinks it. Once the library costs more than about 24 MiB the
 * 14 MiB floor takes the heap the reserve was meant to keep free and the UI fails
 * its allocations. Hard disks keep the native sizes, since a smaller read-ahead
 * means more spin-ups. Flash refills at no spin-up cost, so it keeps a larger
 * reserve of free heap and a 6 MiB floor: 2 MiB of read-ahead, the UI's two
 * blocks, Now Playing art and four blocks for Cover Flow. */

#include "internal.h"
#include "memory.h"
#include "../../rvx-sdk/rv_api.h"
#include "../../rvx-sdk/rv_sites.h"
#include "../../rvx-sdk/osos-2.0.5.h"

#define NOP 0xe1a00000u

/* Sorted orders of the library that Apple builds itself during the import: 2
 * copies the database order, and 22, 26 and 29 serve the playlist import.
 * Nothing reads them while browsing and a later request rebuilds the same order,
 * so the import's end releases them (4 bytes per track each). Orders read from
 * the iTunesDB stay: a rebuild would sort them with Apple's collation instead of
 * the order iTunes wrote. */
static void release_import_orders(uint8_t *owner)
{
    static const uint8_t built_orders[] = {2, 22, 26, 29};
    struct MediaList **slot =
        owner ? RV_FIELD(owner, MediaBrowser_library, struct MediaList **) : 0;
    struct MediaList *library = slot ? *slot : 0;
    if (!library)
        return;
    for (u32 i = 0; i < sizeof built_orders; i++)
        NATIVE_CALL(MediaList_InvalidateOrderCache)(library, built_orders[i]);
}

/* Runs once the import has built every list, so it also drops what the import
 * alone used. */
static u32 block_cache_budget(u32 heap_total, u32 import_cost, u32 owner,
                              const void *next)
{
    (void)next;
    release_import_orders((uint8_t *)owner);
    import_cache_bytes =
        rvx_block_cache_size(heap_total, import_cost,
                             rvx_storage_is_flash((const uint16_t *)ADDR_ATA_Identify));
    return import_cache_bytes;
}

static const struct rvx_hook cache_budget_hooks[] = {
    {RV_SITE_BlockCache_ImportCost_word, (u32)block_cache_budget, 0,
     RV_HOOK_MODE_REPLACE, 0},
    {RV_SITE_BlockCache_ReserveNative_word, NOP, 0, RV_HOOK_MODE_REPLACE, 0},
    {RV_SITE_BlockCache_FloorTest_word, NOP, 0, RV_HOOK_MODE_REPLACE, 0},
    {RV_SITE_BlockCache_FloorSet_word, NOP, 0, RV_HOOK_MODE_REPLACE, 0},
};

/* Called with r0 = cache bytes - 10 MiB, as Apple computes it; may be negative. */
static u32 read_ahead(u32 over_reserve, const void *next)
{
    (void)next;
    return rvx_media_read_ahead(over_reserve + RVX_PLAYER_RESERVE);
}

static const struct rvx_hook read_ahead_hooks[] = {
    {RV_SITE_MediaPlayer_ReadAheadMinTest_word, (u32)read_ahead, 0,
     RV_HOOK_MODE_REPLACE, 0},
    {RV_SITE_MediaPlayer_ReadAheadMinSet_word, NOP, 0, RV_HOOK_MODE_REPLACE, 0},
};

static void bind_all(const struct rvx_hook *hooks, u32 count)
{
    for (u32 i = 0; i < count; i++)
        rvx_bind_core(&hooks[i]);
}

void rvx_memory_bind(void)
{
    bind_all(cache_budget_hooks, 4);
    bind_all(read_ahead_hooks, 2);
}
