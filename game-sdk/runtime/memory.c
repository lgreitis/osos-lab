/* SPDX-License-Identifier: GPL-3.0-only */

#include "game_api.h"
#include "native.h"

union allocation_header {
    size_t size;
    uint64_t alignment;
};

void *game_realloc(void *pointer, size_t size)
{
    if (!size) {
        game_free(pointer);
        return NULL;
    }
    if (!pointer)
        return game_alloc(size);

    union allocation_header *old = (union allocation_header *)pointer - 1;
    if (size <= old->size) {
        old->size = size;
        return pointer;
    }

    void *result = game_alloc(size);
    if (!result)
        return NULL;

    /* Reserve blocks have no native heap header, so resize by copying. */
    for (size_t i = 0; i < old->size; ++i)
        ((uint8_t *)result)[i] = ((const uint8_t *)pointer)[i];
    game_free(pointer);
    return result;
}

void *game_alloc(size_t size)
{
    if (!size)
        size = 1;
    if (size > 8 * 1024 * 1024 - sizeof(union allocation_header))
        return NULL;

    size_t bytes = size + sizeof(union allocation_header);
    union allocation_header *header = osos_game_alloc(bytes);

    /* The regular entry replenishes reserves; larger requests need the heap entry. */
    if (!header)
        header = osos_game_realloc(NULL, bytes);
    if (!header)
        return NULL;

    header->size = size;
    return header + 1;
}

void game_free(void *pointer)
{
    if (pointer)
        osos_game_free((union allocation_header *)pointer - 1);
}

/* GCC can emit this helper for aggregate initialization in freestanding code. */
void *memset(void *destination, int value, size_t bytes)
{
    unsigned char *out = destination;
    for (size_t i = 0; i < bytes; i++)
        out[i] = (unsigned char)value;
    return destination;
}
