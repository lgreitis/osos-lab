/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/text.c and ui.c (8fc840d) by lgreitis. */

#include "eapp_draw.h"
#include "eapp_libc.h"

/* The glyphs of the printable ASCII characters, 32 to 126, five bits per
 * row from the top, the high bit at the left. */
static const uint8_t glyphs[95][7] = {
    { 0,  0,  0,  0,  0,  0,  0}, /* ' ' */
    { 4,  4,  4,  4,  4,  0,  4}, /* '!' */
    {10, 10, 10,  0,  0,  0,  0}, /* '"' */
    {10, 10, 31, 10, 31, 10, 10}, /* '#' */
    { 4, 15, 20, 14,  5, 30,  4}, /* '$' */
    {24, 25,  2,  4,  8, 19,  3}, /* '%' */
    {12, 18, 20,  8, 21, 18, 13}, /* '&' */
    { 4,  4,  4,  0,  0,  0,  0}, /* '\'' */
    { 2,  4,  8,  8,  8,  4,  2}, /* '(' */
    { 8,  4,  2,  2,  2,  4,  8}, /* ')' */
    { 0,  4, 21, 14, 21,  4,  0}, /* '*' */
    { 0,  4,  4, 31,  4,  4,  0}, /* '+' */
    { 0,  0,  0,  0, 12,  4,  8}, /* ',' */
    { 0,  0,  0, 31,  0,  0,  0}, /* '-' */
    { 0,  0,  0,  0,  0, 12, 12}, /* '.' */
    { 1,  2,  2,  4,  8,  8, 16}, /* '/' */
    {14, 17, 19, 21, 25, 17, 14}, /* '0' */
    { 4, 12,  4,  4,  4,  4, 14}, /* '1' */
    {14, 17,  1,  2,  4,  8, 31}, /* '2' */
    {30,  1,  1, 14,  1,  1, 30}, /* '3' */
    { 2,  6, 10, 18, 31,  2,  2}, /* '4' */
    {31, 16, 16, 30,  1,  1, 30}, /* '5' */
    {14, 16, 16, 30, 17, 17, 14}, /* '6' */
    {31,  1,  2,  4,  8,  8,  8}, /* '7' */
    {14, 17, 17, 14, 17, 17, 14}, /* '8' */
    {14, 17, 17, 15,  1,  1, 14}, /* '9' */
    { 0,  4,  4,  0,  4,  4,  0}, /* ':' */
    { 0, 12, 12,  0, 12,  4,  8}, /* ';' */
    { 2,  4,  8, 16,  8,  4,  2}, /* '<' */
    { 0,  0, 31,  0, 31,  0,  0}, /* '=' */
    { 8,  4,  2,  1,  2,  4,  8}, /* '>' */
    {14, 17,  1,  2,  4,  0,  4}, /* '?' */
    {14, 17, 23, 21, 23, 16, 15}, /* '@' */
    {14, 17, 17, 31, 17, 17, 17}, /* 'A' */
    {30, 17, 17, 30, 17, 17, 30}, /* 'B' */
    {14, 17, 16, 16, 16, 17, 14}, /* 'C' */
    {30, 17, 17, 17, 17, 17, 30}, /* 'D' */
    {31, 16, 16, 30, 16, 16, 31}, /* 'E' */
    {31, 16, 16, 30, 16, 16, 16}, /* 'F' */
    {14, 17, 16, 23, 17, 17, 15}, /* 'G' */
    {17, 17, 17, 31, 17, 17, 17}, /* 'H' */
    {14,  4,  4,  4,  4,  4, 14}, /* 'I' */
    { 7,  2,  2,  2,  2, 18, 12}, /* 'J' */
    {17, 18, 20, 24, 20, 18, 17}, /* 'K' */
    {16, 16, 16, 16, 16, 16, 31}, /* 'L' */
    {17, 27, 21, 21, 17, 17, 17}, /* 'M' */
    {17, 25, 21, 19, 17, 17, 17}, /* 'N' */
    {14, 17, 17, 17, 17, 17, 14}, /* 'O' */
    {30, 17, 17, 30, 16, 16, 16}, /* 'P' */
    {14, 17, 17, 17, 21, 18, 13}, /* 'Q' */
    {30, 17, 17, 30, 20, 18, 17}, /* 'R' */
    {15, 16, 16, 14,  1,  1, 30}, /* 'S' */
    {31,  4,  4,  4,  4,  4,  4}, /* 'T' */
    {17, 17, 17, 17, 17, 17, 14}, /* 'U' */
    {17, 17, 17, 17, 17, 10,  4}, /* 'V' */
    {17, 17, 17, 21, 21, 21, 10}, /* 'W' */
    {17, 17, 10,  4, 10, 17, 17}, /* 'X' */
    {17, 17, 10,  4,  4,  4,  4}, /* 'Y' */
    {31,  1,  2,  4,  8, 16, 31}, /* 'Z' */
    {14,  8,  8,  8,  8,  8, 14}, /* '[' */
    {16,  8,  8,  4,  2,  2,  1}, /* '\\' */
    {14,  2,  2,  2,  2,  2, 14}, /* ']' */
    { 4, 10, 17,  0,  0,  0,  0}, /* '^' */
    { 0,  0,  0,  0,  0,  0, 31}, /* '_' */
    { 8,  4,  2,  0,  0,  0,  0}, /* '`' */
    { 0,  0, 14,  1, 15, 17, 15}, /* 'a' */
    {16, 16, 22, 25, 17, 17, 30}, /* 'b' */
    { 0,  0, 14, 16, 16, 17, 14}, /* 'c' */
    { 1,  1, 13, 19, 17, 17, 15}, /* 'd' */
    { 0,  0, 14, 17, 31, 16, 14}, /* 'e' */
    { 6,  9,  8, 28,  8,  8,  8}, /* 'f' */
    { 0,  0, 15, 17, 15,  1, 14}, /* 'g' */
    {16, 16, 22, 25, 17, 17, 17}, /* 'h' */
    { 4,  0, 12,  4,  4,  4, 14}, /* 'i' */
    { 2,  0,  6,  2,  2, 18, 12}, /* 'j' */
    {16, 16, 18, 20, 24, 20, 18}, /* 'k' */
    {12,  4,  4,  4,  4,  4,  7}, /* 'l' */
    { 0,  0, 26, 21, 21, 17, 17}, /* 'm' */
    { 0,  0, 22, 25, 17, 17, 17}, /* 'n' */
    { 0,  0, 14, 17, 17, 17, 14}, /* 'o' */
    { 0,  0, 30, 17, 30, 16, 16}, /* 'p' */
    { 0,  0, 15, 17, 15,  1,  1}, /* 'q' */
    { 0,  0, 22, 25, 16, 16, 16}, /* 'r' */
    { 0,  0, 15, 16, 14,  1, 30}, /* 's' */
    { 8,  8, 28,  8,  8,  9,  6}, /* 't' */
    { 0,  0, 17, 17, 17, 19, 13}, /* 'u' */
    { 0,  0, 17, 17, 17, 10,  4}, /* 'v' */
    { 0,  0, 17, 17, 21, 21, 10}, /* 'w' */
    { 0,  0, 17, 10,  4, 10, 17}, /* 'x' */
    { 0,  0, 17, 10,  4,  4,  8}, /* 'y' */
    { 0,  0, 31,  2,  4,  8, 31}, /* 'z' */
    { 2,  4,  4,  8,  4,  4,  2}, /* '{' */
    { 4,  4,  4,  4,  4,  4,  4}, /* '|' */
    { 8,  4,  4,  2,  4,  4,  8}, /* '}' */
    { 0,  0,  9, 21, 18,  0,  0}, /* '~' */
};

