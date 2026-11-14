/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../payload/album_artists_index.h"

/* Distinct cache tokens can compare equal; native album order must survive. */
static int compare(void *context, const struct album_artist_track *a,
                   const struct album_artist_track *b)
{
    assert(context == (void *)0x1234);
    uint32_t left = a->token / 3, right = b->token / 3;
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
        actual[i + 1] = (struct album_artist_track){
            .entry = (struct osos_media_entry *)(uintptr_t)(i + 1),
            .token = random_value() % groups,
            .order = i};
        expected[i] = actual[i + 1];
    }
    qsort(expected, count, sizeof(*expected), reference);
    album_artist_sort(actual + 1, count, compare, (void *)0x1234);
    assert(actual[0].token == 0xfeedbeef && actual[count + 1].token == 0xfeedbeef);
    for (uint32_t i = 0; i < count; i++) {
        assert(actual[i + 1].entry == expected[i].entry);
        assert(actual[i + 1].token == expected[i].token);
        assert(actual[i + 1].order == expected[i].order);
    }
    free(actual);
    free(expected);
}

static void check_reorder(uint32_t count)
{
    struct artist_group *rows = malloc((count + 1) * sizeof(*rows));
    struct album_artist_track *order = malloc((count + 1) * sizeof(*order));
    uint32_t *expected = malloc((count + 1) * sizeof(*expected));
    assert(rows && order && expected);
    for (uint32_t i = 0; i < count; i++) {
        rows[i] = (struct artist_group){i, 0, NULL, i % 10, i + 1};
        order[i].group_index = i;
    }
    for (uint32_t i = count; i > 1; i--) {
        uint32_t other = random_value() % i, saved = order[i - 1].group_index;
        order[i - 1].group_index = order[other].group_index;
        order[other].group_index = saved;
    }
    for (uint32_t i = 0; i < count; i++)
        expected[i] = order[i].group_index;
    album_artist_reorder(rows, order, count);
    for (uint32_t i = 0; i < count; i++) {
        assert(rows[i].token == expected[i]);
        assert(rows[i].artwork_count == expected[i] % 10);
        assert(rows[i].albums == expected[i] + 1);
    }
    free(expected);
    free(order);
    free(rows);
}

int main(void)
{
    album_artist_sort(NULL, 0, compare, NULL);
    for (uint32_t i = 0; i < 512; i++) {
        check(i, 1);
        check(i, 17);
        check(i, 4001);
        check_reorder(i);
    }
    check(UINT16_MAX, 701);
    check(UINT16_MAX, UINT16_MAX);
    check_reorder(UINT16_MAX);
    return 0;
}
