/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

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
