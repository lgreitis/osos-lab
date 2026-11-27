/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */
#pragma once
#include <stdint.h>

/* The sizes the memory rules choose, kept apart from the hooks so the host tests
 * can check them. memory.c has the reasoning. */

#define RVX_MIB (1024u * 1024u)
#define RVX_NATIVE_RESERVE (14u * RVX_MIB)
#define RVX_NATIVE_FLOOR (14u * RVX_MIB)
#define RVX_FLASH_RESERVE (24u * RVX_MIB)
#define RVX_FLASH_FLOOR (6u * RVX_MIB)
/* The native media-player read-ahead: max(cache - PLAYER_RESERVE, PLAYER_MIN). */
#define RVX_PLAYER_RESERVE (10u * RVX_MIB)
#define RVX_PLAYER_MIN (6u * RVX_MIB)
/* The eApp block manager's request for a game. */
#define RVX_GAME_BLOCKS (8u * RVX_MIB)
/* Read-ahead that still plays ALAC at 1 Mbps with seeking and skipping. */
#define RVX_PLAYER_FLOOR (2u * RVX_MIB)
/* Heap that growing the cache for a game leaves free for the game and the UI. */
#define RVX_GROW_HEAP_MARGIN (1u * RVX_MIB)

/* The block cache after the import: max(heap - import - reserve, floor). */
static inline uint32_t rvx_block_cache_size(uint32_t heap_total, uint32_t import_cost,
                                            int flash)
{
    uint32_t reserve = flash ? RVX_FLASH_RESERVE : RVX_NATIVE_RESERVE;
    uint32_t floor = flash ? RVX_FLASH_FLOOR : RVX_NATIVE_FLOOR;
    uint32_t size = heap_total > import_cost && heap_total - import_cost > reserve
                        ? heap_total - import_cost - reserve
                        : 0;
    return size < floor ? floor : size;
}

/* The media player's read-ahead for a cache of cache_bytes. The native minimum
 * shrinks only where a game would not fit beside it, which only the flash floor
 * reaches: on a hard disk the cache is at least 14 MiB and the result is Apple's. */
static inline uint32_t rvx_media_read_ahead(uint32_t cache_bytes)
{
    uint32_t minimum = RVX_PLAYER_MIN;
    if (cache_bytes < RVX_PLAYER_MIN + RVX_GAME_BLOCKS)
        minimum = cache_bytes > RVX_PLAYER_FLOOR + RVX_GAME_BLOCKS
                      ? cache_bytes - RVX_GAME_BLOCKS
                      : RVX_PLAYER_FLOOR;
    return cache_bytes > RVX_PLAYER_RESERVE + minimum ? cache_bytes - RVX_PLAYER_RESERVE
                                                      : minimum;
}

/* Blocks to add to a cache of total blocks so that request blocks fit beside the
 * kept blocks of clients that do not share theirs, leaving RVX_GROW_HEAP_MARGIN of
 * heap_free unused. */
static inline uint32_t rvx_block_cache_shortfall(uint32_t total, uint32_t kept,
                                                 uint32_t request, uint32_t heap_free,
                                                 uint32_t block_bytes)
{
    if (!block_bytes || kept + request <= total)
        return 0;
    uint32_t wanted = kept + request - total;
    uint32_t spare = heap_free > RVX_GROW_HEAP_MARGIN
                         ? (heap_free - RVX_GROW_HEAP_MARGIN) / block_bytes
                         : 0;
    return wanted < spare ? wanted : spare;
}
