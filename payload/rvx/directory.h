/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "../../rvx-sdk/osos-2.0.5.h"
#include <stdint.h>

/* The synchronous directory iterator and the path string it fills. */
struct osos_native_path_string {
    uint32_t vtable_address;
    uint32_t data_address;
};

struct osos_directory_iterator {
    uint32_t state[0x44 / 4];
};

_Static_assert(sizeof(struct osos_native_path_string) == 8, "native path string size");
_Static_assert(sizeof(struct osos_directory_iterator) == 0x44,
               "directory iterator size");

static inline void osos_directory_init(struct osos_directory_iterator *iterator,
                                       const char *path, int recursive)
{
    typedef void *(*fn)(void *, const char *, uint8_t, int);
    ((fn)ADDR_DirectoryIterator_Init)(iterator, path, recursive != 0, 0);
}

static inline int osos_directory_next(struct osos_directory_iterator *iterator,
                                      struct osos_native_path_string *path,
                                      uint8_t *is_directory)
{
    typedef int (*fn)(void *, void *, uint8_t *);
    return ((fn)ADDR_DirectoryIterator_Next)(iterator, path, is_directory);
}

static inline void osos_directory_destroy(struct osos_directory_iterator *iterator)
{
    typedef void (*fn)(void *);
    ((fn)ADDR_DirectoryIterator_Destroy)(iterator);
}

static inline void osos_path_string_init(struct osos_native_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)ADDR_NativePath_InitEmpty)(path);
}

static inline const char *osos_path_string_text(struct osos_native_path_string *path)
{
    typedef const char *(*fn)(void *);
    return ((fn)ADDR_NativeString_GetUtf8)(path);
}

static inline void osos_path_string_destroy(struct osos_native_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)ADDR_NativePath_Destroy)(path);
}
