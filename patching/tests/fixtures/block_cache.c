/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM

static uint16_t identify[256];

static const uint16_t *osos_ata_identify(void)
{
    return identify;
}

static void *fake_cache;
static uint32_t heap_free_bytes, grow_requests[4], grow_count, lock_depth;
static unsigned acquired, released, rebalanced;

static void osos_mutex_lock(void *mutex)
{
    (void)mutex;
    lock_depth++;
}

static void osos_mutex_unlock(void *mutex)
{
    (void)mutex;
    lock_depth--;
}

static uint32_t osos_heap_free_bytes(void)
{
    return heap_free_bytes;
}

static void *osos_block_cache(void)
{
    return fake_cache;
}

static int osos_block_cache_grow(uint32_t bytes);
static void *osos_block_list_erase(void *list, void *node);

static void osos_block_cache_rebalance(void *cache)
{
    (void)cache;
    assert(lock_depth == 1);
    rebalanced++;
}

static void osos_eapp_blocks_acquire(void *manager)
{
    (void)manager;
    assert(lock_depth == 0);
    acquired++;
}

static void osos_eapp_blocks_release(void *manager)
{
    (void)manager;
    released++;
}

#include "../../../payload/block_cache.c"

static struct block_cache cache;
static struct block_cache_node sentinel, nodes[3];
static struct block_cache_client clients[3], game_client;
static struct block_cache_client *client_slots[3], *game_slot = &game_client;

/* The free list: a ring through the list head, as the native list keeps it. */
struct fake_node {
    void *tag;
    struct fake_node *next, *previous;
};

static struct fake_node free_nodes[32];
static uint32_t free_used;

static void push_free_block(void)
{
    struct fake_node *node = &free_nodes[free_used++];
    struct fake_node *head = (struct fake_node *)&cache.free_blocks;
    node->next = head;
    node->previous = cache.free_blocks.last;
    ((struct fake_node *)cache.free_blocks.last)->next = node;
    cache.free_blocks.last = node;
    if (cache.free_blocks.first == head)
        cache.free_blocks.first = node;
    cache.free_blocks.count++;
}

static void reset_free_blocks(void)
{
    free_used = 0;
    cache.free_blocks.first = cache.free_blocks.last = &cache.free_blocks;
    cache.free_blocks.count = 0;
}

static void *osos_block_list_erase(void *list, void *node)
{
    assert(lock_depth == 1);
    assert(list == &cache.free_blocks && node != list);
    struct fake_node *erased = node;
    struct fake_node *head = list;
    if (erased->previous == NULL || (void *)erased->previous == list)
        cache.free_blocks.first = erased->next;
    else
        erased->previous->next = erased->next;
    if ((void *)erased->next != list)
        erased->next->previous = erased->previous;
    else
        cache.free_blocks.last = erased->previous;
    (void)head;
    cache.free_blocks.count--;
    return erased->next;
}

static int osos_block_cache_grow(uint32_t bytes)
{
    assert(lock_depth == 0);
    grow_requests[grow_count++] = bytes;
    uint32_t blocks = bytes / cache.block_bytes;
    while (cache.total_blocks < blocks) {
        cache.total_blocks++;
        push_free_block();
    }
    return 1;
}

static void link_clients(void)
{
    cache.block_bytes = 512 * 1024;
    cache.clients = &sentinel;
    struct block_cache_node *previous = &sentinel;
    for (int i = 0; i < 3; i++) {
        client_slots[i] = &clients[i];
        nodes[i].client = &client_slots[i];
        previous->next = &nodes[i];
        nodes[i].previous = previous;
        previous = &nodes[i];
    }
    previous->next = &sentinel;
    sentinel.previous = previous;
}

#define MIB (1024u * 1024u)

static void set_model(const char *model)
{
    char padded[40];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, model, strlen(model));
    for (int i = 0; i < 20; i++)
        identify[27 + i] =
            (uint16_t)((uint8_t)padded[2 * i] << 8 | (uint8_t)padded[2 * i + 1]);
}

