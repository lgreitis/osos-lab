/* SPDX-License-Identifier: MIT */

/* Pixel work on an RGB565 framebuffer: the one a frame callback is handed,
 * or one the app makes. Every call clips to the framebuffer and takes
 * coordinates outside it; a NULL framebuffer or pixels draws nothing. */

#ifndef EAPP_DRAW_H
#define EAPP_DRAW_H

#include "eapp.h"

#ifdef __cplusplus
extern "C" {
#endif

enum eapp_align { EAPP_ALIGN_LEFT = -1, EAPP_ALIGN_CENTER, EAPP_ALIGN_RIGHT };

/* RGB565 pixels, rows `stride_pixels` apart; pixels equal to `transparent`
 * are skipped, -1 draws every pixel. */
struct eapp_bitmap {
    const uint16_t *pixels;
    uint32_t width, height, stride_pixels;
    int32_t transparent;
};

void eapp_clear(const struct eapp_framebuffer *fb, uint16_t color);
void eapp_fill_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color);
/* The 1 px outline just inside the rectangle. */
void eapp_draw_rect(const struct eapp_framebuffer *fb, int x, int y, int width, int height,
                    uint16_t color);
void eapp_draw_pixel(const struct eapp_framebuffer *fb, int x, int y, uint16_t color);
/* Both ends included. */
void eapp_draw_line(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                    uint16_t color);
void eapp_fill_triangle(const struct eapp_framebuffer *fb, int x0, int y0, int x1, int y1,
                        int x2, int y2, uint16_t color);
/* Blends `color` over the triangle with corners x[i], y[i] in 16.16 pixels;
 * alpha goes linearly from alpha[i] (0 to 255) at each corner. Pixels whose
 * centres lie inside are drawn, so neighbouring triangles never both draw a
 * shared edge. */
void eapp_blend_triangle(const struct eapp_framebuffer *fb, const int32_t x[3],
                         const int32_t y[3], const uint8_t alpha[3], uint16_t color);
void eapp_draw_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color);
void eapp_fill_circle(const struct eapp_framebuffer *fb, int cx, int cy, int radius,
                      uint16_t color);
/* The 1 px outline of a rectangle with quarter-circle corners of `radius`,
 * cut to less than half the shorter side. */
void eapp_draw_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color);
/* The same, filled. */
void eapp_fill_round_rect(const struct eapp_framebuffer *fb, int x, int y, int width,
                          int height, int radius, uint16_t color);

/* A framebuffer in the heap, `width` x `height` pixels with the width as
 * stride, cleared to black, for drawing that outlives a frame; every call
 * here takes it in place of the frame's. NULL for a side of 0 or over
 * EAPP_FRAMEBUFFER_MAX_SIDE, or no memory. */
#define EAPP_FRAMEBUFFER_MAX_SIDE 4096
struct eapp_framebuffer *eapp_framebuffer_create(unsigned width, unsigned height);
/* Only one from eapp_framebuffer_create. */
void eapp_framebuffer_free(struct eapp_framebuffer *fb);
/* The RGB565 value at (x, y), -1 outside the framebuffer. */
int32_t eapp_read_pixel(const struct eapp_framebuffer *fb, int x, int y);
/* The `width` by `height` pixels of `source` from (source_x, source_y) onto
 * (x, y), cut to both framebuffers, which must not share pixels. */
void eapp_copy_rect(const struct eapp_framebuffer *fb, int x, int y,
                    const struct eapp_framebuffer *source, int source_x, int source_y,
                    int width, int height);

/* A picture of the package, from IMAGES in the Makefile (art/tiles.png is
 * read as "art/tiles.eimg", which the generated images.h names), into one
 * heap block: width x height x 2 bytes plus the struct. NULL for a missing
 * file, one in another format, or no memory. */
struct eapp_bitmap *eapp_bitmap_load(const char *path);
void eapp_bitmap_free(struct eapp_bitmap *bitmap);

/* A pack of pictures, from IMAGE_PACKS in the Makefile, read in one request
 * into one heap block: the file plus a bitmap per entry. images.h names the
 * pack's file and each entry's index. eapp_pack_bitmap is entry `index` as
 * a bitmap over the pack's memory, valid until the pack is freed; NULL past
 * the end or for an entry that is texture data rather than RGB565 (an
 * IMAGES_ALPHA picture), which eapp_gl_texture_from_pack takes. */
struct eapp_pack;
struct eapp_pack *eapp_pack_load(const char *path);
unsigned eapp_pack_count(const struct eapp_pack *pack);
const struct eapp_bitmap *eapp_pack_bitmap(const struct eapp_pack *pack, unsigned index);
void eapp_pack_free(struct eapp_pack *pack);

void eapp_draw_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                      const struct eapp_bitmap *bitmap);
/* The `width` by `height` part of the bitmap from (source_x, source_y), cut
 * to the bitmap. */
void eapp_draw_bitmap_part(const struct eapp_framebuffer *fb, int x, int y,
                           const struct eapp_bitmap *bitmap, int source_x, int source_y,
                           int width, int height);
/* One bit per pixel, most significant bit first, each row padded to a byte;
 * set bits are drawn in `color`. */
void eapp_draw_mono_bitmap(const struct eapp_framebuffer *fb, int x, int y,
                           const uint8_t *bits, unsigned width, unsigned height,
                           uint16_t color);

/* Text in the SDK's 5 x 7 font, the printable ASCII characters 32 to 126;
 * any other byte leaves its cell empty. Every font pixel is `scale` pixels
 * wide and high. A glyph cell is EAPP_FONT_CELL x EAPP_FONT_HEIGHT font
 * pixels; the last glyph has no trailing gap.
 *
 * The text call is adapted from RepriseOS game-sdk/include/game_api.h (8fc840d) by
 * lgreitis. */
#define EAPP_FONT_WIDTH 5
#define EAPP_FONT_HEIGHT 7
#define EAPP_FONT_CELL 6
void eapp_draw_text(const struct eapp_framebuffer *fb, int x, int y, const char *text,
                    uint16_t color, unsigned scale);
/* The same text placed by `align` across the `width` pixels from `x`. */
void eapp_draw_text_in(const struct eapp_framebuffer *fb, int x, int y, unsigned width,
                       const char *text, uint16_t color, unsigned scale,
                       enum eapp_align align);
/* The text broken into lines of at most `width` pixels at spaces, or inside
 * a word longer than that, and at every newline, `line_height` pixels
 * apart; spaces at a line's start are dropped and lines of more than 63
 * glyphs are cut. Returns the lines, also for a NULL framebuffer, to
 * measure. */
unsigned eapp_draw_text_wrapped(const struct eapp_framebuffer *fb, int x, int y, unsigned width,
                                unsigned line_height, const char *text, uint16_t color,
                                unsigned scale);
unsigned eapp_text_width(const char *text, unsigned scale);
unsigned eapp_text_height(unsigned scale);

#ifdef __cplusplus
}
#endif
#endif