static int usable(const struct eapp_framebuffer *fb)
{
    return fb && fb->pixels;
}

static void pixel(const struct eapp_framebuffer *fb, int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < (int)fb->width && y < (int)fb->height)
        fb->pixels[(unsigned)y * fb->stride_pixels + (unsigned)x] = color;
}

/* `width` pixels from (x, y) along the row, cut to the framebuffer. */
static void span(const struct eapp_framebuffer *fb, int x, int y, int width, uint16_t color)
{
    if (y < 0 || y >= (int)fb->height)
        return;
    int right = x + width;
    if (x < 0)
        x = 0;
    if (right > (int)fb->width)
        right = fb->width;
    uint16_t *row = fb->pixels + (unsigned)y * fb->stride_pixels;
    for (; x < right; ++x)
        row[x] = color;
}

void eapp_fill_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color)
{
    if (!usable(fb))
        return;
    int bottom = y + height;
    if (y < 0)
        y = 0;
    if (bottom > (int)fb->height)
        bottom = fb->height;
    for (; y < bottom; ++y)
        span(fb, x, y, width, color);
}

void eapp_clear(const struct eapp_framebuffer *fb, uint16_t color)
{
    if (usable(fb))
        eapp_fill_rect(fb, 0, 0, fb->width, fb->height, color);
}

