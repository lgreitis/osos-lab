/* SPDX-License-Identifier: GPL-3.0-only */

/* Fast MP4 audio seek.
 *
 * The native seek walks the audio track one sample at a time from its current
 * position, or from the first sample when seeking backwards. Near the end of a
 * 16 hour audiobook that is 2.4 million samples and takes 4-9 seconds, and the
 * audio engine gives up on the track. Compute the state the walk would have
 * reached at the start of the target chunk from the time-to-sample and
 * sample-to-chunk tables instead, then let the native walk cover the rest of
 * that chunk. Anything unusual takes the native path unchanged. */

#include <stdint.h>
#include "native/bindings.h"
#include "patch.h"

/* Audio track state, [reader + OSOS_MP4_AUDIO_TRACK]. */
enum {
    TRACK_SAMPLE_SIZES = 0x0c, /* stsz window: fixed size, count, base, entries */
    TRACK_CHUNK_RUNS = 0x14,   /* stsc window: count, base, entries */
    TRACK_DURATIONS = 0x18,    /* stts: count, -, entries */
    TRACK_EDITS = 0x1c,        /* elst: count, entries */
    TRACK_NEXT_SAMPLE = 0x68,
    TRACK_RUN = 0x6c,
    TRACK_CHUNK = 0x70,
    TRACK_CHUNK_SAMPLE = 0x74,
    TRACK_CHUNK_OFFSET = 0x78,
    TRACK_TIMESCALE = 0x7c,
    TRACK_DURATION_POSITION = 0x84,
    TRACK_PRESENTED = 0x88,
    TRACK_MEDIA_TIME = 0x90,
    TRACK_DURATION_INDEX = 0xa0,
    TRACK_EDIT_START = 0xa8,
    TRACK_EDIT_END = 0xb0,
    WINDOW = 0x8000,
};

struct duration_entry {
    uint32_t count, delta;
};

struct chunk_run {
    uint32_t first_chunk, samples_per_chunk, description;
};

typedef int (*seek_fn)(void *, uint32_t);
typedef int (*window_fn)(void *, uint32_t);
typedef uint64_t (*divide_fn)(uint64_t, uint64_t);

static uint32_t word(const void *base, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)base + offset);
}

static uint64_t dword(const void *base, uint32_t offset)
{
    return word(base, offset) | (uint64_t)word(base, offset + 4) << 32;
}

static void set_word(void *base, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)base + offset) = value;
}

static void set_dword(void *base, uint32_t offset, uint64_t value)
{
    set_word(base, offset, (uint32_t)value);
    set_word(base, offset + 4, (uint32_t)(value >> 32));
}

static uint64_t divide(uint64_t value, uint64_t by)
{
    return ((divide_fn)OSOS_DIVIDE_U64)(value, by);
}

struct durations {
    const struct duration_entry *entries;
    uint32_t count;
};

/* End time of sample n (1-based) and its position in the duration table. */
static int sample_end(const struct durations *d, uint32_t n, uint64_t *time, uint32_t *index,
                      uint32_t *position)
{
    uint64_t t = 0;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        uint32_t count = d->entries[i].count;
        if (n <= seen + count) {
            *time = t + (uint64_t)(n - seen) * d->entries[i].delta;
            *index = i;
            *position = n - seen;
            return 1;
        }
        t += (uint64_t)count * d->entries[i].delta;
        seen += count;
    }
    return 0;
}

/* First sample (1-based) that starts at or after time. */
static uint32_t first_sample_from(const struct durations *d, uint64_t time)
{
    uint64_t t = 0;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        uint32_t count = d->entries[i].count, delta = d->entries[i].delta;
        uint64_t span = (uint64_t)count * delta;
        if (delta && t + span > time)
            return seen + (uint32_t)divide(time - t + delta - 1, delta) + 1;
        t += span;
        seen += count;
    }
    return 0;
}

