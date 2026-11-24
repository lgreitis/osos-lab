/* SPDX-License-Identifier: MIT */
#pragma once
#include "../include/osos_directory_types.h"

/* The synchronous iterator used internally by the game's AsyncFileIO API. */
static inline void osos_directory_init(struct osos_directory_iterator *iterator,
                                       const char *path, int recursive)
{
    typedef void *(*fn)(void *, const char *, uint8_t, int);
    ((fn)0x081e57dc)(iterator, path, recursive != 0, 0);
}

static inline int osos_directory_next(struct osos_directory_iterator *iterator,
                                      struct osos_native_path_string *path,
                                      uint8_t *is_directory)
{
    typedef int (*fn)(void *, void *, uint8_t *);
    return ((fn)0x081e54e8)(iterator, path, is_directory);
}

static inline void osos_directory_destroy(struct osos_directory_iterator *iterator)
{
    typedef void (*fn)(void *);
    ((fn)0x081e5928)(iterator);
}

static inline void osos_path_string_init(struct osos_native_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)0x0826fcc4)(path);
}

static inline const char *osos_path_string_text(struct osos_native_path_string *path)
{
    typedef const char *(*fn)(void *);
    return ((fn)0x0829bac0)(path);
}

static inline void osos_path_string_destroy(struct osos_native_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)0x0826fcf4)(path);
}
