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
 * This keeps the native reserve and lowers the floor to a cache that still plays
 * audio. Flash storage has no spin-up cost, so it trades cache for a larger reserve
 * of free heap.
 */
#define MIB (1024u * 1024u)
#define NATIVE_RESERVE (14u * MIB)
#define FLASH_RESERVE (24u * MIB)
/* Playback stalls at 6 MiB and starts at 7 MiB; the rest is margin for the other
 * block clients and high-bitrate files. */
#define CACHE_FLOOR (10u * MIB)

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
    uint32_t size = heap_total > import_cost && heap_total - import_cost > reserve
                        ? heap_total - import_cost - reserve
                        : 0;
    return size < CACHE_FLOOR ? CACHE_FLOOR : size;
}

PATCH_ARM uint32_t cfw_block_cache_budget(uint32_t heap_total, uint32_t import_cost)
{
    return cfw_block_cache_size(heap_total, import_cost,
                                cfw_storage_is_flash(osos_ata_identify()));
}
