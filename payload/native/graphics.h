/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Included through osos.h, after native bindings and integer types. */
enum {
    OSOS_RESOURCE_BMAP = 0x424d6170,
    OSOS_RESOURCE_DRAW = 0x44726177,
};

/* Descriptor identity is retained separately from the changing model property ID. */
static inline uint32_t osos_view_id(const void *view)
{
    return *(const uint32_t *)((const unsigned char *)view + 0x4c);
}

/* Drawing colors are byte-order RGBA; COLR resource words are 0x00RRGGBB. */
static inline void osos_surface_fill_rect(void *surface, const void *rect,
                                          const unsigned char *color, uint32_t mode,
                                          void *clip)
{
    typedef void (*fn)(void *, const void *, const unsigned char *, uint32_t, void *);
    ((fn)OSOS_SURFACE_FILL_RECT)(surface, rect, color, mode, clip);
}

static inline void osos_graphics_fill_rect(void *context, const int32_t *rect)
{
    typedef void (*fn)(void *, const int32_t *);
    ((fn)OSOS_GRAPHICS_FILL_RECT)(context, rect);
}

/* Match GraphicsContext_FillRect's origin, surface, mode and clip contract. */
static inline void osos_graphics_fill_rect_unmapped(void *context, const int32_t *rect)
{
    const unsigned char *bytes = context;
    int32_t x = *(const int32_t *)(bytes + 0x2c);
    int32_t y = *(const int32_t *)(bytes + 0x30);
    int32_t translated[] = {rect[0] + y, rect[1] + x, rect[2] + y, rect[3] + x};
    void *surface = *(unsigned char **)(bytes + 0x1c) + 4;
    osos_surface_fill_rect(surface, translated, bytes + 0x11, bytes[0x10],
                           (unsigned char *)context + 0x34);
}

static inline void osos_graphics_set_color(void *context, const unsigned char *color)
{
    typedef void (*fn)(void *, const unsigned char *);
    ((fn)OSOS_GRAPHICS_SET_COLOR)(context, color);
}

/* Native fonts are borrowed two-word handles resolved from the firmware font cache. */
static inline void osos_font_construct(uint32_t *font, const char *family,
                                       uint32_t size, unsigned char style)
{
    typedef void (*fn)(uint32_t *, const char *, uint32_t, unsigned char);
    ((fn)OSOS_FONT_CONSTRUCT)(font, family, size, style);
}

static inline void osos_graphics_set_font(void *context, const uint32_t *font)
{
    typedef void (*fn)(void *, const uint32_t *);
    ((fn)OSOS_GRAPHICS_SET_FONT)(context, font);
}

static inline void osos_graphics_set_pen(void *context, int x, int baseline)
{
    typedef void (*fn)(void *, int, int);
    ((fn)OSOS_GRAPHICS_SET_PEN)(context, x, baseline);
}

static inline void osos_graphics_set_mode(void *context, unsigned char mode)
{
    typedef void (*fn)(void *, unsigned char);
    ((fn)OSOS_GRAPHICS_SET_MODE)(context, mode);
}

static inline void osos_graphics_set_background_color(void *context,
                                                      const unsigned char *color)
{
    typedef void (*fn)(void *, const unsigned char *);
    ((fn)OSOS_GRAPHICS_SET_BACKGROUND_COLOR)(context, color);
}

static inline void osos_graphics_draw_text(void *context, const char *text)
{
    typedef void (*fn)(void *, const char *);
    ((fn)OSOS_GRAPHICS_DRAW_TEXT)(context, text);
}

static inline int osos_graphics_text_width(void *context, const char *text)
{
    typedef int (*fn)(void *, const char *);
    return ((fn)OSOS_GRAPHICS_TEXT_WIDTH)(context, text);
}

static inline int osos_graphics_font_ascent(void *context)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_GRAPHICS_FONT_ASCENT)(context);
}

static inline int osos_graphics_font_descent(void *context)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_GRAPHICS_FONT_DESCENT)(context);
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
