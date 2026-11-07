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

#include "../../../payload/block_cache.c"

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
    /* Flash keeps a larger reserve and a 10 MiB floor. */
    assert(cfw_block_cache_size(heap, 2 * MIB, 1) == heap - 2 * MIB - 24 * MIB);
    assert(cfw_block_cache_size(heap, 20 * MIB, 1) == 10 * MIB);
    assert(cfw_block_cache_size(heap, heap + 1, 1) == 10 * MIB);

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
    return 0;
}