void eapp_draw_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    span(fb, x, y, width, color);
    span(fb, x, y + height - 1, width, color);
    eapp_fill_rect(fb, x, y + 1, 1, height - 2, color);
    eapp_fill_rect(fb, x + width - 1, y + 1, 1, height - 2, color);
}

void eapp_draw_pixel(const struct eapp_framebuffer *fb, int x, int y, uint16_t color)
{
    if (usable(fb))
        pixel(fb, x, y, color);
}

void eapp_draw_line(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                    uint16_t color)
{
    if (!usable(fb))
        return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            return;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/* The column where the edge from (xs, ys) to (xe, ye) crosses row y. */
static int edge(int xs, int ys, int xe, int ye, int y)
{
    return ye == ys ? xe : xs + (xe - xs) * (y - ys) / (ye - ys);
}

static void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

static void widen(int *a, int *b, int x)
{
    if (x < *a)
        *a = x;
    if (x > *b)
        *b = x;
}

void eapp_fill_triangle(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                        int x2, int y2, uint16_t color)
{
    if (!usable(fb))
        return;
    if (y0 > y1) {
        swap(&x0, &x1);
        swap(&y0, &y1);
    }
    if (y1 > y2) {
        swap(&x1, &x2);
        swap(&y1, &y2);
    }
    if (y0 > y1) {
        swap(&x0, &x1);
        swap(&y0, &y1);
    }
    int top = y0 < 0 ? 0 : y0, bottom = y2 >= (int)fb->height ? (int)fb->height - 1 : y2;
    for (int y = top; y <= bottom; ++y) {
        int a = edge(x0, y0, x2, y2, y);
        int b = y < y1 ? edge(x0, y0, x1, y1, y) : edge(x1, y1, x2, y2, y);
        if (a > b)
            swap(&a, &b);
        /* A flat side lies on a vertex's row, which an edge of zero height
         * does not span. */
        if (y == y0)
            widen(&a, &b, x0);
        if (y == y1)
            widen(&a, &b, x1);
        if (y == y2)
            widen(&a, &b, x2);
        span(fb, a, y, b - a + 1, color);
    }
}

/* The first pixel whose centre is at or past `v`, in 16.16. */
static int first_centre(int64_t v)
{
    return (int)((v - 0x8000 + 0xffff) >> 16);
}

/* How far x (16.16) moves per row along the edge from (x0, y0) down to
 * (x1, y1), and x where the edge crosses the centre of row `row`. Triangles
 * that share an edge walk it from the same end with the same step, so they
 * agree on every row. */
static int64_t edge_step(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    return y1 > y0 ? ((int64_t)x1 - x0) * 65536 / ((int64_t)y1 - y0) : 0;
}

static int64_t edge_at(int32_t x0, int32_t y0, int64_t step, int row)
{
    return x0 + ((((int64_t)row << 16) + 0x8000 - y0) * step >> 16);
}

/* Alpha 0 to 255 blended over a pixel. */
static void blend_pixel(uint16_t *out, int alpha, int sr, int sg, int sb)
{
    alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
    /* 0 to 256, so that 255 covers the pixel fully. */
    alpha += alpha >> 7;
    unsigned d = *out;
    int dr = (int)(d >> 11), dg = (int)(d >> 5 & 63), db = (int)(d & 31);
    dr += (sr - dr) * alpha >> 8;
    dg += (sg - dg) * alpha >> 8;
    db += (sb - db) * alpha >> 8;
    *out = (uint16_t)(dr << 11 | dg << 5 | db);
}

/* Alpha slopes up to this many per pixel step in integers; a steeper one
 * belongs to a sliver under a sixteenth of a pixel wide, whose few pixels
 * take their alpha from the plane in floating point. */
#define STEEP 4096

void eapp_blend_triangle(const struct eapp_framebuffer *fb, const int32_t x[3],
                         const int32_t y[3], const uint8_t alpha[3], uint16_t color)
{
    if (!usable(fb) || !x || !y || !alpha)
        return;
    unsigned t = 0, m = 1, b = 2, swap;
    if (y[m] < y[t])
        swap = t, t = m, m = swap;
    if (y[b] < y[t])
        swap = t, t = b, b = swap;
    if (y[b] < y[m])
        swap = m, m = b, b = swap;
    int64_t mx = (int64_t)x[m] - x[t], my = (int64_t)y[m] - y[t];
    int64_t bx = (int64_t)x[b] - x[t], by = (int64_t)y[b] - y[t];
    /* Twice the area in 2^-32 square pixels, in floating point, which holds
     * any corners; negative when the long edge from top to bottom lies on
     * the right. */
    double area = (double)mx * (double)by - (double)bx * (double)my;
    if (area == 0)
        return;
    int top = first_centre(y[t]), middle = first_centre(y[m]), bottom = first_centre(y[b]);
    if (top < 0)
        top = 0;
    if (bottom > (int)fb->height)
        bottom = (int)fb->height;
    if (top >= bottom)
        return;
    /* The alpha plane: its slopes per pixel, in floating point once per
     * triangle and in 16.16 for the steps. */
    double ma = alpha[m] - alpha[t], ba = alpha[b] - alpha[t];
    double slope_x = (ma * (double)by - ba * (double)my) * 65536 / area;
    double slope_y = (ba * (double)mx - ma * (double)bx) * 65536 / area;
    int steep = slope_x > STEEP || slope_x < -STEEP || slope_y > STEEP || slope_y < -STEEP;
    int64_t step_x = steep ? 0 : (int64_t)(slope_x * 65536);
    int64_t step_y = steep ? 0 : (int64_t)(slope_y * 65536);
    int64_t long_step = edge_step(x[t], y[t], x[b], y[b]);
    /* The short edge from the top corner to the middle one, or once past
     * the middle on to the bottom one. */
    unsigned from = top < middle ? t : m, to = top < middle ? m : b;
    int64_t short_step = edge_step(x[from], y[from], x[to], y[to]);
    int64_t along = edge_at(x[t], y[t], long_step, top);
    int64_t across = edge_at(x[from], y[from], short_step, top);
    int sr = color >> 11, sg = color >> 5 & 63, sb = color & 31;
    for (int row = top; row < bottom; ++row, along += long_step, across += short_step) {
        if (row == middle && row > top) {
            short_step = edge_step(x[m], y[m], x[b], y[b]);
            across = edge_at(x[m], y[m], short_step, row);
        }
        int first = first_centre(area < 0 ? across : along);
        int end = first_centre(area < 0 ? along : across);
        if (first < 0)
            first = 0;
        if (end > (int)fb->width)
            end = (int)fb->width;
        if (first >= end)
            continue;
        /* From the top corner to the first pixel's centre, 16.16. */
        int64_t dx = ((int64_t)first << 16) + 0x8000 - x[t];
        int64_t dy = ((int64_t)row << 16) + 0x8000 - y[t];
        uint16_t *out = fb->pixels + (unsigned)row * fb->stride_pixels + (unsigned)first;
        if (steep) {
            for (int col = first; col < end; ++col, dx += 65536) {
                double v = alpha[t] + (slope_x * (double)dx + slope_y * (double)dy) / 65536;
                blend_pixel(out++, v <= 0 ? 0 : v >= 255 ? 255 : (int)v, sr, sg, sb);
            }
            continue;
        }
        int64_t start = ((int64_t)alpha[t] << 16) + ((step_x * dx + step_y * dy) >> 16);
        int32_t a = start < -(1 << 29) ? -(1 << 29) : start > (1 << 29) ? (1 << 29) : (int32_t)start;
        for (int col = first; col < end; ++col, a += (int32_t)step_x)
            blend_pixel(out++, a >> 16, sr, sg, sb);
    }
}

/* Midpoint circle: `outline` plots the eight points of (x, y), the fill
 * covers the rows at ±y with x and, when x is about to shrink, the rows at ±x
 * with the widest y seen there. */
static void circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                   uint16_t color, int outline)
{
    if (!usable(fb) || radius < 0)
        return;
    int x = radius, y = 0, error = 1 - radius;
    while (y <= x) {
        if (outline) {
            pixel(fb, cx + x, cy + y, color);
            pixel(fb, cx - x, cy + y, color);
            pixel(fb, cx + x, cy - y, color);
            pixel(fb, cx - x, cy - y, color);
            pixel(fb, cx + y, cy + x, color);
            pixel(fb, cx - y, cy + x, color);
            pixel(fb, cx + y, cy - x, color);
            pixel(fb, cx - y, cy - x, color);
        } else {
            span(fb, cx - x, cy + y, 2 * x + 1, color);
            span(fb, cx - x, cy - y, 2 * x + 1, color);
        }
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            if (!outline) {
                span(fb, cx - y + 1, cy + x, 2 * y - 1, color);
                span(fb, cx - y + 1, cy - x, 2 * y - 1, color);
            }
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

void eapp_draw_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color)
{
    circle(fb, cx, cy, radius, color, 1);
}

void eapp_fill_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color)
{
    circle(fb, cx, cy, radius, color, 0);
}

