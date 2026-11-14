/* SPDX-License-Identifier: GPL-3.0-only */
#include "store.h"
#include "preferences.h"

/* File words: magic, version, total bytes, FNV-1a of the record bytes, then records
 * of key, size and data padded to a word. Unknown records survive a rewrite, so
 * settings written by a newer build are kept by an older one. */
enum {
    MAGIC = 0x50574643, /* "CFWP" */
    VERSION = 1,
    HEADER = 4,
    CAPACITY = 128, /* words; the known records use well under half */
};

static const char file[] = "iPod_Control\\Device\\CFWPreferences";
static uint32_t store[CAPACITY];
static int dirty, migrating;

static uint32_t words(uint32_t size)
{
    return (size + 3) / 4;
}

static uint32_t checksum(void)
{
    const unsigned char *data = (const unsigned char *)store;
    uint32_t hash = 2166136261u;
    for (uint32_t i = HEADER * 4; i < store[2]; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

/* Returns the word index of key's record, or the end of the records. */
static uint32_t find(uint32_t key)
{
    uint32_t i = HEADER, end = store[2] / 4;
    while (i < end && store[i] != key)
        i += 2 + words(store[i + 1]);
    return i;
}

static int valid(void)
{
    uint32_t end = store[2] / 4;
    if (store[0] != MAGIC || store[1] != VERSION || store[2] % 4 || end < HEADER ||
        end > CAPACITY || store[3] != checksum())
        return 0;
    for (uint32_t i = HEADER; i < end; i += 2 + words(store[i + 1]))
        if (end - i < 2 || store[i + 1] > (end - i - 2) * 4)
            return 0;
    return 1;
}

void cfw_store_load(void)
{
    if (cfw_preferences_read(file, store, HEADER * 4) && store[2] <= CAPACITY * 4 &&
        cfw_preferences_read(file, store, store[2]) && valid())
        return;
    store[0] = MAGIC;
    store[1] = VERSION;
    store[2] = HEADER * 4;
    migrating = dirty = 1;
}

int cfw_store_migrating(void)
{
    return migrating;
}

uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity)
{
    uint32_t i = find(key);
    if (i >= store[2] / 4)
        return 0;
    uint32_t size = store[i + 1] < capacity ? store[i + 1] : capacity;
    const unsigned char *from = (const unsigned char *)&store[i + 2];
    for (uint32_t b = 0; b < size; b++)
        ((unsigned char *)data)[b] = from[b];
    return size;
}

void cfw_store_set(uint32_t key, const void *data, uint32_t size)
{
    const unsigned char *from = data;
    uint32_t i = find(key), end = store[2] / 4, need = 2 + words(size);
    if (i < end && store[i + 1] == size) {
        unsigned char *to = (unsigned char *)&store[i + 2];
        for (uint32_t b = 0; b < size; b++) {
            if (to[b] != from[b]) {
                to[b] = from[b];
                dirty = 1;
            }
        }
        return;
    }
    uint32_t old = i < end ? 2 + words(store[i + 1]) : 0;
    /* Capacity covers every known record; a record that cannot fit is not stored. */
    if (end - old + need > CAPACITY)
        return;
    for (uint32_t w = i + old; w < end; w++)
        store[w - old] = store[w];
    end -= old;
    store[end] = key;
    store[end + 1] = size;
    store[end + need - 1] = 0;
    unsigned char *to = (unsigned char *)&store[end + 2];
    for (uint32_t b = 0; b < size; b++)
        to[b] = from[b];
    store[2] = (end + need) * 4;
    dirty = 1;
}

void cfw_store_flush(void)
{
    if (!dirty)
        return;
    store[3] = checksum();
    if (!cfw_preferences_write(file, store, store[2]))
        return;
    dirty = 0;
    if (migrating) {
        migrating = 0;
        cfw_store_delete_legacy();
    }
}
