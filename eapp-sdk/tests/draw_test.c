/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing calls on a 16 x 12 framebuffer with a 20 pixel stride, checked
 * pixel by pixel: clipping on every side, the shapes' extents and symmetry,
 * blended triangles' coverage and alpha, bitmap keys and cuts, mono bit
 * order, text metrics, the random stream, the wheel steps, the menu, the
 * message box, the table, the page, the dialog and an off-screen
 * framebuffer. */

#include "eapp_draw.h"
#include "eapp_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 12
#define STRIDE 20
#define INK 0xffff
#define BG 0x0000

static uint16_t pixels[H * STRIDE];
static const struct eapp_framebuffer fb = {pixels, W, H, STRIDE};

static void clear(void)
{
    memset(pixels, 0, sizeof pixels);
}

static uint16_t at(int x, int y)
{
    return pixels[y * STRIDE + x];
}

static unsigned count(void)
{
    unsigned n = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            n += at(x, y) == INK;
    return n;
}

/* Nothing lands in the stride padding. */
static void padding_clean(void)
{
    for (int y = 0; y < H; ++y)
        for (int x = W; x < STRIDE; ++x)
            assert(at(x, y) == BG);
}

static void rects(void)
{
    clear();
    eapp_fill_rect(&fb, -2, -2, 5, 5, INK);
    assert(count() == 9 && at(2, 2) == INK && at(3, 3) == BG);
    eapp_fill_rect(&fb, 14, 10, 10, 10, INK);
    assert(count() == 13 && at(15, 11) == INK);
    padding_clean();

    clear();
    eapp_draw_rect(&fb, 2, 1, 5, 4, INK);
    assert(count() == 14);
    assert(at(2, 1) == INK && at(6, 1) == INK && at(2, 4) == INK && at(6, 4) == INK);
    assert(at(3, 2) == BG && at(4, 2) == BG);
    eapp_draw_rect(&fb, 0, 0, 1, 1, INK);
    assert(count() == 15 && at(0, 0) == INK);
    eapp_draw_rect(&fb, 0, 0, 0, 5, INK);
    assert(count() == 15);
    eapp_draw_rect(&fb, -1, -1, W + 2, H + 2, INK);
    assert(count() == 15);

    clear();
    eapp_clear(&fb, INK);
    assert(count() == W * H);
    padding_clean();
    eapp_fill_rect(NULL, 0, 0, 1, 1, INK);
}

static void lines(void)
{
    clear();
    eapp_draw_pixel(&fb, 3, 4, INK);
    eapp_draw_pixel(&fb, -1, 4, INK);
    eapp_draw_pixel(&fb, 3, H, INK);
    assert(count() == 1 && at(3, 4) == INK);

    clear();
    eapp_draw_line(&fb, 1, 1, 6, 1, INK);
    assert(count() == 6 && at(1, 1) == INK && at(6, 1) == INK);
    clear();
    eapp_draw_line(&fb, 6, 8, 6, 3, INK);
    assert(count() == 6 && at(6, 3) == INK && at(6, 8) == INK);
    clear();
    eapp_draw_line(&fb, 0, 0, 5, 5, INK);
    for (int i = 0; i <= 5; ++i)
        assert(at(i, i) == INK);
    assert(count() == 6);
    clear();
    eapp_draw_line(&fb, 10, 2, 0, 7, INK);
    assert(at(10, 2) == INK && at(0, 7) == INK && count() == 11);
    clear();
    eapp_draw_line(&fb, -5, 5, 20, 5, INK);
    assert(count() == W);
    padding_clean();
}

static void triangles(void)
{
    clear();
    eapp_fill_triangle(&fb, 2, 1, 2, 7, 8, 7, INK);
    assert(at(2, 1) == INK && at(2, 7) == INK && at(8, 7) == INK);
    assert(at(3, 1) == BG && at(8, 6) == BG && at(4, 5) == INK);
    unsigned right = count();
    assert(right >= 25 && right <= 28);

    clear();
    eapp_fill_triangle(&fb, 8, 7, 2, 1, 2, 7, INK);
    assert(count() == right);

    clear();
    eapp_fill_triangle(&fb, 5, 1, 5, 1, 5, 1, INK);
    assert(count() == 1 && at(5, 1) == INK);
    clear();
    eapp_fill_triangle(&fb, 1, 3, 9, 3, 5, 3, INK);
    assert(count() == 9);

    clear();
    eapp_fill_triangle(&fb, -4, -4, 30, -4, -4, 30, INK);
    assert(at(0, 0) == INK && at(15, 0) == INK && at(0, 11) == INK && at(15, 11) == INK);
    assert(count() == W * H);
    padding_clean();
}

#define PX(v) ((int32_t)((v) * 65536))

static void blend(const int32_t x[3], const int32_t y[3], uint8_t a0, uint8_t a1, uint8_t a2)
{
    const uint8_t alpha[3] = {a0, a1, a2};
    eapp_blend_triangle(&fb, x, y, alpha, INK);
}

static void blends(void)
{
    /* Two triangles that make the square 0..8 cover its 64 pixels once:
     * opaque they fill it exactly, half way every pixel gets one blend. */
    const int32_t ux[3] = {PX(0), PX(8), PX(0)}, uy[3] = {PX(0), PX(0), PX(8)};
    const int32_t lx[3] = {PX(8), PX(8), PX(0)}, ly[3] = {PX(0), PX(8), PX(8)};
    clear();
    blend(ux, uy, 255, 255, 255);
    blend(lx, ly, 255, 255, 255);
    assert(count() == 64 && at(7, 7) == INK && at(8, 0) == BG && at(0, 8) == BG);
    clear();
    blend(ux, uy, 128, 128, 128);
    blend(lx, ly, 128, 128, 128);
    uint16_t half = at(0, 0);
    assert(half != BG && half != INK);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            assert(at(x, y) == half);
    assert(at(8, 3) == BG && at(3, 8) == BG);

    /* Transparent corners leave the pixels alone; the alpha falls along a
     * row away from the opaque corner; the corner order does not matter. */
    clear();
    blend(ux, uy, 0, 0, 0);
    assert(count() == 0 && at(2, 2) == BG);
    clear();
    blend(ux, uy, 255, 0, 0);
    for (int x = 1; x < 7; ++x)
        assert((at(x, 0) >> 11) <= (at(x - 1, 0) >> 11));
    assert(at(0, 0) >> 11 > at(6, 0) >> 11);
    uint16_t before[H * STRIDE];
    memcpy(before, pixels, sizeof pixels);
    clear();
    const int32_t rx[3] = {PX(0), PX(0), PX(8)}, ry[3] = {PX(8), PX(0), PX(0)};
    blend(rx, ry, 0, 255, 0);
    assert(memcmp(before, pixels, sizeof pixels) == 0);

    /* Half pixels: a corner past a centre takes that pixel in. */
    clear();
    const int32_t hx[3] = {PX(2), PX(4.5), PX(2)}, hy[3] = {PX(2), PX(2), PX(4.5)};
    blend(hx, hy, 255, 255, 255);
    assert(at(2, 2) == INK && at(3, 2) == INK && at(2, 3) == INK && at(4, 2) == BG);

    /* Nothing for a flat triangle; clipped on every side. */
    clear();
    const int32_t fx[3] = {PX(1), PX(9), PX(5)}, fy[3] = {PX(3), PX(3), PX(3)};
    blend(fx, fy, 255, 255, 255);
    assert(count() == 0);
    clear();
    const int32_t cx[3] = {PX(-40), PX(90), PX(-40)}, cy[3] = {PX(-40), PX(-40), PX(90)};
    blend(cx, cy, 255, 255, 255);
    assert(count() == W * H);
    padding_clean();
}

