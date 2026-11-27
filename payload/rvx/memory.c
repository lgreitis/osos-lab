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

/* The size chosen after the import. */
static u32 import_cache_bytes;

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

/* Apple rounds fixed-pool slots to 16 bytes; 8 keeps the 36-byte ArtworkDB
 * thumbnail records in 40-byte slots instead of 48. */
static const struct rvx_hook pool_hooks[] = {
    {RV_SITE_FixedPool_SlotRoundAdd_word, 0xe2840007u, 0, RV_HOOK_MODE_REPLACE,
     0}, /* add r0, r4, #7 */
    {RV_SITE_FixedPool_SlotRoundMask_word, 0xe3c00007u, 0, RV_HOOK_MODE_REPLACE,
     0}, /* bic r0, r0, #7 */
};

/* Client ring: each node {next, previous, client **}. Clients with a weight share
 * their blocks with the rebalancer; the others keep max(held, desired). */
static u32 kept_blocks(uint8_t *cache, uint8_t *skip)
{
    u32 block_bytes = RV_FIELD(cache, BlockCache_block_bytes, u32);
    u32 kept = 0;
    void **ring = RV_FIELD(cache, BlockCache_clients, void **);
    for (void **node = ring[0]; node != ring; node = node[0]) {
        uint8_t *client = *(uint8_t **)node[2];
        if (!client || client == skip || RV_FIELD(client, BlockCacheClient_weight, u32))
            continue;
        u32 desired =
            (RV_FIELD(client, BlockCacheClient_desired_bytes, u32) + block_bytes - 1) /
            block_bytes;
        u32 held = RV_FIELD(client, BlockCacheClient_block_count, u32);
        kept += held > desired ? held : desired;
    }
    return kept;
}

static int grown_for_game;

/* Before Apple's acquire: grow the cache so the game's blocks fit beside what the
 * other clients keep, as far as the heap allows. */
static u32 game_blocks_acquire(u32 manager, const void *next)
{
    uint8_t *cache = NATIVE_CALL(BlockCache_Get)();
    u32 block_bytes = cache ? RV_FIELD(cache, BlockCache_block_bytes, u32) : 0;
    if (block_bytes) {
        uint8_t **client_slot =
            RV_FIELD((uint8_t *)manager, EAppBlockManager_client, uint8_t **);
        NATIVE_CALL(Mutex_Lock)(cache + BlockCache_lock);
        u32 total = RV_FIELD(cache, BlockCache_total_blocks, u32);
        u32 kept = kept_blocks(cache, client_slot ? *client_slot : 0);
        NATIVE_CALL(Mutex_Unlock)(cache + BlockCache_lock);
        u32 request =
            RV_FIELD((uint8_t *)manager, EAppBlockManager_request_blocks, u32);
        u32 add = rvx_block_cache_shortfall(
            total, kept, request,
            RV_FIELD(*(void **)ADDR_MainHeap, MainHeap_free_bytes, u32), block_bytes);
        if (add) {
            NATIVE_CALL(BlockCache_Grow)((total + add) * block_bytes);
            grown_for_game = 1;
        }
    }
    return rvx_next(next, manager, 0, 0, 0, 0, 0);
}

/* After Apple's release: free unassigned blocks above the import size, as
 * BlockCache_Trim does but without waiting for pending requests, which the
 * rebalancer is still serving while the game's blocks come back. */
static u32 game_blocks_release(u32 manager, const void *next)
{
    u32 result = rvx_next(next, manager, 0, 0, 0, 0, 0);
    uint8_t *cache = NATIVE_CALL(BlockCache_Get)();
    u32 block_bytes = cache ? RV_FIELD(cache, BlockCache_block_bytes, u32) : 0;
    if (!grown_for_game || !block_bytes)
        return result;
    grown_for_game = 0;
    NATIVE_CALL(Mutex_Lock)(cache + BlockCache_lock);
    u32 keep = import_cache_bytes / block_bytes;
    u32 total = RV_FIELD(cache, BlockCache_total_blocks, u32);
    u32 extra = total > keep ? total - keep : 0;
    u32 free_before = RV_FIELD(cache, BlockCache_free_count, u32);
    if (extra > free_before)
        extra = free_before;
    /* Each free node is {tag, next, previous, block}; the list head has the same
     * shape with the node count at +0x10. Erase takes the address of the variable
     * holding the node, checks the node's tag against the list's, unlinks and
     * frees it, runs the block's destructor, and stores the next node through
     * out_next; passing the same variable for both walks the list. */
    void *list = cache + BlockCache_free_blocks;
    void *node = RV_FIELD(cache, BlockCache_free_blocks + 4, void *); /* list.first */
    for (u32 i = 0; i < extra && node != list; i++)
        NATIVE_CALL(BlockList_Erase)(&node, list, &node);
    RV_FIELD(cache, BlockCache_total_blocks, u32) -=
        free_before - RV_FIELD(cache, BlockCache_free_count, u32);
    NATIVE_CALL(BlockCache_PostRebalance)(cache, 0);
    NATIVE_CALL(Mutex_Unlock)(cache + BlockCache_lock);
    return result;
}

static const struct rvx_hook game_hooks[] = {
    {RV_SITE_EAppBlocks_Acquire_call, (u32)game_blocks_acquire, 0, RV_HOOK_MODE_FILTER,
     0},
    {RV_SITE_EAppBlocks_Release_call, (u32)game_blocks_release, 0, RV_HOOK_MODE_FILTER,
     0},
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
    bind_all(pool_hooks, 2);
    bind_all(game_hooks, 2);
}
