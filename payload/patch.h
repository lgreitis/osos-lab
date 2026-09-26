/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_PATCH_H
#define CFW_PATCH_H
#include <stdint.h>

/* Native ARM call sites can enter marked C functions directly. */
#define PATCH_ARM __attribute__((target("arm")))

/* Build metadata; the linker discards it from the installed payload. */
struct cfw_patch {
    uint32_t kind, address, expected, extra, value;
    char symbol[64];
};

_Static_assert(sizeof(struct cfw_patch) == 84, "Patch metadata layout changed");
#define PATCH_NAME_(a, b) a##b
#define PATCH_NAME(a, b) PATCH_NAME_(a, b)
#define PATCH_RECORD(kind, address, expected, extra, value, symbol)                    \
    static const struct cfw_patch PATCH_NAME(cfw_patch_, __COUNTER__)                  \
        __attribute__((used, section(".cfw.patch"), aligned(4))) = {                   \
            kind, address, expected, extra, value, symbol}

#define PATCH_WORD(address, expected, value)                                           \
    PATCH_RECORD(1, address, expected, 0, value, "")
#define PATCH_POINTER(address, expected, symbol)                                       \
    PATCH_RECORD(2, address, expected, 0, 0, #symbol)
#define PATCH_CALL_WORD(address, expected, symbol)                                     \
    PATCH_RECORD(3, address, expected, 0, 0, #symbol)
#define PATCH_CALL(address, original, symbol)                                          \
    PATCH_CALL_WORD(                                                                   \
        address, 0xEB000000u | (((original - address - 8u) >> 2) & 0xFFFFFFu), symbol)
#define PATCH_JUMP(address, first, second, symbol)                                     \
    PATCH_RECORD(4, address, first, second, 0, #symbol)
/* Fill an ordinary C array from the pinned native firmware at assembly time. */
#define PATCH_COPY(array, offset, source, bytes)                                       \
    _Static_assert((offset) + (bytes) <= sizeof(array), "Copy exceeds array");         \
    PATCH_RECORD(5, source, offset, sizeof(array), bytes, #array)

#endif