static void circles(void)
{
    clear();
    eapp_draw_circle(&fb, 7, 5, 4, INK);
    assert(at(3, 5) == INK && at(11, 5) == INK && at(7, 1) == INK && at(7, 9) == INK);
    assert(at(7, 5) == BG && at(2, 5) == BG);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (14 - x >= 0 && 14 - x < W && 10 - y >= 0 && 10 - y < H)
                assert(at(x, y) == at(14 - x, 10 - y));
    unsigned outline = count();
    uint16_t ring[H * STRIDE];
    memcpy(ring, pixels, sizeof ring);

    clear();
    eapp_fill_circle(&fb, 7, 5, 4, INK);
    assert(at(7, 5) == INK && at(3, 5) == INK && at(7, 1) == INK && at(2, 5) == BG);
    assert(at(3, 2) == BG && at(11, 8) == BG);
    unsigned filled = count();
    assert(filled > outline);
    /* The fill covers the outline and nothing outside its rows' extents. */
    for (int y = 0; y < H; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < W; ++x)
            if (ring[y * STRIDE + x] == INK) {
                if (first < 0)
                    first = x;
                last = x;
            }
        for (int x = 0; x < W; ++x)
            assert((at(x, y) == INK) == (first >= 0 && x >= first && x <= last));
    }
    for (int y = 0; y < H; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < W; ++x)
            if (at(x, y) == INK) {
                if (first < 0)
                    first = x;
                last = x;
            }
        for (int x = first; first >= 0 && x <= last; ++x)
            assert(at(x, y) == INK);
    }

    clear();
    eapp_draw_circle(&fb, 7, 5, 1, INK);
    assert(count() == 4 && at(7, 5) == BG);
    clear();
    eapp_fill_circle(&fb, 7, 5, 1, INK);
    assert(count() == 5 && at(7, 5) == INK && at(6, 4) == BG);
    clear();
    eapp_fill_circle(&fb, 7, 5, 0, INK);
    assert(count() == 1);
    clear();
    eapp_fill_circle(&fb, 0, 0, 30, INK);
    assert(count() == W * H);
    padding_clean();
}

static void round_rects(void)
{
    clear();
    eapp_fill_round_rect(&fb, 1, 1, 12, 9, 3, INK);
    /* The corners are cut, the edges and the middle filled. */
    assert(at(1, 1) == BG && at(12, 1) == BG && at(1, 9) == BG && at(12, 9) == BG);
    assert(at(4, 1) == INK && at(1, 4) == INK && at(6, 5) == INK && at(12, 4) == INK);
    assert(at(9, 9) == INK && at(0, 5) == BG && at(13, 5) == BG && at(6, 0) == BG);
    assert(at(6, 10) == BG);
    for (int y = 1; y <= 9; ++y)
        for (int x = 1; x <= 12; ++x)
            assert(at(x, y) == at(13 - x, y) && at(x, y) == at(x, 10 - y));
    unsigned filled = count();
    assert(filled < 12 * 9);
    uint16_t fill[H * STRIDE];
    memcpy(fill, pixels, sizeof fill);

    /* The outline lies on the fill and is thinner. */
    clear();
    eapp_draw_round_rect(&fb, 1, 1, 12, 9, 3, INK);
    assert(at(1, 1) == BG && at(4, 1) == INK && at(1, 4) == INK && at(6, 5) == BG);
    assert(at(12, 4) == INK && at(6, 9) == INK);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            assert(at(x, y) == BG || fill[y * STRIDE + x] == INK);
    assert(count() < filled);

    /* No radius is a plain rectangle; a radius of half the shorter side or
     * more is cut below it; clipping covers the framebuffer. */
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 0, INK);
    assert(count() == 24 && at(2, 2) == INK);
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 10, INK);
    assert(count() < 24 && at(2, 2) == BG && at(4, 3) == INK);
    unsigned cut = count();
    clear();
    eapp_fill_round_rect(&fb, 2, 2, 6, 4, 1, INK);
    assert(count() == cut);
    clear();
    eapp_fill_round_rect(&fb, -4, -4, 40, 30, 6, INK);
    assert(count() == W * H);
    padding_clean();
}

