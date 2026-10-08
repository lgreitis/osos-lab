/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../payload/album_artists_index.h"

/* Distinct cache tokens can compare equal; native album order must survive. */
static int compare(void *context, uint32_t left, uint32_t right)
{
    assert(context == (void *)0x1234);
    left /= 3;
    right /= 3;
    return (left > right) - (left < right);
}

static int reference(const void *left, const void *right)
{
    const struct album_artist_track *a = left, *b = right;
    uint32_t a_group = a->token / 3, b_group = b->token / 3;
    if (a_group != b_group)
        return a_group > b_group ? 1 : -1;
    return (a->order > b->order) - (a->order < b->order);
}

static uint32_t random_state = 7;
static uint32_t random_value(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static void check(uint32_t count, uint32_t groups)
{
    struct album_artist_track *actual = malloc((count + 2) * sizeof(*actual));
    struct album_artist_track *expected = malloc((count + 1) * sizeof(*expected));
    assert(actual && expected);
    actual[0].token = actual[count + 1].token = 0xfeedbeef;
    for (uint32_t i = 0; i < count; i++) {
        actual[i+1] = (struct album_artist_track){
            (struct osos_media_entry *)(uintptr_t)(i + 1), random_value() % groups, i};
        expected[i] = actual[i+1];
    }
    qsort(expected, count, sizeof(*expected), reference);
    album_artist_sort(actual + 1, count, compare, (void *)0x1234);
    assert(actual[0].token == 0xfeedbeef && actual[count+1].token == 0xfeedbeef);
    for (uint32_t i = 0; i < count; i++) {
        assert(actual[i+1].entry == expected[i].entry);
        assert(actual[i+1].token == expected[i].token);
        assert(actual[i+1].order == expected[i].order);
    }
    free(actual);
    free(expected);
}

int main(void)
{
    album_artist_sort(NULL, 0, compare, NULL);
    for (uint32_t i = 0; i < 512; i++) {
        check(i, 1);
        check(i, 17);
        check(i, 4001);
    }
    check(UINT16_MAX, 701);
    check(UINT16_MAX, UINT16_MAX);
    return 0;
}
