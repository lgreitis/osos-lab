/* SPDX-License-Identifier: GPL-3.0-only */

#include "game_api.h"
#include "i_system.h"
#include "z_zone.h"

#include <limits.h>

#define ZONE_ID UINT32_C(0x1d4a11)
#define ZONE_BUDGET (4u * 1024u * 1024u)

struct zone_block {
    uint32_t id;
    unsigned bytes;
    int tag;
    void **owner;
    struct zone_block *previous, *next;
};

_Static_assert(sizeof(struct zone_block) % 8 == 0, "zone payload alignment");

static struct zone_block blocks;
static unsigned allocated_bytes, allocated_count;

static struct zone_block *block_for(void *pointer)
{
    if (!pointer)
        I_Error("Z: null allocation");

    struct zone_block *block = (struct zone_block *)pointer - 1;
    if (block->id != ZONE_ID)
        I_Error("Z: invalid allocation %p", pointer);
    return block;
}

static void check_tag(int tag, void **owner)
{
    if (tag <= 0 || tag == PU_FREE)
        I_Error("Z: invalid tag %d", tag);
    if (tag >= PU_PURGELEVEL && !owner)
        I_Error("Z: cache allocation needs an owner");
}

void Z_Init(void)
{
    blocks.next = blocks.previous = &blocks;
    allocated_bytes = allocated_count = 0;
    printf("Doom zone: native blocks, %u KiB budget\n", ZONE_BUDGET / 1024);
}

void Z_Free(void *pointer)
{
    struct zone_block *block = block_for(pointer);
    if (block->owner)
        *block->owner = NULL;
    block->previous->next = block->next;
    block->next->previous = block->previous;
    allocated_bytes -= block->bytes;
    --allocated_count;
    block->id = 0;
    game_free(block);
}

static int purge_one(void)
{
    for (struct zone_block *block = blocks.next; block != &blocks;
         block = block->next) {
        if (block->tag >= PU_PURGELEVEL) {
            Z_Free(block + 1);
            return 1;
        }
    }
    return 0;
}

void *Z_Malloc(int size, int tag, void *user)
{
    void **owner = user;
    check_tag(tag, owner);
    if (size < 0 || (unsigned)size > ZONE_BUDGET - sizeof(struct zone_block))
        I_Error("Z_Malloc: invalid size %d", size);

    unsigned bytes = (unsigned)size + sizeof(struct zone_block);
    struct zone_block *block;
    for (;;) {
        block = bytes <= ZONE_BUDGET - allocated_bytes ? game_alloc(bytes) : NULL;
        if (block)
            break;
        if (!purge_one())
            I_Error("Z_Malloc: need %u bytes, %u used in %u blocks", bytes,
                    allocated_bytes, allocated_count);
    }

    block->id = ZONE_ID;
    block->bytes = bytes;
    block->tag = tag;
    block->owner = owner;
    block->previous = blocks.previous;
    block->next = &blocks;
    blocks.previous->next = block;
    blocks.previous = block;
    allocated_bytes += bytes;
    ++allocated_count;
    if (owner)
        *owner = block + 1;
    return block + 1;
}

void Z_FreeTags(int lowtag, int hightag)
{
    struct zone_block *block = blocks.next;
    while (block != &blocks) {
        struct zone_block *next = block->next;
        if (block->tag >= lowtag && block->tag <= hightag)
            Z_Free(block + 1);
        block = next;
    }
}

void Z_ChangeTag2(void *pointer, int tag, char *file, int line)
{
    (void)file;
    (void)line;
    struct zone_block *block = block_for(pointer);
    check_tag(tag, block->owner);
    block->tag = tag;
}

void Z_ChangeUser(void *pointer, void **user)
{
    struct zone_block *block = block_for(pointer);
    if (!user)
        I_Error("Z_ChangeUser: null owner");

    /* The previous owner may be in a relocated lump table that was already freed. */
    block->owner = user;
    *user = pointer;
}

void Z_CheckHeap(void)
{
    unsigned bytes = 0, count = 0;
    if (!blocks.next || !blocks.previous || blocks.next->previous != &blocks ||
        blocks.previous->next != &blocks)
        I_Error("Z_CheckHeap: invalid list");

    for (struct zone_block *block = blocks.next; block != &blocks;
         block = block->next) {
        if (++count > allocated_count || block->id != ZONE_ID || !block->next ||
            !block->previous || block->next->previous != block ||
            block->previous->next != block || block->bytes < sizeof(*block) ||
            block->bytes > ZONE_BUDGET - bytes || ((uintptr_t)(block + 1) & 7))
            I_Error("Z_CheckHeap: invalid block %p", (void *)block);
        check_tag(block->tag, block->owner);
        bytes += block->bytes;
    }

    if (bytes != allocated_bytes || count != allocated_count)
        I_Error("Z_CheckHeap: accounting mismatch");
}

static void dump_heap(FILE *stream, int lowtag, int hightag)
{
    fprintf(stream, "zone budget: %u used: %u blocks: %u\n", ZONE_BUDGET,
            allocated_bytes, allocated_count);
    for (struct zone_block *block = blocks.next; block != &blocks; block = block->next)
        if (block->tag >= lowtag && block->tag <= hightag)
            fprintf(stream, "block:%p size:%u owner:%p tag:%d\n", (void *)block,
                    block->bytes, (void *)block->owner, block->tag);
}

void Z_DumpHeap(int lowtag, int hightag)
{
    dump_heap(stdout, lowtag, hightag);
}

void Z_FileDumpHeap(FILE *stream)
{
    dump_heap(stream, 0, INT_MAX);
}

/* Logical zone allowance, including reclaimable cache; native availability varies. */
int Z_FreeMemory(void)
{
    unsigned available = ZONE_BUDGET - allocated_bytes;
    for (struct zone_block *block = blocks.next; block != &blocks; block = block->next)
        if (block->tag >= PU_PURGELEVEL)
            available += block->bytes;
    return (int)available;
}

unsigned int Z_ZoneSize(void)
{
    return ZONE_BUDGET;
}
