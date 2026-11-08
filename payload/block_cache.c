/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

#include "osos.h"
#include "patch.h"

/*
 * After the library import, the native loader grows the shared block cache (audio
 * read-ahead, eApp memory, block-backed heaps) to
 * max(heap total - import cost - 14 MiB, 14 MiB). It never shrinks. Once the
 * library costs more than about 24 MiB, the 14 MiB floor takes the heap that the
 * 14 MiB reserve was meant to keep free, and the UI fails its allocations.
 *
 * The 14 MiB floor is what playback and a game need together: on first play the
 * media player claims max(cache - 10 MiB, 6 MiB) as read-ahead and keeps it, and a
 * game launches only if the eApp block manager gets 8 MiB beside it. Hard disks keep
 * the native sizes, since a smaller read-ahead means more spin-ups. Flash storage
 * refills at no spin-up cost, so it keeps a larger reserve of free heap and shrinks
 * the media player's read-ahead below a 14 MiB cache so that a game still fits.
 *
 * The flash floor of 6 MiB is what music needs at once: 2 MiB of read-ahead, the
 * UI's two blocks, Now Playing art, and four blocks for Cover Flow, which shows only
 * placeholders without any. A game's blocks do not fit in that floor, so when the
 * eApp block manager asks for them, the cache first grows by what the other clients
 * would not leave free, as far as the heap allows. When the game returns its blocks,
 * the unassigned blocks above the import size go back to the heap.
 */
#define MIB (1024u * 1024u)
#define NATIVE_RESERVE (14u * MIB)
#define NATIVE_FLOOR (14u * MIB)
#define FLASH_RESERVE (24u * MIB)
#define FLASH_FLOOR (6u * MIB)
/* The native media-player buffer: max(cache - PLAYER_RESERVE, PLAYER_MIN). */
#define PLAYER_RESERVE (10u * MIB)
#define PLAYER_MIN (6u * MIB)
/* The eApp block manager's request for a game. */
#define GAME_BLOCKS (8u * MIB)
/* Read-ahead that still plays ALAC at 1 Mbps with seeking and skipping. */
#define PLAYER_FLOOR (2u * MIB)
/* Heap that growing the cache for a game leaves free for the game and the UI. */
#define GROW_HEAP_MARGIN (1u * MIB)

enum {
    ID_CONFIG = 0,
    ID_MODEL = 27,
    ID_MODEL_WORDS = 20,
    ID_COMMAND_SET_2 = 83,
    ID_CF_ADVANCED_TIMING = 163,
    ID_FORM_FACTOR = 168,
    ID_DATA_SET_MANAGEMENT = 169,
    ID_ROTATION_RATE = 217,
};

/* IDENTIFY stores two ASCII characters per word, high byte first. */
static void model_text(const uint16_t *identify, char *model)
{
    for (int i = 0; i < ID_MODEL_WORDS; i++) {
        model[2 * i] = (char)(identify[ID_MODEL + i] >> 8);
        model[2 * i + 1] = (char)identify[ID_MODEL + i];
    }
    model[ID_MODEL_WORDS * 2] = 0;
}

static int contains(const char *text, const char *needle)
{
    for (; *text; text++) {
        int i = 0;
        while (needle[i] && text[i] == needle[i])
            i++;
        if (!needle[i])
            return 1;
    }
    return 0;
}

/* Toshiba (MK1634GAL) and Samsung (HS12YHA) 1.8" drives, the only hard disks Apple
 * fitted to these iPods, optionally after the vendor name. */
static int hard_disk_model(const char *model)
{
    if (contains(model, "TOSHIBA") || contains(model, "SAMSUNG"))
        return 1;
    for (const char *p = model; *p; p++) {
        int word_start = p == model || p[-1] == ' ';
        if (word_start &&
            ((p[0] == 'M' && p[1] == 'K') || (p[0] == 'H' && p[1] == 'S')) &&
            p[2] >= '0' && p[2] <= '9')
            return 1;
    }
    return 0;
}

/*
 * Flash adapters report themselves inconsistently, so a reported rotation rate
 * decides first, then the flash markers Rockbox checks, then the known hard-disk
 * models; any other disk in a Classic is an adapter.
 */