static void reset(uint16_t config, uint16_t command_set_2, uint16_t rate,
                  const char *model)
{
    memset(identify, 0, sizeof(identify));
    identify[0] = config;
    identify[83] = command_set_2;
    identify[217] = rate;
    set_model(model);
}

int main(void)
{
    /* QEMU's IDE disk, as read from guest memory: it advertises TRIM. */
    reset(0x0040, 0x7400, 0, "QEMU HARDDISK");
    identify[169] = 1;
    assert(cfw_storage_is_flash(identify));
    /* A reported spindle speed is a hard disk whatever the model. */
    identify[217] = 5400;
    assert(!cfw_storage_is_flash(identify));

    /* Apple's hard disks, with and without the vendor name, report no rate. */
    reset(0x0040, 0x7400, 0, "TOSHIBA MK1634GAL");
    identify[168] = 0x0004; /* 1.8 inch */
    assert(!cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "MK8022GAA");
    assert(!cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "SAMSUNG HS12YHA");
    assert(!cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "HS081HA");
    assert(!cfw_storage_is_flash(identify));
    /* Word 83 without the validity pattern is ignored. */
    reset(0x0040, 0x0004, 0, "MK1634GAL");
    assert(!cfw_storage_is_flash(identify));

    /* iFlash reports no rotation rate but sets the CompactFlash feature bit. */
    reset(0x0040, 0x7404, 0, "iFlash-Platform iPod Adapters");
    assert(cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "iFlash-Platform iPod Adapters");
    assert(cfw_storage_is_flash(identify));

    reset(0x0040, 0x7400, 0x0001, "TOSHIBA THNSNJ128");
    assert(cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0x0100, "CF card");
    assert(cfw_storage_is_flash(identify));
    reset(0x848a, 0, 0, "CF card");
    assert(cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "MK1634GAL");
    identify[168] = 0x0009; /* M.2 */
    assert(cfw_storage_is_flash(identify));
    reset(0x0040, 0x7400, 0, "MK1634GAL");
    identify[163] = 0x0012;
    assert(cfw_storage_is_flash(identify));

    /* An adapter without any marker is not one of Apple's disks. */
    reset(0x0040, 0x7400, 0, "SD-ZIF ADAPTER");
    assert(cfw_storage_is_flash(identify));
    /* Missing IDENTIFY data keeps the native size. */
    reset(0, 0, 0, "");
    memset(identify, 0, sizeof(identify));
    assert(!cfw_storage_is_flash(identify));

    uint32_t heap = 54169568;
    /* Small library: the native size. */
    assert(cfw_block_cache_size(heap, 2 * MIB, 0) == heap - 2 * MIB - 14 * MIB);
    /* Large library on a hard disk: the native 14 MiB floor. */
    assert(cfw_block_cache_size(heap, 30 * MIB, 0) == 14 * MIB);
    assert(cfw_block_cache_size(heap, heap, 0) == 14 * MIB);
    assert(cfw_block_cache_size(heap, heap + 1, 0) == 14 * MIB);
    /* Flash keeps a larger reserve and a 6 MiB floor. */
    assert(cfw_block_cache_size(heap, 2 * MIB, 1) == heap - 2 * MIB - 24 * MIB);
    assert(cfw_block_cache_size(heap, 20 * MIB, 1) == heap - 20 * MIB - 24 * MIB);
    assert(cfw_block_cache_size(heap, 30 * MIB, 1) == 6 * MIB);
    assert(cfw_block_cache_size(heap, heap + 1, 1) == 6 * MIB);
    /* The 6 MiB floor gets the smallest read-ahead. */
    assert(cfw_media_player_buffer_size(6 * MIB) == 2 * MIB);

    /* At 14 MiB and above, the native read-ahead. */
    assert(cfw_media_player_buffer_size(14 * MIB) == 6 * MIB);
    assert(cfw_media_player_buffer_size(16 * MIB) == 6 * MIB);
    assert(cfw_media_player_buffer_size(40 * MIB) == 30 * MIB);
    /* Below 14 MiB, 8 MiB stays free for a game. */
    assert(cfw_media_player_buffer_size(10 * MIB) == 2 * MIB);
    assert(cfw_media_player_buffer_size(12 * MIB) == 4 * MIB);
    /* A cache that did not grow still gets the smallest read-ahead. */
    assert(cfw_media_player_buffer_size(8 * MIB) == 2 * MIB);
    assert(cfw_media_player_buffer_size(0) == 2 * MIB);
    /* The patched call receives cache - 10 MiB, which may be negative. */
    assert(cfw_media_player_buffer((int32_t)(10 * MIB) - (int32_t)(10 * MIB)) ==
           (int32_t)(2 * MIB));
    assert(cfw_media_player_buffer((int32_t)(30 * MIB)) == (int32_t)(30 * MIB));

    uint32_t block = 512 * 1024;
    /* Enough room: nothing to add. */
    assert(cfw_block_cache_shortfall(40, 8, 16, 10 * MIB, block) == 0);
    assert(cfw_block_cache_shortfall(24, 8, 16, 10 * MIB, block) == 0);
    /* The missing blocks, limited by the heap above the margin. */
    assert(cfw_block_cache_shortfall(12, 8, 16, 10 * MIB, block) == 12);
    assert(cfw_block_cache_shortfall(12, 8, 16, 4 * MIB, block) == 6);
    assert(cfw_block_cache_shortfall(12, 8, 16, 1 * MIB, block) == 0);
    assert(cfw_block_cache_shortfall(12, 8, 16, 0, block) == 0);
    assert(cfw_block_cache_shortfall(12, 8, 16, 10 * MIB, 0) == 0);

    /* A 6 MiB cache: media player 4 blocks, UI 2, Cover Flow (weighted) 5; 1 free. */
    link_clients();
    reset_free_blocks();
    cache.total_blocks = 12;
    push_free_block();
    clients[0].held_blocks = 4;
    clients[0].desired_bytes = 2 * MIB;
    clients[1].desired_bytes = 1 * MIB;
    clients[1].held_blocks = 2;
    clients[2].weight = 68;
    clients[2].held_blocks = 5;
    /* A stale request of the game's own client does not count. */
    game_client.desired_bytes = 8 * MIB;
    struct eapp_block_manager manager = {.client = &game_slot, .request_blocks = 16};
    fake_cache = &cache;
    import_cache_bytes = 6 * MIB;
    heap_free_bytes = 9 * MIB;
    cfw_game_blocks_acquire(&manager);
    assert(acquired == 1 && grow_count == 1 && grow_requests[0] == 22 * block);
    assert(cache.total_blocks == 22 && cache.free_blocks.count == 11);
    /* Release frees unassigned blocks down to the import size, then rebalances. */
    cfw_game_blocks_release(&manager);
    assert(released == 1 && rebalanced == 1 && lock_depth == 0);
    assert(cache.total_blocks == 12 && cache.free_blocks.count == 1);
    /* Without growth, release leaves the cache alone. */
    cfw_game_blocks_release(&manager);
    assert(released == 2 && rebalanced == 1 && cache.total_blocks == 12);
    /* Blocks still assigned stay: only free ones go back. */
    cfw_game_blocks_acquire(&manager);
    assert(cache.total_blocks == 22 && cache.free_blocks.count == 11);
    cache.free_blocks.count = 0;
    reset_free_blocks();
    push_free_block();
    push_free_block();
    push_free_block();
    cfw_game_blocks_release(&manager);
    assert(cache.total_blocks == 19 && cache.free_blocks.count == 0);
    /* A large cache does not grow and is not trimmed. */
    cache.total_blocks = 40;
    cfw_game_blocks_acquire(&manager);
    assert(acquired == 3 && grow_count == 2);
    cfw_game_blocks_release(&manager);
    assert(rebalanced == 2 && cache.total_blocks == 40);
    /* No cache yet: the native acquire still runs. */
    fake_cache = NULL;
    cfw_game_blocks_acquire(&manager);
    assert(acquired == 4 && grow_count == 2);
    return 0;
}