void eapp_draw_bitmap_part(const struct eapp_framebuffer *fb, int x, int y,
                           const struct eapp_bitmap *bitmap, int source_x, int source_y,
                           int width, int height)
{
    if (!usable(fb) || !bitmap || !bitmap->pixels)
        return;
    if (source_x < 0) {
        width += source_x;
        x -= source_x;
        source_x = 0;
    }
    if (source_y < 0) {
        height += source_y;
        y -= source_y;
        source_y = 0;
    }
    if (source_x + width > (int)bitmap->width)
        width = (int)bitmap->width - source_x;
    if (source_y + height > (int)bitmap->height)
        height = (int)bitmap->height - source_y;
    if (x < 0) {
        width += x;
        source_x -= x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        source_y -= y;
        y = 0;
    }
    if (x + width > (int)fb->width)
        width = (int)fb->width - x;
    if (y + height > (int)fb->height)
        height = (int)fb->height - y;
    if (width <= 0)
        return;
    for (int row = 0; row < height; ++row) {
        const uint16_t *source =
            bitmap->pixels + (unsigned)(source_y + row) * bitmap->stride_pixels + source_x;
        uint16_t *target = fb->pixels + (unsigned)(y + row) * fb->stride_pixels + x;
        if (bitmap->transparent < 0)
            memcpy(target, source, (size_t)width * sizeof *target);
        else
            for (int column = 0; column < width; ++column)
                if (source[column] != bitmap->transparent)
                    target[column] = source[column];
    }
}