int cfw_storage_is_flash(const uint16_t *identify)
{
    char model[ID_MODEL_WORDS * 2 + 1];
    model_text(identify, model);
    /* No IDENTIFY data: keep the native behavior. */
    if (!identify[ID_CONFIG] && !model[0])
        return 0;

    /* Some CF adapters byte-swap the solid-state value. */
    uint16_t rate = identify[ID_ROTATION_RATE];
    if (rate == 0x0001 || rate == 0x0100)
        return 1;
    if (rate >= 0x0401 && rate <= 0xfffe)
        return 0;

    if (identify[ID_CONFIG] == 0x848a || identify[ID_CONFIG] == 0x844a)
        return 1;
    /* Bits 15:14 = 01 mark the word valid; bit 2 is the CompactFlash feature set. */
    if ((identify[ID_COMMAND_SET_2] & 0xc004) == 0x4004)
        return 1;
    if ((identify[ID_FORM_FACTOR] & 0x0f) >= 0x06)
        return 1;
    if (identify[ID_DATA_SET_MANAGEMENT] & 1) /* TRIM */
        return 1;
    if (identify[ID_CF_ADVANCED_TIMING])
        return 1;
    if (contains(model, "iFlash"))
        return 1;
    return !hard_disk_model(model);
}

uint32_t cfw_block_cache_size(uint32_t heap_total, uint32_t import_cost, int flash)
{
    uint32_t reserve = flash ? FLASH_RESERVE : NATIVE_RESERVE;
    uint32_t floor = flash ? FLASH_FLOOR : NATIVE_FLOOR;
    uint32_t size = heap_total > import_cost && heap_total - import_cost > reserve
                        ? heap_total - import_cost - reserve
                        : 0;
    return size < floor ? floor : size;
}

/* The size chosen after the import; the cache returns to it after a game. */
static uint32_t import_cache_bytes;
static int grown_for_game;

PATCH_ARM uint32_t cfw_block_cache_budget(uint32_t heap_total, uint32_t import_cost)
{
    import_cache_bytes = cfw_block_cache_size(
        heap_total, import_cost, cfw_storage_is_flash(osos_ata_identify()));
    return import_cache_bytes;
}

/*
 * The media player's read-ahead for a cache of cache_bytes. The native minimum
 * shrinks only where a game would not fit beside it, which only the flash floor
 * reaches.
 */
uint32_t cfw_media_player_buffer_size(uint32_t cache_bytes)
{
    uint32_t minimum = PLAYER_MIN;
    if (cache_bytes < PLAYER_MIN + GAME_BLOCKS)
        minimum = cache_bytes > PLAYER_FLOOR + GAME_BLOCKS ? cache_bytes - GAME_BLOCKS
                                                           : PLAYER_FLOOR;
    return cache_bytes > PLAYER_RESERVE + minimum ? cache_bytes - PLAYER_RESERVE
                                                  : minimum;
}

/* Called with r0 = cache bytes - 10 MiB, as the native code computes it. */
PATCH_ARM int32_t cfw_media_player_buffer(int32_t over_reserve)
{
    return (int32_t)cfw_media_player_buffer_size(
        (uint32_t)(over_reserve + (int32_t)PLAYER_RESERVE));
}

/*
 * Blocks to add to a cache of total blocks so that request blocks fit beside the
 * kept blocks of clients that do not share theirs, leaving GROW_HEAP_MARGIN of
 * heap_free unused.
 */
uint32_t cfw_block_cache_shortfall(uint32_t total, uint32_t kept, uint32_t request,
                                   uint32_t heap_free, uint32_t block_bytes)
{
    if (!block_bytes || kept + request <= total)
        return 0;
    uint32_t wanted = kept + request - total;
    uint32_t spare =
        heap_free > GROW_HEAP_MARGIN ? (heap_free - GROW_HEAP_MARGIN) / block_bytes : 0;
    return wanted < spare ? wanted : spare;
}

struct block_cache_node {
    struct block_cache_node *next, *previous;
    struct block_cache_client **client;
};