static int jump_to_chunk(void *reader, void *track, uint32_t ms)
{
    const void *runs_window = (const void *)word(track, TRACK_CHUNK_RUNS);
    const void *duration_table = (const void *)word(track, TRACK_DURATIONS);
    const void *edits = (const void *)word(track, TRACK_EDITS);
    void *sizes = (void *)word(track, TRACK_SAMPLE_SIZES);
    if (!runs_window || !duration_table || !sizes)
        return 0;
    uint32_t run_count = word(runs_window, 0);
    if (word(runs_window, 4) != 0 || run_count == 0 || run_count > WINDOW)
        return 0;
    const struct chunk_run *runs = (const struct chunk_run *)word(runs_window, 8);
    struct durations d = {(const struct duration_entry *)word(duration_table, 8),
                          word(duration_table, 0)};
    if (!d.entries || !d.count)
        return 0;

    uint64_t start = 0, end = UINT64_MAX;
    if (edits) {
        if (word(edits, 0) != 1)
            return 0;
        const uint8_t *edit = (const uint8_t *)word(edits, 4);
        if (word(edit, 12) & 0x80000000u)
            return 0; /* empty edit */
        start = dword(track, TRACK_EDIT_START);
        end = dword(track, TRACK_EDIT_END);
    }
    uint32_t first_presented = first_sample_from(&d, start);
    if (!first_presented)
        return 0;
    uint64_t presented_base = 0;
    uint32_t index, position;
    if (first_presented > 1 &&
        !sample_end(&d, first_presented - 1, &presented_base, &index, &position))
        return 0;

    uint64_t target = divide((uint64_t)ms * word(track, TRACK_TIMESCALE), 1000);
    uint32_t total = 0;
    for (uint32_t i = 0; i < d.count; i++)
        total += d.entries[i].count;

    /* Last chunk whose first sample still ends before the target. */
    uint32_t chunk = 0, sample = 0, run = 0, first = 1;
    for (uint32_t i = 0; i < run_count; i++) {
        uint32_t per_chunk = runs[i].samples_per_chunk;
        uint32_t next = i + 1 < run_count ? runs[i + 1].first_chunk : UINT32_MAX;
        for (uint32_t c = runs[i].first_chunk; c < next && first <= total; c++) {
            uint64_t time;
            if (!sample_end(&d, first, &time, &index, &position) || time > end)
                goto found;
            if (first >= first_presented && time - presented_base >= target)
                goto found;
            chunk = c;
            sample = first;
            run = i;
            first += per_chunk;
        }
    }
found:
    if (!chunk || sample < first_presented)
        return 0;

    uint64_t media;
    if (!sample_end(&d, sample, &media, &index, &position))
        return 0;
    uint32_t size = word(sizes, 0);
    if (!size) {
        uint32_t base = word(sizes, 8);
        if (sample - 1 < base || sample - 1 >= base + WINDOW) {
            if (((window_fn)OSOS_MP4_LOAD_SAMPLE_SIZES)(reader, sample - 1))
                return 0;
            base = word(sizes, 8);
        }
        size = ((const uint32_t *)word(sizes, 12))[sample - 1 - base];
    }

    set_word(track, TRACK_NEXT_SAMPLE, sample + 1);
    set_word(track, TRACK_RUN, run);
    set_word(track, TRACK_CHUNK, chunk);
    set_word(track, TRACK_CHUNK_SAMPLE, 1);
    set_word(track, TRACK_CHUNK_OFFSET, size);
    set_word(track, TRACK_DURATION_INDEX, index);
    set_word(track, TRACK_DURATION_POSITION, position);
    set_dword(track, TRACK_PRESENTED, media - presented_base);
    set_dword(track, TRACK_MEDIA_TIME, media);
    return 1;
}

PATCH_ARM int cfw_mp4_seek(void *reader, uint32_t ms)
{
    seek_fn native = (seek_fn)OSOS_MP4_SEEK;
    void *track = (void *)word(reader, OSOS_MP4_AUDIO_TRACK);
    /* Start over from the first sample, which also reloads the edit, then put the
     * walk at the start of the target chunk. */
    if (track && ms && !native(reader, 0))
        jump_to_chunk(reader, track, ms);
    return native(reader, ms);
}
