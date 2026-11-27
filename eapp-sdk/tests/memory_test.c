/* SPDX-License-Identifier: GPL-3.0-only */

/* The runtime's memcpy, memmove, memset and memcmp at every alignment and
 * length up to a few blocks, against byte loops, and realloc's copy. They
 * build under other names so that the host's C library stays in use. */

#define memcpy runtime_memcpy
#define memmove runtime_memmove
#define memset runtime_memset
#define memcmp runtime_memcmp
#include "../runtime/memory.c"
#undef memcpy
#undef memmove
#undef memset
#undef memcmp

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void *GameMemory_Allocate(uint32_t byte_count)
{
    return malloc(byte_count);
}

void *GameMemory_Reallocate(void *block, uint32_t byte_count)
{
    return realloc(block, byte_count);
}

void GameMemory_Free(void *block)
{
    free(block);
}

#define SIZE 160

static uint8_t source[SIZE], target[SIZE], expected[SIZE];

static void fill(uint8_t *bytes, uint8_t seed)
{
    for (unsigned i = 0; i < SIZE; ++i)
        bytes[i] = (uint8_t)(seed + i * 7);
}

static void copies(void)
{
    for (unsigned from = 0; from < 8; ++from)
        for (unsigned to = 0; to < 8; ++to)
            for (unsigned n = 0; n + 8 <= SIZE; n += n < 40 ? 1 : 13) {
                fill(source, 1);
                fill(target, 99);
                fill(expected, 99);
                for (unsigned i = 0; i < n; ++i)
                    expected[to + i] = source[from + i];
                assert(runtime_memcpy(target + to, source + from, n) == target + to);
                for (unsigned i = 0; i < SIZE; ++i)
                    assert(target[i] == expected[i]);
            }
}

static void fills(void)
{
    for (unsigned to = 0; to < 8; ++to)
        for (unsigned n = 0; n + 8 <= SIZE; n += n < 40 ? 1 : 13) {
            fill(target, 5);
            fill(expected, 5);
            for (unsigned i = 0; i < n; ++i)
                expected[to + i] = 0xa7;
            assert(runtime_memset(target + to, 0x3a7, n) == target + to);
            for (unsigned i = 0; i < SIZE; ++i)
                assert(target[i] == expected[i]);
        }
}

/* Overlaps both ways. */
static void moves(void)
{
    for (unsigned from = 0; from < 24; ++from)
        for (unsigned to = 0; to < 24; ++to) {
            unsigned n = SIZE - 24;
            fill(target, 3);
            fill(expected, 3);
            for (unsigned i = 0; i < n; ++i)
                expected[to + i] = (uint8_t)(3 + (from + i) * 7);
            assert(runtime_memmove(target + to, target + from, n) == target + to);
            for (unsigned i = 0; i < SIZE; ++i)
                assert(target[i] == expected[i]);
        }
}

static void compares(void)
{
    fill(source, 1);
    fill(target, 1);
    assert(runtime_memcmp(source, target, SIZE) == 0);
    target[40] = (uint8_t)(source[40] + 1);
    assert(runtime_memcmp(source, target, SIZE) < 0);
    assert(runtime_memcmp(target, source, SIZE) > 0);
    assert(runtime_memcmp(source, target, 40) == 0);
}

static void reallocs(void)
{
    uint8_t *block = eapp_alloc(37);
    for (unsigned i = 0; i < 37; ++i)
        block[i] = (uint8_t)i;
    block = eapp_realloc(block, 300);
    for (unsigned i = 0; i < 37; ++i)
        assert(block[i] == i);
    eapp_free(block);
}

int main(void)
{
    copies();
    fills();
    moves();
    compares();
    reallocs();
    puts("memory_test passed");
    return 0;
}