struct block_cache_client {
    uint8_t unknown_00[0x18];
    uint32_t held_blocks;
    uint8_t unknown_1c[0x24];
    /* Clients with a weight share their blocks with the rebalancer. */
    uint32_t weight;
    uint32_t flags;
    uint32_t desired_bytes;
};

/* Blocks not assigned to any client. */
struct block_list {
    void *tag;
    void *first;
    void *last;
    uint32_t unknown_0c;
    uint32_t count;
};

struct block_cache {
    uint8_t unknown_00[0x04];
    struct block_list free_blocks;
    uint8_t unknown_18[0x10];
    struct block_cache_node *clients;
    uint32_t client_count;
    uint32_t block_bytes;
    uint32_t total_blocks;
    uint8_t unknown_38[0x148 - 0x38];
    uint8_t lock[4];
};

struct eapp_block_manager {
    uint8_t unknown_00[0x14];
    struct block_cache_client **client;
    uint8_t unknown_18[0x1c];
    uint32_t request_blocks;
};

_Static_assert(sizeof(void *) != 4 ||
                   offsetof(struct block_cache_client, desired_bytes) == 0x48,
               "Block cache client layout");
_Static_assert(sizeof(void *) != 4 ||
                   (offsetof(struct block_cache, free_blocks.count) == 0x14 &&
                    offsetof(struct block_cache, lock) == 0x148),
               "Block cache layout");
_Static_assert(sizeof(void *) != 4 ||
                   offsetof(struct eapp_block_manager, request_blocks) == 0x34,
               "eApp block manager layout");

/* Blocks that clients other than skip keep from the rebalancer. */
static uint32_t kept_blocks(struct block_cache *cache, struct block_cache_client *skip)
{
    uint32_t kept = 0;
    for (struct block_cache_node *node = cache->clients->next; node != cache->clients;
         node = node->next) {
        struct block_cache_client *client = *node->client;
        if (!client || client == skip || client->weight)
            continue;
        uint32_t desired =
            (client->desired_bytes + cache->block_bytes - 1) / cache->block_bytes;
        kept += client->held_blocks > desired ? client->held_blocks : desired;
    }
    return kept;
}

PATCH_ARM void cfw_game_blocks_acquire(struct eapp_block_manager *manager)
{
    struct block_cache *cache = osos_block_cache();
    if (cache && cache->block_bytes) {
        osos_mutex_lock(cache->lock);
        uint32_t total = cache->total_blocks;
        uint32_t kept = kept_blocks(cache, manager->client ? *manager->client : NULL);
        osos_mutex_unlock(cache->lock);
        uint32_t add =
            cfw_block_cache_shortfall(total, kept, manager->request_blocks,
                                      osos_heap_free_bytes(), cache->block_bytes);
        if (add) {
            osos_block_cache_grow((total + add) * cache->block_bytes);
            grown_for_game = 1;
        }
    }
    osos_eapp_blocks_acquire(manager);
}

PATCH_ARM void cfw_game_blocks_release(struct eapp_block_manager *manager)
{
    osos_eapp_blocks_release(manager);
    struct block_cache *cache = osos_block_cache();
    if (!grown_for_game || !cache || !cache->block_bytes)
        return;
    grown_for_game = 0;
    /*
     * Free unassigned blocks above the import size, as the native trim does but
     * without waiting for pending requests, which the rebalancer is still serving
     * while the game's blocks come back.
     */
    osos_mutex_lock(cache->lock);
    uint32_t keep = import_cache_bytes / cache->block_bytes;
    uint32_t extra = cache->total_blocks > keep ? cache->total_blocks - keep : 0;
    if (extra > cache->free_blocks.count)
        extra = cache->free_blocks.count;
    uint32_t free_before = cache->free_blocks.count;
    void *node = cache->free_blocks.first;
    for (uint32_t i = 0; i < extra && node != &cache->free_blocks; i++)
        node = osos_block_list_erase(&cache->free_blocks, node);
    cache->total_blocks -= free_before - cache->free_blocks.count;
    osos_block_cache_rebalance(cache);
    osos_mutex_unlock(cache->lock);
}