static void bitmaps(void)
{
    static const uint16_t art[3 * 5] = {
        1, 2, 3, 4, 9, /* a 4-wide picture with a 5 pixel stride */
        5, 0, 6, 7, 9, /* 0 is the key */
        8, 9, 0, 1, 9,
    };
    struct eapp_bitmap bitmap = {art, 4, 3, 5, -1};

    clear();
    eapp_draw_bitmap(&fb, 1, 1, &bitmap);
    assert(at(1, 1) == 1 && at(4, 1) == 4 && at(5, 1) == BG);
    assert(at(2, 2) == 0 && at(3, 3) == 0 && at(4, 3) == 1);

    clear();
    eapp_fill_rect(&fb, 0, 0, W, H, 7);
    bitmap.transparent = 0;
    eapp_draw_bitmap(&fb, 1, 1, &bitmap);
    assert(at(2, 2) == 7 && at(3, 3) == 7 && at(1, 1) == 1 && at(4, 3) == 1);

    clear();
    bitmap.transparent = -1;
    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 1, 1, 2, 2);
    assert(at(0, 0) == 0 && at(1, 0) == 6 && at(0, 1) == 9 && at(1, 1) == 0);
    assert(count() == 0 && at(2, 0) == BG && at(0, 2) == BG);

    clear();
    eapp_draw_bitmap_part(&fb, 3, 3, &bitmap, -1, -1, 10, 10);
    assert(at(3, 3) == BG && at(4, 4) == 1 && at(7, 4) == 4 && at(8, 4) == BG && at(7, 6) == 1);

    clear();
    eapp_draw_bitmap(&fb, -2, -1, &bitmap);
    assert(at(0, 0) == 6 && at(1, 0) == 7 && at(0, 1) == 0 && at(1, 1) == 1 && at(2, 1) == BG);

    clear();
    eapp_draw_bitmap(&fb, 14, 10, &bitmap);
    assert(at(14, 10) == 1 && at(15, 10) == 2 && at(14, 11) == 5 && at(15, 11) == 0);
    padding_clean();

    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 4, 0, 2, 2);
    eapp_draw_bitmap_part(&fb, 0, 0, &bitmap, 0, 3, 2, 2);
    eapp_draw_bitmap(&fb, W, 0, &bitmap);
    eapp_draw_bitmap(&fb, 0, H, &bitmap);
    eapp_draw_bitmap(&fb, 0, 0, NULL);

    static const uint8_t mono[] = {
        0xa5, 0x80, /* 1010 0101 1 */
        0x00, 0x00,
        0xff, 0x80,
    };
    clear();
    eapp_draw_mono_bitmap(&fb, 2, 2, mono, 9, 3, INK);
    assert(at(2, 2) == INK && at(3, 2) == BG && at(4, 2) == INK && at(5, 2) == BG);
    assert(at(6, 2) == BG && at(7, 2) == INK && at(9, 2) == INK && at(10, 2) == INK);
    assert(at(11, 2) == BG && at(2, 3) == BG);
    assert(count() == 5 + 9);
    clear();
    eapp_draw_mono_bitmap(&fb, -1, -1, mono, 9, 3, INK);
    assert(at(0, 1) == INK && at(7, 1) == INK && at(8, 1) == BG && count() == 8);
    padding_clean();
}

static void text(void)
{
    assert(eapp_text_width("", 1) == 0);
    assert(eapp_text_width(NULL, 2) == 0);
    assert(eapp_text_width("A", 1) == 5);
    assert(eapp_text_width("AB", 1) == 11);
    assert(eapp_text_width("AB", 3) == 33);
    assert(eapp_text_width("A B", 1) == 17);

    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 1);
    assert(count() == 5 && at(1, 4) == INK && at(5, 4) == INK && at(1, 3) == BG);
    clear();
    eapp_draw_text(&fb, 1, 1, "--", INK, 1);
    assert(count() == 10 && at(6, 4) == BG && at(7, 4) == INK && at(11, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, 1, "- -", INK, 1);
    assert(count() == 8 && at(13, 4) == INK && at(15, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 2);
    assert(count() == 20 && at(1, 7) == INK && at(10, 8) == INK && at(1, 6) == BG);
    clear();
    eapp_draw_text(&fb, 1, 1, "-", INK, 0);
    assert(count() == 0);

    clear();
    eapp_draw_text(&fb, -2, 1, "-", INK, 1);
    assert(count() == 3 && at(0, 4) == INK && at(2, 4) == INK);
    clear();
    eapp_draw_text(&fb, 13, 1, "-", INK, 1);
    assert(count() == 3 && at(13, 4) == INK && at(15, 4) == INK);
    clear();
    eapp_draw_text(&fb, 1, -3, ":", INK, 1);
    assert(count() == 2 && at(3, 1) == INK && at(3, 2) == INK);
    clear();
    eapp_draw_text(&fb, 1, 9, ":", INK, 1);
    assert(count() == 2 && at(3, 10) == INK && at(3, 11) == INK);
    clear();
    /* Lower case has its own glyphs; bytes outside printable ASCII leave
     * their cell empty. */
    eapp_draw_text(&fb, 1, 1, "x", INK, 1);
    uint16_t lower[H * STRIDE];
    memcpy(lower, pixels, sizeof lower);
    clear();
    eapp_draw_text(&fb, 1, 1, "X", INK, 1);
    assert(memcmp(lower, pixels, sizeof lower) && count() == 13);
    clear();
    eapp_draw_text(&fb, 1, 1, "\x7f\xc3\xa4\t", INK, 1);
    assert(count() == 0);
    eapp_draw_text(&fb, 1, 1, ".", INK, 1);
    assert(count() == 4 && at(2, 6) == INK && at(3, 7) == INK);
    assert(eapp_text_height(2) == 14);
    padding_clean();

    /* Wrapping breaks at spaces, inside a word too long for the width, and
     * at newlines; the count comes back without a framebuffer too. */
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 40, 8, "ab cd ef", 0, 1) == 2);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 100, 8, "ab cd ef", 0, 1) == 1);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 17, 8, "abcdefg", 0, 1) == 3);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 100, 8, "a\nb\n", 0, 1) == 2);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 100, 8, "", 0, 1) == 0);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 100, 8, "ab", 0, 0) == 0);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 11, 8, "ab  cd", 0, 1) == 2);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 23, 8, " abcdefgh", 0, 1) == 2);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 23, 8, "ab \ncd", 0, 1) == 2);
    assert(eapp_draw_text_wrapped(NULL, 0, 0, 23, 8, "   ", 0, 1) == 0);
    clear();
    assert(eapp_draw_text_wrapped(&fb, 0, 0, 11, 8, "- -", INK, 1) == 2);
    assert(at(0, 3) == INK && at(0, 11) == INK && count() == 10);

    clear();
    eapp_draw_text_in(&fb, 0, 1, W, "-", INK, 1, EAPP_ALIGN_LEFT);
    assert(at(0, 4) == INK && at(4, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 0, 1, W, "-", INK, 1, EAPP_ALIGN_RIGHT);
    assert(at(11, 4) == INK && at(15, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 2, 1, 10, "-", INK, 1, EAPP_ALIGN_CENTER);
    assert(at(4, 4) == INK && at(8, 4) == INK && count() == 5);
    clear();
    eapp_draw_text_in(&fb, 2, 1, 3, "-", INK, 1, EAPP_ALIGN_RIGHT);
    assert(at(0, 4) == INK && at(4, 4) == INK && count() == 5);
}

static void random_stream(void)
{
    uint32_t a = eapp_random(), b = eapp_random();
    assert(a && b && a != b);
    eapp_random_seed(12345);
    uint32_t first[4];
    for (unsigned i = 0; i < 4; ++i)
        first[i] = eapp_random();
    eapp_random_seed(12345);
    for (unsigned i = 0; i < 4; ++i)
        assert(eapp_random() == first[i]);
    assert(first[0] != first[1] && first[1] != first[2]);
    eapp_random_seed(0);
    uint32_t zero = eapp_random();
    assert(zero);
    eapp_random_seed(1);
    assert(eapp_random() != zero);
    eapp_random_seed(0);
    assert(eapp_random() == zero);
    /* A seed with one set bit still gives large numbers at once. */
    eapp_random_seed(1);
    assert(eapp_random() > 0xffff && eapp_random() > 0xffff);
    /* Every value below the count comes up, nothing else does. */
    unsigned seen[7] = {0};
    for (unsigned i = 0; i < 2000; ++i) {
        uint32_t value = eapp_random_below(7);
        assert(value < 7);
        ++seen[value];
    }
    for (unsigned i = 0; i < 7; ++i)
        assert(seen[i] > 200);
    assert(eapp_random_below(0) == 0 && eapp_random_below(1) == 0);
}


/* The wheel steps: a step per threshold, the remainder carried, dropped on
 * a lift, one step a frame on a slow turn, and doubling on a fast turn in
 * one direction. The menu follows them and keeps the selection in view. */
static struct eapp_frame wheel_frame(uint32_t ticks_ms, int32_t delta, int touched)
{
    struct eapp_frame frame = {0};
    frame.ticks_ms = ticks_ms;
    frame.wheel.delta = delta;
    frame.wheel.touched = (uint8_t)touched;
    return frame;
}

static void wheel_steps(void)
{
    struct eapp_wheel_steps steps = {0};
    struct eapp_frame frame = wheel_frame(1000, 3, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == 0);
    frame = wheel_frame(1200, 1, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == 1 && steps.remainder == 0);
    frame = wheel_frame(1400, -5, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == -1 && steps.remainder == -1);
    frame = wheel_frame(1600, 0, 0);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == 0 && steps.remainder == 0);
    /* A slow turn gives at most one step a frame. */
    frame = wheel_frame(5000, 12, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == 1);
    /* A fast turn: 8 positions in 16 ms is well over a turn in four
     * seconds; the multiplier doubles 350 ms after the turn began. */
    int32_t total = 0;
    for (uint32_t t = 6000; t < 6000 + 1000; t += 16) {
        frame = wheel_frame(t, 8, 1);
        total += eapp_wheel_steps_consume(&steps, &frame, 4);
    }
    assert(total > 63 * 2 && steps.multiplier == 4);
    /* A direction change resets the multiplier. */
    frame = wheel_frame(7016, -8, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == -2 && steps.multiplier == 1);
    assert(eapp_wheel_steps_consume(NULL, &frame, 4) == 0);
    assert(eapp_wheel_steps_consume(&steps, &frame, 0) == 0);
    /* The lift clears a remainder from the touch before. */
    steps = (struct eapp_wheel_steps){0};
    frame = wheel_frame(9000, 3, 1);
    eapp_wheel_steps_consume(&steps, &frame, 4);
    frame = wheel_frame(9100, 0, 0);
    eapp_wheel_steps_consume(&steps, &frame, 4);
    frame = wheel_frame(9200, 1, 1);
    assert(eapp_wheel_steps_consume(&steps, &frame, 4) == 0);
}

