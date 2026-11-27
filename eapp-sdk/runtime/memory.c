/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/memory.c (8fc840d) by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "eapp.h"

/* Every block carries its requested size, so realloc can shrink in place and
 * copy on growth; the host's blocks have no header of their own. */
union allocation_header {
    size_t size;
    uint64_t alignment;
};

void *eapp_realloc(void *pointer, size_t size)
{
    if (!size) {
        eapp_free(pointer);
        return NULL;
    }
    if (!pointer)
        return eapp_alloc(size);

    union allocation_header *old = (union allocation_header *)pointer - 1;
    if (size <= old->size) {
        old->size = size;
        return pointer;
    }

    void *result = eapp_alloc(size);
    if (!result)
        return NULL;
    memcpy(result, pointer, old->size);
    eapp_free(pointer);
    return result;
}

void *eapp_alloc(size_t size)
{
    if (!size)
        size = 1;
    if (size > 8 * 1024 * 1024 - sizeof(union allocation_header))
        return NULL;

    size_t bytes = size + sizeof(union allocation_header);
    union allocation_header *header = GameMemory_Allocate(bytes);
    /* The block entry serves from the reserve; larger requests go to the heap. */
    if (!header)
        header = GameMemory_Reallocate(NULL, bytes);
    if (!header)
        return NULL;

    header->size = size;
    return header + 1;
}

void eapp_free(void *pointer)
{
    if (pointer)
        GameMemory_Free((union allocation_header *)pointer - 1);
}

typedef uint32_t __attribute__((may_alias)) word;

/* GCC emits calls to these for aggregates in an app without a C library, and
 * apps call them for their own copies. memcpy moves eight words a step once
 * both ends share a word boundary, memset once its destination reaches
 * one. */
void *memcpy(void *destination, const void *source, size_t bytes)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    if ((((uintptr_t)out ^ (uintptr_t)in) & 3) == 0) {
        for (; bytes && ((uintptr_t)out & 3); --bytes)
            *out++ = *in++;
        word *to = (word *)out;
        const word *from = (const word *)in;
        for (; bytes >= 32; bytes -= 32, to += 8, from += 8) {
            word a = from[0], b = from[1], c = from[2], d = from[3];
            word e = from[4], f = from[5], g = from[6], h = from[7];
            to[0] = a, to[1] = b, to[2] = c, to[3] = d;
            to[4] = e, to[5] = f, to[6] = g, to[7] = h;
        }
        for (; bytes >= 4; bytes -= 4)
            *to++ = *from++;
        out = (uint8_t *)to;
        in = (const uint8_t *)from;
    }
    while (bytes--)
        *out++ = *in++;
    return destination;
}

void *memmove(void *destination, const void *source, size_t bytes)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    if (out <= in || out >= in + bytes)
        return memcpy(destination, source, bytes);
    while (bytes--)
        out[bytes] = in[bytes];
    return destination;
}

int memcmp(const void *a, const void *b, size_t bytes)
{
    const uint8_t *x = a, *y = b;
    for (; bytes; --bytes, ++x, ++y)
        if (*x != *y)
            return *x - *y;
    return 0;
}

void *memset(void *destination, int value, size_t bytes)
{
    uint8_t *out = destination;
    for (; bytes && ((uintptr_t)out & 3); --bytes)
        *out++ = (uint8_t)value;
    word fill = (uint8_t)value * 0x01010101u;
    word *to = (word *)out;
    for (; bytes >= 32; bytes -= 32, to += 8) {
        to[0] = fill, to[1] = fill, to[2] = fill, to[3] = fill;
        to[4] = fill, to[5] = fill, to[6] = fill, to[7] = fill;
    }
    for (; bytes >= 4; bytes -= 4)
        *to++ = fill;
    out = (uint8_t *)to;
    while (bytes--)
        *out++ = (uint8_t)value;
    return destination;
}