void eapp_draw_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                      const struct eapp_bitmap *bitmap)
{
    if (bitmap)
        eapp_draw_bitmap_part(fb, x, y, bitmap, 0, 0, bitmap->width, bitmap->height);
}

struct heap_framebuffer {
    struct eapp_framebuffer fb;
    uint16_t pixels[];
};

struct eapp_framebuffer *eapp_framebuffer_create(unsigned width, unsigned height)
{
    if (!width || !height || width > EAPP_FRAMEBUFFER_MAX_SIDE || height > EAPP_FRAMEBUFFER_MAX_SIDE)
        return NULL;
    size_t bytes = (size_t)width * height * sizeof(uint16_t);
    struct heap_framebuffer *heap = eapp_alloc(sizeof *heap + bytes);
    if (!heap)
        return NULL;
    memset(heap->pixels, 0, bytes);
    heap->fb = (struct eapp_framebuffer){heap->pixels, width, height, width};
    return &heap->fb;
}

void eapp_framebuffer_free(struct eapp_framebuffer *fb)
{
    eapp_free(fb);
}

int32_t eapp_read_pixel(const struct eapp_framebuffer *fb, int x, int y)
{
    if (!usable(fb) || x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height)
        return -1;
    return fb->pixels[(unsigned)y * fb->stride_pixels + (unsigned)x];
}

