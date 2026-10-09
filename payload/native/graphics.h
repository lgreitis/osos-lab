/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Included through osos.h, after native bindings and integer types. */
enum {
    OSOS_RESOURCE_BMAP = 0x424d6170,
    OSOS_RESOURCE_DRAW = 0x44726177,
    OSOS_BMAP_HEADER_SIZE = 28,
};

static inline int osos_bitmap_view_ready(void *view)
{
    return *((unsigned char *)view + 0xa1) != 0;
}

static inline uint32_t osos_bitmap_view_type(void *view)
{
    return *(uint32_t *)((unsigned char *)view + 0x40);
}

static inline uint32_t osos_bitmap_view_id(void *view)
{
    return *(uint32_t *)((unsigned char *)view + 0x44);
}

static inline void *osos_bitmap_view_service(void *view)
{
    return *(void **)((unsigned char *)view + 0x38);
}

static inline void *osos_bitmap_view_context(void *view)
{
    return *(void **)((unsigned char *)view + 0x15c);
}

static inline void *osos_bitmap_view_bitmap(void *view)
{
    return (unsigned char *)view + 0xa4;
}

static inline void *osos_graphics_bitmap(void *context)
{
    return *(void **)((unsigned char *)context + 0x1c);
}

static inline const unsigned char *osos_bitmap_pixels(void *bitmap)
{
    return *(const unsigned char **)((unsigned char *)bitmap + 4);
}

static inline void *osos_resource_service_get(void *service, uint32_t type, uint32_t id)
{
    typedef void *(*fn)(void *, uint32_t, uint32_t);
    return ((fn)OSOS_RESOURCE_SERVICE_GET)(service, type, id);
}

static inline void osos_bitmap_load_resource(void *bitmap, const void *data)
{
    typedef void (*fn)(void *, const void *, uint32_t, uint32_t);
    ((fn)OSOS_BITMAP_LOAD_RESOURCE)(bitmap, data, 0, 0);
}

static inline void osos_graphics_attach_bitmap(void *context, void *bitmap)
{
    typedef void (*fn)(void *, void *);
    ((fn)OSOS_GRAPHICS_ATTACH_BITMAP)(context, bitmap);
}

/* Drawing colors are byte-order RGBA; COLR resource words are 0x00RRGGBB. */
static inline void osos_surface_fill_rect(void *surface, const void *rect,
                                          const unsigned char *color,
                                          uint32_t mode, void *clip)
{
    typedef void (*fn)(void *, const void *, const unsigned char *, uint32_t, void *);
    ((fn)OSOS_SURFACE_FILL_RECT)(surface, rect, color, mode, clip);
}

static inline void osos_graphics_set_color(void *context, const unsigned char *color)
{
    typedef void (*fn)(void *, const unsigned char *);
    ((fn)OSOS_GRAPHICS_SET_COLOR)(context, color);
}

/* GLES color components use 16.16 fixed point. */
static inline void osos_gles_clear_color(int red, int green, int blue, int alpha)
{
    typedef void (*fn)(int, int, int, int);
    ((fn)OSOS_GLES_CLEAR_COLOR_X)(red, green, blue, alpha);
}

static inline uint32_t osos_rgb565_blend(uint32_t first, uint32_t second, int weight)
{
    typedef uint32_t (*fn)(uint32_t, uint32_t, int);
    return ((fn)OSOS_RGB565_BLEND_FIXED)(first, second, weight);
}

static inline void osos_ui_invalidate(void)
{
    typedef unsigned char *(*root_fn)(void);
    typedef void (*invalidate_fn)(void *, const void *);
    unsigned char *root = ((root_fn)OSOS_UI_ROOT_VIEW)();
    if (root)
        ((invalidate_fn)OSOS_VIEW_INVALIDATE_RECT)(root, root + 0x80);
}
