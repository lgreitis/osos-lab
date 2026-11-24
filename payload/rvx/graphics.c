/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "osos.h"
#include "dark_mode.h"

static void *gc;
static int left, top;
static struct rect area;
static u32 fonts[3][2];
static Pkg *color_owner;
static const u32 *roles;
static const u32 palettes[][11] = {
    {0xffffffff, 0xff202020, 0xff2688db, 0xffffffff, 0xff555555, 0xff202020, 0xff1479d5,
     0xff5da9eb, 0xff53a6ee, 0xff2278cc, 0xff196bb8},
    {0xff181818, 0xffe8e8e8, 0xff157fc4, 0xffffffff, 0xffa8a8a8, 0xffe8e8e8, 0xff30abff,
     0xff318ac0, 0xff257dab, 0xff145e90, 0xff104c73}};

void rvx_begin_draw(void *context, const int32_t *bounds, const struct rect *dirty)
{
    gc = context;
    left = bounds[1];
    top = bounds[0];
    area = (struct rect){0, 0, bounds[3] - left, bounds[2] - top};
    if (dirty) {
        int32_t *clip = (int32_t *)context + 0x34 / 4;
        int x = dirty->x + left + ((int32_t *)context)[0x2c / 4];
        int y = dirty->y + top + ((int32_t *)context)[0x30 / 4];
        int r = x + dirty->w, b = y + dirty->h;
        if (clip[0] < y)
            clip[0] = y;
        if (clip[1] < x)
            clip[1] = x;
        if (clip[2] > b)
            clip[2] = b;
        if (clip[3] > r)
            clip[3] = r;
        area.x = dirty->x;
        area.y = dirty->y;
        area.w = dirty->w;
        area.h = dirty->h;
    }
}

int rvx_colors(Pkg *p, const u32 *r)
{
    if (color_owner && color_owner != p)
        return ALREADY_BOUND;
    color_owner = r ? p : NULL;
    roles = r;
    osos_ui_invalidate();
    return OK;
}

static u32 color(uint8_t role)
{
    return (roles ? roles : palettes[!!cfw_dark_mode_enabled()])[role < 11 ? role : 0];
}

static void rgba(u32 c, uint8_t out[4])
{
    out[0] = c >> 16;
    out[1] = c >> 8;
    out[2] = c;
    out[3] = c >> 24;
}

static void fill(const struct rect *r, u32 c)
{
    if (!r || r->w <= 0 || r->h <= 0 || !(c >> 24))
        return;
    int32_t bounds[] = {top + r->y, left + r->x, top + r->y + r->h, left + r->x + r->w};
    uint8_t bytes[4];
    rgba(c, bytes);
    osos_graphics_set_mode(gc, 0);
    osos_graphics_set_color(gc, bytes);
    osos_graphics_fill_rect_unmapped(gc, bounds);
}

static void font(unsigned index)
{
    static const u32 ids[] = {0x0dad0000, 0x0dad0020, 0x0dad0000};
    if (index > 2)
        index = 1;
    if (!(fonts[index][0] & ~1u)) {
        u32 n;
        const u32 *f = osos_resource_get(rvx_bank, 0x464f4e54, ids[index], &n);
        if (f && n >= 12)
            osos_font_construct(fonts[index], (const char *)osos_ui_string(f[0]),
                                f[1] & 0xffff, f[2] & 255);
    }
    if (fonts[index][0] & ~1u)
        osos_graphics_set_font(gc, fonts[index]);
}

static int measure(uint8_t i, const char *s)
{
    font(i);
    return s ? osos_graphics_text_width(gc, s) : 0;
}

static int height(uint8_t i)
{
    font(i);
    return osos_graphics_font_ascent(gc) + osos_graphics_font_descent(gc);
}

static void text(int x, int y, uint8_t i, u32 c, const char *s)
{
    if (!s)
        return;
    uint8_t fg[4], bg[4];
    rgba(c, fg);
    rgba(color(0), bg);
    font(i);
    osos_graphics_set_pen(gc, left + x, top + y + osos_graphics_font_ascent(gc));
    osos_graphics_set_background_color(gc, bg);
    osos_graphics_set_color(gc, fg);
    osos_graphics_set_mode(gc, 0x21);
    osos_graphics_draw_text(gc, s);
}

static void text_in(const struct rect *r, uint8_t i, u32 c, const char *s, int8_t align)
{
    if (!s || !r)
        return;
    int slack = r->w - measure(i, s);
    text(r->x + (align < 0   ? 0
                 : align > 0 ? slack
                             : slack / 2),
         r->y + (r->h - height(i)) / 2, i, c, s);
}

static u32 mix(u32 a, u32 b, uint8_t w)
{
    u32 result = 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned c =
            (((a >> shift) & 255) * (255 - w) + ((b >> shift) & 255) * w + 127) / 255;
        result |= c << shift;
    }
    return result;
}

static void bar(const struct rect *r)
{
    for (int j = 0; j < r->h; j++) {
        u32 c = j == 0          ? color(7)
                : j + 1 == r->h ? color(10)
                                : mix(color(8), color(9), 255 * j / (r->h - 1));
        fill(&(struct rect){r->x, r->y + j, r->w, 1}, c);
    }
}

static void row(int y, int width, const char *label, const char *value, int selected)
{
    if (selected)
        bar(&(struct rect){0, y, width, 25});
    struct rect r = {9, y, width - 18, 25};
    text_in(&r, 1, color(selected ? 3 : 1), label, -1);
    text_in(&r, 1, color(selected ? 3 : 4), value, 1);
}