void eapp_copy_rect(const struct eapp_framebuffer *fb, int x, int y,
                    const struct eapp_framebuffer *source, int source_x, int source_y,
                    int width, int height)
{
    if (!usable(source))
        return;
    struct eapp_bitmap view = {source->pixels, source->width, source->height,
                               source->stride_pixels, -1};
    eapp_draw_bitmap_part(fb, x, y, &view, source_x, source_y, width, height);
}

void eapp_draw_mono_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                           const uint8_t *bits, unsigned width, unsigned height,
                           uint16_t color)
{
    if (!usable(fb) || !bits)
        return;
    unsigned pitch = (width + 7) / 8;
    for (unsigned row = 0; row < height; ++row, bits += pitch)
        for (unsigned column = 0; column < width; ++column)
            if (bits[column / 8] & (0x80u >> (column % 8)))
                pixel(fb, x + (int)column, y + (int)row, color);
}

static const uint8_t *glyph(char c)
{
    unsigned code = (unsigned char)c;
    return code >= 32 && code <= 126 ? glyphs[code - 32] : NULL;
}

void eapp_draw_text(const struct eapp_framebuffer *fb, int x, int y, const char *text,
                    uint16_t color, unsigned scale)
{
    if (!usable(fb) || !text || !scale)
        return;
    int cell = 6 * (int)scale, size = (int)scale;
    if (y >= (int)fb->height || y + 7 * size <= 0)
        return;
    for (; *text && x < (int)fb->width; ++text, x += cell) {
        const uint8_t *rows = x + 5 * size > 0 ? glyph(*text) : NULL;
        if (!rows)
            continue;
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (rows[row] & (16u >> column))
                    eapp_fill_rect(fb, x + column * size, y + row * size, size, size, color);
    }
}

unsigned eapp_text_width(const char *text, unsigned scale)
{
    unsigned length = 0;
    while (text && text[length])
        ++length;
    return length ? (length * 6 - 1) * scale : 0;
}

unsigned eapp_text_height(unsigned scale)
{
    return EAPP_FONT_HEIGHT * scale;
}

/* The glyphs that fit `width` from `text`: up to the last space before the
 * width runs out, the whole rest when it fits, or as many glyphs as fit when
 * the first word alone does not. `*next` is where the following line starts,
 * past the spaces or the newline. */
static unsigned line_length(const char *text, unsigned width, unsigned scale, const char **next)
{
    unsigned cell = 6 * scale, length = 0;
    int break_at = -1;
    for (; text[length] && text[length] != '\n'; ++length) {
        if (text[length] == ' ')
            break_at = (int)length;
        if (length * cell + 5 * scale > width && length) {
            if (break_at >= 0) {
                length = (unsigned)break_at;
                break;
            }
            *next = text + length;
            return length;
        }
    }
    const char *rest = text + length;
    while (*rest == ' ')
        ++rest;
    *next = *rest == '\n' ? rest + 1 : rest;
    return length;
}

