/* SPDX-License-Identifier: GPL-3.0-only */

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
 * refills at no spin-up cost, so it keeps a larger reserve of free heap, allows a
 * 10 MiB cache, and shrinks the media player's read-ahead below 14 MiB so that a
 * game still fits.
 */
#define MIB (1024u * 1024u)
#define NATIVE_RESERVE (14u * MIB)
#define NATIVE_FLOOR (14u * MIB)
#define FLASH_RESERVE (24u * MIB)
#define FLASH_FLOOR (10u * MIB)
/* The native media-player buffer: max(cache - PLAYER_RESERVE, PLAYER_MIN). */
#define PLAYER_RESERVE (10u * MIB)
#define PLAYER_MIN (6u * MIB)
/* The eApp block manager's request for a game. */
#define GAME_BLOCKS (8u * MIB)
/* Read-ahead that still plays ALAC at 1 Mbps with seeking and skipping. */
#define PLAYER_FLOOR (2u * MIB)

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

PATCH_ARM uint32_t cfw_block_cache_budget(uint32_t heap_total, uint32_t import_cost)
{
    return cfw_block_cache_size(heap_total, import_cost,
                                cfw_storage_is_flash(osos_ata_identify()));
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
