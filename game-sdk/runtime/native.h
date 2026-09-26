/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef OSOS_GAME_NATIVE_H
#define OSOS_GAME_NATIVE_H

#include <stddef.h>
#include <stdint.h>

/* Classic 7G FW2.0.4 bindings. Native function entries use ARM state. */
_Static_assert(sizeof(void *) == 4, "native runtime requires 32-bit ARM");

static inline void *osos_game_alloc(uint32_t bytes)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)0x082bf52c)(bytes);
}

static inline void *osos_game_realloc(void *pointer, uint32_t bytes)
{
    typedef void *(*fn)(void *, uint32_t);
    return ((fn)0x082bf558)(pointer, bytes);
}

static inline void osos_game_free(void *pointer)
{
    typedef void (*fn)(void *);
    ((fn)0x082bc3ac)(pointer);
}

static inline uint32_t osos_ticks_us(void)
{
    typedef uint32_t (*fn)(void);
    return ((fn)0x0802d368)();
}

static inline void osos_yield(void)
{
    typedef void (*fn)(uint32_t);
    ((fn)0x0804ba10)(0);
}

static inline void osos_read_wheel(int32_t *delta, uint32_t *flags)
{
    typedef void (*fn)(int32_t *, uint32_t *);
    ((fn)0x082bc194)(delta, flags);
}

static inline int32_t osos_read_setting(const char *name, void *value, uint32_t *size)
{
    typedef int32_t (*fn)(const char *, void *, uint32_t *);
    return ((fn)0x082bc614)(name, value, size);
}

struct osos_path {
    uint16_t volume;
    char text[258];
};

/* Open the data fork for reading. */
static inline int osos_file_open(const struct osos_path *path, void **handle)
{
    typedef int (*fn)(const struct osos_path *, uint32_t, uint32_t, void **);
    return ((fn)0x0804f75c)(path, 0x64617461, 1, handle);
}

static inline int osos_file_size(void *handle, uint32_t *size)
{
    typedef int (*fn)(void *, uint32_t *);
    return ((fn)0x0829a540)(handle, size);
}

static inline int osos_file_read(void *handle, void *buffer, uint32_t bytes,
                                 uint32_t *read)
{
    typedef int (*fn)(void *, void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x0804f810)(handle, buffer, bytes, 0, read);
}

static inline int osos_file_seek(void *handle, int64_t offset)
{
    /* AAPCS aligns offset to r2/r3; absolute whence is passed on the stack. */
    typedef int (*fn)(void *, int64_t, int);
    return ((fn)0x0826d8e0)(handle, offset, 0);
}

static inline void osos_file_close(void *handle)
{
    typedef void (*fn)(void *);
    ((fn)0x0804f6bc)(handle);
}

struct osos_surface {
    uint8_t unknown_00[0x64];
    uint8_t color_mode;
    uint8_t unknown_65[3];
    uint32_t pixels_address;
    uint8_t unknown_6c[0x94 - 0x6c];
    uint32_t width;
    uint32_t height;
};

_Static_assert(offsetof(struct osos_surface, color_mode) == 0x64,
               "surface color mode offset");
_Static_assert(offsetof(struct osos_surface, pixels_address) == 0x68,
               "surface pixels offset");
_Static_assert(offsetof(struct osos_surface, width) == 0x94, "surface width offset");
_Static_assert(offsetof(struct osos_surface, height) == 0x98, "surface height offset");

static inline const struct osos_surface *osos_draw_surface(void)
{
    typedef const struct osos_surface *(*fn)(uint32_t);
    return ((fn)0x082c00f0)(0x3059);
}

static inline void osos_present_surface(uintptr_t surface)
{
    typedef uint32_t (*fn)(uint32_t, uintptr_t);
    ((fn)0x082c0274)(0, surface);
}

#endif