unsigned eapp_draw_text_wrapped(const struct eapp_framebuffer *fb, int x, int y, unsigned width,
                                unsigned line_height, const char *text, uint16_t color,
                                unsigned scale)
{
    if (!text || !scale)
        return 0;
    unsigned lines = 0;
    while (*text == ' ')
        ++text;
    while (*text) {
        const char *next;
        unsigned length = line_length(text, width, scale, &next);
        if (usable(fb) && y < (int)fb->height) {
            char line[64];
            unsigned n = length < sizeof line - 1 ? length : sizeof line - 1;
            for (unsigned i = 0; i < n; ++i)
                line[i] = text[i];
            line[n] = 0;
            eapp_draw_text(fb, x, y, line, color, scale);
        }
        ++lines;
        y += (int)line_height;
        text = next;
        while (*text == ' ')
            ++text;
    }
    return lines;
}

void eapp_draw_text_in(const struct eapp_framebuffer *fb, int x, int y, unsigned width,
                       const char *text, uint16_t color, unsigned scale,
                       enum eapp_align align)
{
    int slack = (int)width - (int)eapp_text_width(text, scale);
    if (align == EAPP_ALIGN_CENTER)
        x += slack / 2;
    else if (align == EAPP_ALIGN_RIGHT)
        x += slack;
    eapp_draw_text(fb, x, y, text, color, scale);
}

/* Midpoint arcs as in circle(); each row of the top and bottom bands is one
 * span from the left arc to the right one. */
void eapp_fill_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    int limit = ((width < height ? width : height) - 1) / 2;
    if (radius > limit)
        radius = limit;
    if (radius <= 0) {
        eapp_fill_rect(fb, x, y, width, height, color);
        return;
    }
    int left = x + radius, right = x + width - 1 - radius;
    int top = y + radius, bottom = y + height - 1 - radius;
    eapp_fill_rect(fb, x, top, width, bottom - top + 1, color);
    int dx = radius, dy = 0, error = 1 - radius;
    while (dy <= dx) {
        span(fb, left - dx, top - dy, right - left + 1 + 2 * dx, color);
        span(fb, left - dx, bottom + dy, right - left + 1 + 2 * dx, color);
        span(fb, left - dy, top - dx, right - left + 1 + 2 * dy, color);
        span(fb, left - dy, bottom + dx, right - left + 1 + 2 * dy, color);
        ++dy;
        if (error < 0) {
            error += 2 * dy + 1;
        } else {
            --dx;
            error += 2 * (dy - dx) + 1;
        }
    }
}

void eapp_draw_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    int limit = ((width < height ? width : height) - 1) / 2;
    if (radius > limit)
        radius = limit;
    if (radius <= 0) {
        eapp_draw_rect(fb, x, y, width, height, color);
        return;
    }
    int left = x + radius, right = x + width - 1 - radius;
    int top = y + radius, bottom = y + height - 1 - radius;
    span(fb, left + 1, y, right - left - 1, color);
    span(fb, left + 1, y + height - 1, right - left - 1, color);
    eapp_fill_rect(fb, x, top + 1, 1, bottom - top - 1, color);
    eapp_fill_rect(fb, x + width - 1, top + 1, 1, bottom - top - 1, color);
    int dx = radius, dy = 0, error = 1 - radius;
    while (dy <= dx) {
        pixel(fb, left - dx, top - dy, color);
        pixel(fb, right + dx, top - dy, color);
        pixel(fb, left - dx, bottom + dy, color);
        pixel(fb, right + dx, bottom + dy, color);
        pixel(fb, left - dy, top - dx, color);
        pixel(fb, right + dy, top - dx, color);
        pixel(fb, left - dy, bottom + dx, color);
        pixel(fb, right + dy, bottom + dx, color);
        ++dy;
        if (error < 0) {
            error += 2 * dy + 1;
        } else {
            --dx;
            error += 2 * (dy - dx) + 1;
        }
    }
}
