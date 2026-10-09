/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "osos.h"

/* Record validation belongs to the caller. Short transfers are failures. */
static inline int cfw_preferences_read(const struct osos_path *path, void *record,
                                       uint32_t size)
{
    void *handle;
    uint32_t count = 0;
    if (osos_file_open(path, 1, &handle) != 0)
        return 0;
    int result = osos_file_read(handle, record, size, &count);
    osos_file_close(handle);
    return !result && count == size;
}

static inline int cfw_preferences_write(const struct osos_path *path,
                                        const void *record, uint32_t size)
{
    void *handle;
    uint32_t count = 0;
    if (osos_file_open(path, 2, &handle) != 0)
        return 0;
    int result = osos_file_write(handle, record, size, &count);
    osos_file_close(handle);
    return !result && count == size;
}