static int paragraph(int x, int y, int w, uint8_t i, u32 c, const char *s, u32 lines)
{
    int dy = height(i);
    while (s && *s && lines--) {
        char buf[256];
        unsigned n = 0, space = 0;
        while (s[n] && s[n] != '\n' && n + 1 < sizeof buf) {
            if (s[n] == ' ')
                space = n;
            buf[n] = s[n];
            n++;
            buf[n] = 0;
            if (measure(i, buf) > w && n > 1) {
                n = space ? space : n - 1;
                break;
            }
        }
        buf[n] = 0;
        text(x, y, i, c, buf);
        y += dy;
        s += n;
        if (*s == '\n')
            s++;
        else
            while (*s == ' ')
                s++;
    }
    return y;
}

static void blit(int x, int y, const void *image, int rows, int columns, unsigned alpha,
                 u32 tint)
{
    if (!image || rows <= 0 || columns <= 0)
        return;
    const u32 *b = image;
    int h = (int)b[4] - (int)b[2], w = (int)b[5] - (int)b[3];
    if (w <= 0 || h <= 0)
        return;
    if (rows < h)
        h = rows;
    if (columns < w)
        w = columns;
    if ((b[1] >> 16) == 8 && (b[0] & 0xffff) == 8) {
        const uint8_t *pixels = (const uint8_t *)image + 28;
        unsigned stride = b[1] & 0xffff;
        if (stride < (unsigned)w || !stride || (unsigned)h > b[6] / stride)
            return;
        for (int r = 0; r < h; r++)
            for (int col = 0; col < w;) {
                unsigned coverage = pixels[r * stride + col];
                int j = col + 1;
                while (j < w && pixels[r * stride + j] == coverage)
                    j++;
                if (coverage)
                    fill(&(struct rect){x + col, y + r, j - col, 1},
                         mix(color(0), tint, coverage));
                col = j;
            }
        return;
    }
    u32 bitmap[0xb8 / 4], src[0x44 / 4];
    ((void *(*)(void *))0x08103e8c)(bitmap);
    ((void *(*)(void *, void *, const void *))0x0825af2c)(src, bitmap, image);
    int32_t from[] = {0, 0, h, w},
            to[] = {y + top, x + left, y + top + h, x + left + w};
    ((void (*)(void *, void *, const int32_t *, const int32_t *, u32, u32))0x08259568)(
        gc, src, from, to, alpha, 0);
    ((void (*)(void *))0x0825b038)(src);
    ((void *(*)(void *))0x08103ee0)(bitmap);
}

static void bmap(int x, int y, const void *b)
{
    blit(x, y, b, 32767, 32767, 255, color(1));
}

static void rows(int x, int y, const void *b, int n, u32 tint)
{
    blit(x, y, b, n, 32767, 255, tint);
}

static void blend(int x, int y, const void *b, int n, uint8_t a)
{
    blit(x, y, b, n, 32767, a, color(1));
}

static void tile(int x, int y, int w, const void *b)
{
    if (!b)
        return;
    int width = ((const int32_t *)b)[5] - ((const int32_t *)b)[3];
    if (width > 0)
        for (int dx = 0; dx < w; dx += width)
            blit(x + dx, y, b, 32767, w - dx, 255, color(1));
}

struct cached_bitmap {
    struct cached_bitmap *next;
    u32 id;
    void *data;
};

void rvx_clear_bitmaps(struct frame *f)
{
    while (f->bitmaps) {
        struct cached_bitmap *b = f->bitmaps;
        f->bitmaps = b->next;
        rvx_free(f->ctx.self, b->data);
        osos_import_free(b);
    }
}

static void bitmap(int x, int y, const struct bitmap *bitmap)
{
    if (!bitmap || !bitmap->name || !rvx_top)
        return;
    u32 id = rvx_hash(bitmap->name);
    for (struct cached_bitmap *b = rvx_top->bitmaps; b; b = b->next)
        if (b->id == id) {
            bmap(x, y, b->data);
            return;
        }
    Pkg *p = rvx_top->ctx.self;
    u32 n;
    void *data = rvx_bitmap_load(p, bitmap->name, &n);
    if (!data)
        return;
    if (n >= 28) {
        bmap(x, y, data);
        struct cached_bitmap *b = osos_import_alloc(sizeof *b);
        if (b) {
            b->id = id;
            b->data = data;
            b->next = rvx_top->bitmaps;
            rvx_top->bitmaps = b;
            return;
        }
    }
    rvx_free(p, data);
}

static void card(int w, int h, const char *l, const char *r, const struct bitmap *b)
{
    fill(&(struct rect){0, 0, w, h}, color(0));
    bitmap((w - 78) / 2, (h - 121) / 2, b);
    text_in(&(struct rect){6, 0, w / 3 - 8, h}, 1, color(1), l, -1);
    text_in(&(struct rect){w * 2 / 3, 0, w / 3 - 8, h}, 1, color(1), r, 1);
}

static const struct rect *clip(void)
{
    return &area;
}
const struct graphics rvx_graphics = {sizeof(struct graphics),
                                      color,
                                      fill,
                                      text,
                                      measure,
                                      height,
                                      bitmap,
                                      bmap,
                                      paragraph,
                                      row,
                                      rows,
                                      blend,
                                      tile,
                                      card,
                                      text_in,
                                      mix,
                                      clip,
                                      bar};

void rvx_forget_colors(Pkg *p, const void *start, u32 bytes)
{
    if (p == color_owner && (uintptr_t)roles - (uintptr_t)start < bytes) {
        roles = NULL;
        color_owner = NULL;
    }
}