static void menu(void)
{
    static const struct eapp_menu_item items[] = {
        {"A", NULL}, {"B", "1"}, {"C", NULL}, {"D", NULL}, {"E", NULL},
    };
    struct eapp_menu m = {.title = NULL, .items = items, .count = 5};
    struct eapp_input_event next = {EAPP_BUTTON_NEXT, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event previous = {EAPP_BUTTON_PREVIOUS, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event center = {EAPP_BUTTON_CENTER, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event back = {EAPP_BUTTON_MENU, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event play = {EAPP_BUTTON_PLAY, EAPP_INPUT_PRESS, 0};
    assert(eapp_menu_input(&m, &previous) == EAPP_MENU_NONE && m.selected == 0);
    assert(eapp_menu_input(&m, &next) == EAPP_MENU_NONE && m.selected == 1);
    assert(eapp_menu_input(&m, &play) == EAPP_MENU_NONE && m.selected == 1);
    assert(eapp_menu_input(&m, &center) == EAPP_MENU_SELECT);
    assert(eapp_menu_input(&m, &back) == EAPP_MENU_BACK);
    struct eapp_frame frame = wheel_frame(100, 4, 1);
    eapp_menu_scroll(&m, &frame);
    assert(m.selected == 2);
    frame = wheel_frame(5000, 40, 1);
    eapp_menu_scroll(&m, &frame);
    assert(m.selected == 3); /* A slow turn moves one row. */
    frame = wheel_frame(5016, 40, 1);
    eapp_menu_scroll(&m, &frame);
    assert(m.selected == 4); /* Clamped to the last row. */

    /* Rows are 12 px: the 12-row framebuffer shows one row, so `first`
     * follows the selection; the selected row is a light bar with dark
     * text. */
    clear();
    eapp_menu_draw(&m, &fb);
    assert(m.first == 4 && at(0, 0) == 0xffff && at(W - 1, H - 1) == 0xffff);
    assert(at(4, 2) == 0 && count() == W * H - 18); /* "E" has 18 pixels. */
    m.selected = 1;
    m.scale = 1;
    m.background = 0x0001;
    m.selected_background = 0x0002;
    m.selected_text = 0x0003;
    m.text = 0x0004;
    clear();
    eapp_menu_draw(&m, &fb);
    assert(m.first == 1 && at(0, 0) == 0x0002 && at(4, 2) == 0x0003);
    assert(at(9, 2) == 0x0003); /* The value "1", right-aligned 4 px in. */
    /* The title takes the first row; here it leaves no room for rows. */
    m.title = "T";
    m.selected = 0;
    clear();
    eapp_menu_draw(&m, &fb);
    assert(m.first == 1 && at(0, 0) == 0x0001 && at(4, 2) == 0x8410 && at(4, 3) == 0x0001);
    /* A region: cleared and drawn inside it only. */
    m.title = NULL;
    m.selected = 2;
    m.x = 2;
    m.y = 0;
    m.width = 10;
    m.height = 0;
    clear();
    eapp_menu_draw(&m, &fb);
    assert(at(0, 0) == 0 && at(1, 5) == 0 && at(12, 5) == 0 && at(2, 0) == 0x0002);
    assert(at(11, 11) == 0x0002 && at(6, 3) == 0x0003); /* "C" at x 2 + 4. */
    /* A label and a value wider than the region stop at its edges. */
    static const struct eapp_menu_item wide[] = {{"ABCD", "1234"}, {"E", NULL}};
    m.items = wide;
    m.count = 2;
    m.selected = 0;
    m.width = 8;
    clear();
    eapp_menu_draw(&m, &fb);
    unsigned inked = 0;
    for (int y = 0; y < H; ++y) {
        assert(at(0, y) == 0 && at(1, y) == 0 && at(10, y) == 0 && at(W - 1, y) == 0);
        for (int x = 2; x < 10; ++x)
            inked += at(x, y) == 0x0003;
    }
    assert(inked); /* "A" and the start of "B" are drawn, the rest is cut. */
    m.items = items;
    m.count = 5;
    m.width = 10;
    eapp_menu_draw(NULL, &fb);
    eapp_menu_scroll(&m, NULL);
    eapp_menu_scroll(NULL, &frame);
    assert(eapp_menu_input(NULL, &next) == EAPP_MENU_NONE);
}

/* The screens: a 40 x 24 framebuffer, with the menu's colour scheme of
 * 0x0001 for the background and other low values for the parts. */
#define SW 40
#define SH 24
static uint16_t screen_pixels[SH * SW];
static const struct eapp_framebuffer screen = {screen_pixels, SW, SH, SW};

static uint16_t sat(int x, int y)
{
    return screen_pixels[y * SW + x];
}

/* The pixels of `color` inside the box, and that none lie outside it. */
static unsigned boxed(uint16_t color, int x, int y, int width, int height)
{
    unsigned inside = 0;
    for (int py = 0; py < SH; ++py)
        for (int px = 0; px < SW; ++px) {
            if (sat(px, py) != color)
                continue;
            assert(px >= x && px < x + width && py >= y && py < y + height);
            ++inside;
        }
    return inside;
}

/* Whether the rows from y are all `color`. */
static int rows_are(uint16_t color, int y, int height)
{
    for (int py = y; py < y + height; ++py)
        for (int px = 0; px < SW; ++px)
            if (sat(px, py) != color)
                return 0;
    return 1;
}

static void message(void)
{
    /* "A" is 5 px wide: a 21 x 20 box centred at 9, 2, a 1 px frame, the
     * glyph centred at 17, 8. */
    memset(screen_pixels, 0, sizeof screen_pixels);
    eapp_message_draw(&screen, "A", 1, 0x0001, 0x0002, 0x0003);
    assert(boxed(0x0002, 9, 2, 21, 20) == 2 * 21 + 2 * 18);
    assert(sat(9, 2) == 0x0002 && sat(29, 21) == 0x0002 && sat(10, 3) == 0x0001);
    assert(boxed(0x0003, 17, 8, 5, 7) && sat(8, 2) == 0 && sat(30, 21) == 0);
    /* Scale 2: the frame is 2 px, the box 40 high and the text 10 px wide
     * with 16 either side, so 42 wide: it overhangs the screen by a pixel
     * each side and 8 above and below, leaving one frame column at each
     * edge and the glyph at 15, 5. */
    memset(screen_pixels, 0, sizeof screen_pixels);
    eapp_message_draw(&screen, "A", 2, 0x0001, 0x0002, 0x0003);
    assert(sat(0, 0) == 0x0002 && sat(1, 0) == 0x0001 && sat(39, 23) == 0x0002);
    assert(boxed(0x0002, 0, 0, SW, SH) == 2 * SH && boxed(0x0003, 15, 5, 10, 14));
    eapp_message_draw(NULL, "A", 1, 0, 0, 0);
    eapp_message_draw(&screen, NULL, 1, 0, 0, 0);
}

/* The table on a 40 x 48 framebuffer, which holds two 13 px rows under
 * the headings. */
#define TH 48
static uint16_t tall_pixels[TH * SW];
static const struct eapp_framebuffer tall = {tall_pixels, SW, TH, SW};

static uint16_t tat(int x, int y)
{
    return tall_pixels[y * SW + x];
}

/* The pixels of `color` on the tall framebuffer inside the box, and that
 * none lie outside it. */
/* Whether the tall framebuffer's rows from y are all `color`. */
static int rows_are_tall(uint16_t color, int y, int height)
{
    for (int py = y; py < y + height; ++py)
        for (int px = 0; px < SW; ++px)
            if (tat(px, py) != color)
                return 0;
    return 1;
}

static unsigned tall_boxed(uint16_t color, int x, int y, int width, int height)
{
    unsigned inside = 0;
    for (int py = 0; py < TH; ++py)
        for (int px = 0; px < SW; ++px) {
            if (tat(px, py) != color)
                continue;
            assert(px >= x && px < x + width && py >= y && py < y + height);
            ++inside;
        }
    return inside;
}

static void table(void)
{
    static const struct eapp_table_column columns[] = {
        {"H", 0, 10, EAPP_ALIGN_LEFT},
        {"V", 10, 20, EAPP_ALIGN_RIGHT},
    };
    static const char *const cells[] = {"a", "1", "b", NULL, "c", NULL};
    struct eapp_table t = {.columns = columns,
                           .column_count = 2,
                           .cells = cells,
                           .rows = 3,
                           .highlighted = 0,
                           .background = 0x0001,
                           .highlight = 0x0002,
                           .highlight_text = 0x0003,
                           .heading_text = 0x0004,
                           .title_text = 0x0005,
                           .text = 0x0006};
    /* Without a title the headings sit 4 px in, their text 3 px down the
     * 13 px row; the first row's bar spans 4 to 35 from y 17 and the "1"
     * ends in its column at x 29; the second row follows at 30 and the
     * third, ending at 56, is left out. */
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_table_draw(&t, &tall);
    assert(tall_boxed(0x0004, 0, 7, 30, 7));
    assert(tat(4, 17) == 0x0002 && tat(35, 29) == 0x0002 && tat(3, 17) == 0x0001);
    assert(tat(36, 17) == 0x0001 && tat(4, 16) == 0x0001 && tat(4, 30) == 0x0001);
    assert(tall_boxed(0x0003, 0, 20, 30, 7) && tat(27, 20) == 0x0003 && tat(30, 20) == 0x0002);
    assert(tall_boxed(0x0006, 0, 33, 10, 7) && tat(0, 33) == 0x0006);
    /* A title one size larger: 4 px of inset, 14 of title, 4 of gap, then
     * the headings' row and one whole row. */
    t.title = "T";
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_table_draw(&t, &tall);
    assert(tat(4, 4) == 0x0005 && tat(13, 4) == 0x0005 && tat(4, 18) == 0x0001);
    assert(tall_boxed(0x0004, 0, 25, 30, 7) && tat(4, 35) == 0x0002 && tat(4, 47) == 0x0002);
    assert(tall_boxed(0x0006, 0, 0, SW, TH) == 0);
    /* No bar when nothing is highlighted; a region clips. */
    t.title = NULL;
    t.highlighted = -1;
    t.x = 2;
    t.width = 20;
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_table_draw(&t, &tall);
    assert(tat(1, 0) == 0 && tat(2, 0) == 0x0001 && tat(22, 0) == 0);
    assert(tall_boxed(0x0002, 0, 0, SW, TH) == 0 && tall_boxed(0x0006, 2, 20, 20, 20));
    /* No cells with rows draws nothing rather than faulting. */
    t.cells = NULL;
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_table_draw(&t, &tall);
    assert(tat(2, 0) == 0);
    eapp_table_draw(NULL, &tall);
    eapp_table_draw(&t, NULL);
}

static void page(void)
{
    /* 32 px of width fit five glyphs: "AB" heads, then "CD EF" and "GH",
     * with a blank line between the blocks: four lines, and the 20 px
     * below the top inset show two. */
    static const struct eapp_page_block blocks[] = {{"AB", 1}, {"CD EF GH", 0}};
    struct eapp_page pg = {.blocks = blocks,
                           .count = 2,
                           .background = 0x0001,
                           .text = 0x0003,
                           .heading_text = 0x0004};
    struct eapp_input_event next = {EAPP_BUTTON_NEXT, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event previous = {EAPP_BUTTON_PREVIOUS, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event back = {EAPP_BUTTON_MENU, EAPP_INPUT_PRESS, 0};
    memset(screen_pixels, 0, sizeof screen_pixels);
    eapp_page_draw(&pg, &screen);
    assert(pg.lines == 4 && pg.shown == 2 && pg.offset == 0);
    assert(boxed(0x0004, 4, 4, 11, 7) && boxed(0x0004, 0, 0, SW, SH) > 10);
    assert(rows_are(0x0001, 11, 13)); /* The blank line, and no third line cut. */
    /* Two lines lie beyond the region; Next reaches them and no further,
     * which scrolls the heading and the blank line off and "CD EF" and
     * "GH" on. */
    assert(eapp_page_input(&pg, &next) == EAPP_MENU_NONE && pg.offset == 1);
    assert(eapp_page_input(&pg, &next) == EAPP_MENU_NONE && pg.offset == 2);
    assert(eapp_page_input(&pg, &next) == EAPP_MENU_NONE && pg.offset == 2);
    memset(screen_pixels, 0, sizeof screen_pixels);
    eapp_page_draw(&pg, &screen);
    assert(boxed(0x0004, 0, 0, SW, SH) == 0 && rows_are(0x0001, 0, 4));
    assert(boxed(0x0003, 4, 4, 29, 15) && rows_are(0x0001, 19, 5));
    assert(!rows_are(0x0001, 4, 7) && !rows_are(0x0001, 12, 7)); /* Both lines. */
    assert(eapp_page_input(&pg, &previous) == EAPP_MENU_NONE && pg.offset == 1);
    assert(eapp_page_input(&pg, &back) == EAPP_MENU_BACK);
    struct eapp_frame frame = wheel_frame(100, 4, 1);
    eapp_page_scroll(&pg, &frame);
    assert(pg.offset == 2);
    pg.offset = 9;
    eapp_page_draw(&pg, &screen);
    assert(pg.offset == 2); /* Clamped at the draw. */
    /* A taller line spacing shows fewer lines. */
    pg.line_height = 12;
    pg.offset = 9;
    eapp_page_draw(&pg, &screen);
    assert(pg.shown == 1 && pg.offset == 3);
    /* A region too short for a line shows nothing and holds the offset
     * at 0. */
    pg.height = 8;
    pg.offset = 0;
    memset(screen_pixels, 0, sizeof screen_pixels);
    eapp_page_draw(&pg, &screen);
    assert(pg.shown == 0 && rows_are(0x0001, 0, 8));
    assert(eapp_page_input(&pg, &next) == EAPP_MENU_NONE && pg.offset == 0);
    eapp_page_draw(NULL, &screen);
    eapp_page_scroll(NULL, &frame);
    assert(eapp_page_input(NULL, &next) == EAPP_MENU_NONE);
}

static void dialog(void)
{
    static const char *const options[] = {"Y", "N"};
    struct eapp_dialog d = {.message = "M",
                            .options = options,
                            .count = 2,
                            .background = 0x0001,
                            .selected_background = 0x0002,
                            .selected_text = 0x0003,
                            .text = 0x0004};
    struct eapp_input_event next = {EAPP_BUTTON_NEXT, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event previous = {EAPP_BUTTON_PREVIOUS, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event center = {EAPP_BUTTON_CENTER, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event back = {EAPP_BUTTON_MENU, EAPP_INPUT_PRESS, 0};
    struct eapp_input_event play = {EAPP_BUTTON_PLAY, EAPP_INPUT_PRESS, 0};
    assert(eapp_dialog_input(&d, &previous) == EAPP_DIALOG_NONE && d.selected == 0);
    assert(eapp_dialog_input(&d, &next) == EAPP_DIALOG_NONE && d.selected == 1);
    assert(eapp_dialog_input(&d, &next) == EAPP_DIALOG_NONE && d.selected == 1);
    assert(eapp_dialog_input(&d, &play) == EAPP_DIALOG_NONE && d.selected == 1);
    assert(eapp_dialog_input(&d, &center) == EAPP_DIALOG_CHOOSE);
    assert(eapp_dialog_input(&d, &back) == EAPP_DIALOG_CANCEL);
    assert(eapp_dialog_input(&d, &previous) == EAPP_DIALOG_NONE && d.selected == 0);
    /* On the 48 px framebuffer the options are 13 wide (5 px of text, 4
     * either side), 4 apart, so 30 together: from x 5, 12 high along the
     * bottom from y 32. The message starts at 4, 4. */
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_dialog_draw(&d, &tall);
    assert(tat(4, 4) == 0x0004 && tat(4, 11) == 0x0001);
    assert(tall_boxed(0x0002, 5, 32, 13, 12) + tall_boxed(0x0003, 9, 34, 5, 7) == 13 * 12);
    assert(tat(5, 32) == 0x0002 && tat(17, 43) == 0x0002 && tat(18, 32) == 0x0001);
    assert(tat(22, 32) == 0x0001 && tat(26, 34) == 0x0004 && tat(4, 31) == 0x0001);
    /* A sixth of a turn moves one option. */
    struct eapp_frame frame = wheel_frame(100, 16, 1);
    eapp_dialog_scroll(&d, &frame);
    assert(d.selected == 1);
    /* A long message wraps to 32 px, five glyphs a line, and stops above
     * the options' row and the gap over it: three lines, not four. */
    d.message = "M M M M M M M M M M";
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_dialog_draw(&d, &tall);
    assert(tat(4, 4) == 0x0004 && tat(4, 12) == 0x0004 && tat(4, 20) == 0x0004);
    assert(rows_are_tall(0x0001, 27, 5));
    d.message = "M";
    /* Options wider than the region share it, 14 px each from x 4, and
     * their text is cut at their edges. */
    static const char *const wide[] = {"AAAAAA", "BBBBBB"};
    d.options = wide;
    d.message = NULL;
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_dialog_draw(&d, &tall);
    assert(tat(22, 32) == 0x0002 && tat(35, 43) == 0x0002 && tat(4, 32) == 0x0001);
    assert(tat(18, 32) == 0x0001 && tat(36, 32) == 0x0001);
    assert(tall_boxed(0x0004, 4, 32, 14, 12) && tall_boxed(0x0003, 22, 32, 14, 12));
    d.message = "M";
    /* More options than the region holds draw none. */
    static const char *const many[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    d.options = many;
    d.count = 10;
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_dialog_draw(&d, &tall);
    assert(tall_boxed(0x0002, 0, 0, SW, TH) == 0 && rows_are_tall(0x0001, 11, TH - 11));
    d.options = options;
    d.count = 2;
    /* A selection past the end answers nothing to Center. */
    d.selected = 5;
    assert(eapp_dialog_input(&d, &center) == EAPP_DIALOG_NONE);
    assert(eapp_dialog_input(&d, &next) == EAPP_DIALOG_NONE && d.selected == 1);
    /* Without options the title and message have the whole region. */
    d.title = "T";
    d.options = NULL;
    memset(tall_pixels, 0, sizeof tall_pixels);
    eapp_dialog_draw(&d, &tall);
    assert(tat(4, 4) == 0xffff && tat(4, 22) == 0x0004 && tall_boxed(0x0002, 0, 0, SW, TH) == 0);
    eapp_dialog_draw(NULL, &tall);
    eapp_dialog_scroll(NULL, &frame);
    assert(eapp_dialog_input(NULL, &next) == EAPP_DIALOG_NONE);
}

/* One picture of the package for eapp_bitmap_load: 2 x 2, the bottom right
 * pixel the key, and a file of another format. */
static const uint8_t picture_file[20 + 8] = {
    'E', 'I', 'M', 'G', 2, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0x1f, 0xf8, 0, 0,
    0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00, 0x1f, 0xf8};
static const uint8_t alpha_file[20 + 2] = {'E', 'I', 'M', 'G', 1, 0, 0, 0, 1, 0, 0,
                                           0, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0};
/* A pack of the two: the table, the 2 x 2 picture at 16, padded to 4 bytes
 * (28 bytes, no padding), the alpha picture at 44. */
static const uint8_t pack_data[16 + 28 + 24] = {
    'E', 'P', 'A', 'K', 2, 0, 0, 0, 16, 0, 0, 0, 44, 0, 0, 0,
    'E', 'I', 'M', 'G', 2, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0x1f, 0xf8, 0, 0,
    0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00, 0x1f, 0xf8,
    'E', 'I', 'M', 'G', 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0};

int eapp_file_read_all(const char *path, void *buffer, size_t capacity)
{
    const uint8_t *file = !strcmp(path, "art/tiles.eimg")   ? picture_file
                          : !strcmp(path, "art/glow.eimg")  ? alpha_file
                          : !strcmp(path, "art/short.eimg") ? picture_file
                          : !strcmp(path, "scene.epak")     ? pack_data
                          : !strcmp(path, "cut.epak")       ? pack_data
                                                            : NULL;
    size_t size = file == picture_file ? sizeof picture_file
                  : file == alpha_file ? sizeof alpha_file
                  : file == pack_data  ? sizeof pack_data
                                       : 0;
    if (!strcmp(path, "art/short.eimg"))
        size = 24;
    if (!strcmp(path, "cut.epak"))
        size = 50; /* Ends inside the second entry. */
    if (!file)
        return -1;
    memcpy(buffer, file, size < capacity ? size : capacity);
    return (int)size;
}

static unsigned blocks;

void *eapp_alloc(size_t size)
{
    ++blocks;
    return malloc(size);
}

void eapp_free(void *pointer)
{
    if (pointer)
        --blocks;
    free(pointer);
}

static void pictures(void)
{
    struct eapp_bitmap *tiles = eapp_bitmap_load("art/tiles.eimg");
    assert(tiles && tiles->width == 2 && tiles->height == 2 && tiles->stride_pixels == 2);
    assert(tiles->transparent == 0xf81f && tiles->pixels[0] == 0xf800 &&
           tiles->pixels[1] == 0x07e0 && tiles->pixels[2] == 0x001f &&
           tiles->pixels[3] == 0xf81f && blocks == 1);
    clear();
    eapp_draw_bitmap(&fb, 1, 1, tiles);
    assert(at(1, 1) == 0xf800 && at(2, 1) == 0x07e0 && at(1, 2) == 0x001f);
    assert(at(2, 2) == 0 && count() == 0); /* The key pixel is skipped. */
    eapp_bitmap_free(tiles);
    assert(blocks == 0);
    assert(!eapp_bitmap_load("art/glow.eimg") && !eapp_bitmap_load("art/short.eimg"));
    assert(!eapp_bitmap_load("art/none.eimg") && blocks == 0);
    eapp_bitmap_free(NULL);

    /* A pack: one block, a bitmap per RGB565 entry, none for texture data. */
    struct eapp_pack *pack = eapp_pack_load("scene.epak");
    assert(pack && eapp_pack_count(pack) == 2 && blocks == 1);
    const struct eapp_bitmap *first = eapp_pack_bitmap(pack, 0);
    assert(first && first->width == 2 && first->height == 2 && first->transparent == 0xf81f);
    assert(first->pixels[0] == 0xf800 && first->pixels[3] == 0xf81f);
    assert(!eapp_pack_bitmap(pack, 1) && !eapp_pack_bitmap(pack, 2) && !eapp_pack_bitmap(NULL, 0));
    clear();
    eapp_draw_bitmap(&fb, 0, 0, first);
    assert(at(0, 0) == 0xf800 && at(1, 1) == 0);
    eapp_pack_free(pack);
    assert(blocks == 0 && eapp_pack_count(NULL) == 0);
    assert(!eapp_pack_load("cut.epak") && !eapp_pack_load("art/tiles.eimg") && blocks == 0);
    assert(!eapp_pack_load("none.epak"));
    eapp_pack_free(NULL);
}

/* An off-screen framebuffer: black with the width as stride, pixels read
 * back, and copies onto the test framebuffer cut on both sides. */
static void off_screen(void)
{
    struct eapp_framebuffer *canvas = eapp_framebuffer_create(5, 3);
    assert(canvas && canvas->width == 5 && canvas->height == 3 && canvas->stride_pixels == 5);
    assert(blocks == 1);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            assert(eapp_read_pixel(canvas, x, y) == 0);
    eapp_draw_pixel(canvas, 4, 2, INK);
    eapp_fill_rect(canvas, 0, 0, 2, 2, 7);
    assert(eapp_read_pixel(canvas, 4, 2) == INK && eapp_read_pixel(canvas, 1, 1) == 7);
    assert(eapp_read_pixel(canvas, 5, 0) == -1 && eapp_read_pixel(canvas, 0, 3) == -1);
    assert(eapp_read_pixel(canvas, -1, 0) == -1 && eapp_read_pixel(canvas, 0, -1) == -1);
    assert(eapp_read_pixel(NULL, 0, 0) == -1);

    clear();
    eapp_copy_rect(&fb, 1, 1, canvas, 0, 0, 5, 3);
    assert(at(1, 1) == 7 && at(2, 2) == 7 && at(3, 1) == 0 && at(5, 3) == INK && at(6, 3) == BG);
    assert(count() == 1);

    clear();
    eapp_fill_rect(&fb, 0, 0, W, H, 9);
    eapp_copy_rect(&fb, -1, -1, canvas, 0, 0, 10, 10);
    assert(at(0, 0) == 7 && at(1, 0) == 0 && at(3, 1) == INK && at(4, 1) == 9 && at(0, 2) == 9);

    clear();
    eapp_copy_rect(&fb, 13, 10, canvas, 1, 1, 10, 10);
    assert(at(13, 10) == 7 && at(14, 10) == 0 && at(15, 11) == BG && count() == 0);
    eapp_copy_rect(&fb, 14, 10, canvas, 0, 0, 5, 3);
    assert(at(14, 10) == 7 && at(15, 11) == 7 && count() == 0); /* INK lands off the edge. */
    padding_clean();

    /* Wholly off the right or bottom of either side, nothing is copied. */
    clear();
    eapp_copy_rect(&fb, 0, 0, canvas, 5, 0, 2, 2);
    eapp_copy_rect(&fb, 0, 0, canvas, 0, 3, 2, 2);
    eapp_copy_rect(&fb, W + 4, 0, canvas, 0, 0, 5, 3);
    eapp_copy_rect(&fb, 0, H, canvas, 0, 0, 5, 3);
    eapp_copy_rect(&fb, 0, 0, NULL, 0, 0, 2, 2);
    eapp_copy_rect(NULL, 0, 0, canvas, 0, 0, 2, 2);
    assert(count() == 0);

    /* A negative source corner, and a copy between two heap framebuffers. */
    struct eapp_framebuffer *other = eapp_framebuffer_create(4, 4);
    assert(other && blocks == 2);
    eapp_copy_rect(other, 0, 0, canvas, -1, -1, 3, 3);
    assert(eapp_read_pixel(other, 0, 0) == 0 && eapp_read_pixel(other, 1, 1) == 7 &&
           eapp_read_pixel(other, 2, 2) == 7 && eapp_read_pixel(other, 3, 3) == 0);
    struct eapp_framebuffer blank = {NULL, 4, 4, 4};
    assert(eapp_read_pixel(&blank, 0, 0) == -1);
    eapp_framebuffer_free(other);

    eapp_framebuffer_free(canvas);
    eapp_framebuffer_free(NULL);
    assert(blocks == 0);
    assert(!eapp_framebuffer_create(0, 4) && !eapp_framebuffer_create(4, 0) && blocks == 0);
    assert(!eapp_framebuffer_create(EAPP_FRAMEBUFFER_MAX_SIDE + 1, 4) && blocks == 0);
    assert(!eapp_framebuffer_create(4, 65536) && blocks == 0);
}

/* A stand-in for the package listing: the walk of "roms", names relative to
 * it and unsorted, with a folder, a hidden file and a file of another
 * extension among the ROMs. */
static const struct eapp_directory_entry listing[] = {
    {"Zelda.NES", 0},   {"sub", 1},        {"sub/alpha.nes", 0},
    {".hidden.nes", 0}, {"readme.txt", 0}, {"Mario.nes", 0},
};
static unsigned listed, listing_open;

struct eapp_directory *eapp_directory_open(const char *path, int recursive)
{
    assert(!strcmp(path, "roms") && recursive);
    listed = 0;
    listing_open = 1;
    return (struct eapp_directory *)&listing_open;
}

int eapp_directory_read(struct eapp_directory *directory, struct eapp_directory_entry *entry)
{
    assert(directory == (struct eapp_directory *)&listing_open);
    if (listed == sizeof listing / sizeof listing[0])
        return 0;
    *entry = listing[listed++];
    return 1;
}

void eapp_directory_close(struct eapp_directory *directory)
{
    assert(directory == (struct eapp_directory *)&listing_open);
    listing_open = 0;
}

/* Rows sorted by name, labels without the extension, the paths apart from the
 * rows so the menu draws no value column. */
static void picker(void)
{
    static struct eapp_file_picker p;
    assert(eapp_file_picker_open(&p, "ROMS", "roms", ".nes") == 0 && !listing_open);
    assert(p.menu.count == 3 && p.menu.items == p.items && !strcmp(p.menu.title, "ROMS"));
    assert(!strcmp(p.items[0].label, "alpha") && !strcmp(p.paths[0], "roms/sub/alpha.nes"));
    assert(!strcmp(p.items[1].label, "Mario") && !strcmp(p.paths[1], "roms/Mario.nes"));
    assert(!strcmp(p.items[2].label, "Zelda") && !strcmp(p.paths[2], "roms/Zelda.NES"));
    for (unsigned i = 0; i < 3; ++i)
        assert(!p.items[i].value);
    unsigned held = blocks;
    eapp_file_picker_close(&p);
    assert(p.menu.count == 0 && blocks == held - 3);
}

int main(void)
{
    rects();
    lines();
    triangles();
    blends();
    circles();
    round_rects();
    bitmaps();
    text();
    random_stream();
    wheel_steps();
    menu();
    picker();
    message();
    table();
    page();
    dialog();
    pictures();
    off_screen();
    puts("draw_test passed");
    return 0;
}
