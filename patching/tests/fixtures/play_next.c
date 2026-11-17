/* SPDX-License-Identifier: GPL-3.0-only */
#include "media_queue.h"
#include "../../../payload/play_next.c"

static void check_order(const unsigned *expected, unsigned count)
{
    assert(queue.root.tracks == count);
    for (unsigned i = 0; i < count; i++)
        assert(queue_entry(&player.playback, i)->id == expected[i]);
}

static void check_single(int shuffled, int reversed, int last)
{
    reset_queue(shuffled, reversed);
    unsigned expected[6], at = last ? 5 : 2;
    for (unsigned i = 0; i < 5; i++)
        expected[i + (i >= at)] = queue_entry(&player.playback, i)->id;
    expected[at] = 5;
    struct osos_media_item item = {.entry = source.physical[0], .record = &records[5]};
    lock_player(&player);
    assert(cfw_play_next_add(&player, &item, last));
    unlock_player(&player);
    check_order(expected, 6);
    assert(stops == 1 && rebuilds == 1 && prepared == 1 && rebound == 1);
}

static void check_bulk(int shuffled, int reversed, int last)
{
    reset_queue(shuffled, reversed);
    filtered_id = 6;
    unsigned expected[7], at = last ? 5 : 2;
    for (unsigned i = 0; i < 5; i++)
        expected[i + (i >= at ? 2 : 0)] = queue_entry(&player.playback, i)->id;
    expected[at] = 5;
    expected[at + 1] = 7;
    lock_player(&player);
    cfw_play_next_add_browser(&player, browser_selection, last);
    unlock_player(&player);
    check_order(expected, 7);
    assert(copies == 2 && stops == 1 && rebuilds == 1 && prepared == 1 && rebound == 1);
}

static void check_failures(void)
{
    reset_queue(0, 0);
    struct osos_media_item item = {.entry = source.physical[0], .record = &records[5]};
    assert(!cfw_play_next_add(&player, NULL, 0));
    music = 0;
    assert(!cfw_play_next_add(&player, &item, 0));
    assert(!copies && !stops);
    music = 1;
    fail_copy = 1;
    assert(!cfw_play_next_add(&player, &item, 0));
    assert(queue.root.tracks == 5 && !prepared && stops == rebuilds);
    fail_copy = 0;
    player.current = NULL;
    assert(!cfw_play_next_add(&player, &item, 0));
    reset_queue(0, 0);
    queue.root.tracks = UINT16_MAX - 1;
    copies = 0;
    assert(!cfw_play_next_add(&player, &item, 0));
    cfw_play_next_add_browser(&player, browser_selection, 0);
    assert(!copies && queue.root.tracks == UINT16_MAX - 1 && !prepared);
    queue.root.tracks = UINT16_MAX;
    assert(!cfw_play_next_add(&player, &item, 0));
    reset_queue(1, 0);
    queue.order.count--;
    assert(!cfw_play_next_add(&player, &item, 0));
}

int main(void)
{
    for (int shuffled = 0; shuffled < 2; shuffled++)
        for (int reversed = 0; reversed < 2; reversed++)
            for (int last = 0; last < 2; last++) {
                check_single(shuffled, reversed, last);
                check_bulk(shuffled, reversed, last);
            }
    check_failures();
    return 0;
}
