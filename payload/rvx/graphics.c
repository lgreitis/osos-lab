/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "osos.h"
#include "../../rvx-sdk/osos-2.0.5.h"

static void *gc;
static int left, top;
static struct rect area;
static u32 fonts[3][2];
static uint8_t fonts_built;
static Pkg *color_owner;
static const u32 *roles;
/* The stock list colours until an extension sets the colour roles: the
 * highlight is the middle of Apple's gradient, the bar colours are the ones its
 * selection bar is rendered from. */
static const u32 palette[11] = {0xffffffff,
                                0xff000000,
                                0xff0084dc,
                                0xffffffff,
                                0xff6b6d6b,
                                0xff424542,
                                0xff3185d0,
                                0xff000000 | SelectionBarColor_TopLine,
                                0xff000000 | SelectionBarColor_GradientStart,
                                0xff000000 | SelectionBarColor_GradientEnd,
                                0xff000000 | SelectionBarColor_BottomLine};

void rvx_begin_draw(void *context, const int32_t *bounds)
{
    gc = context;
    left = bounds[1];
    top = bounds[0];
    area = (struct rect){0, 0, bounds[3] - left, bounds[2] - top};
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
    return (roles ? roles : palette)[role < 11 ? role : 1];
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
    /* Copy for an opaque colour, Blend below 0xff alpha. */
    osos_graphics_set_mode(gc, (c >> 24) == 0xff ? GraphicsDrawMode_Copy
                                                 : GraphicsDrawMode_Blend);
    osos_graphics_set_color(gc, bytes);
    osos_graphics_fill_rect_unmapped(gc, bounds);
}

/* Helvetica in the firmware's size units: RV_FONT_TITLE is the title bar's
 * RES_Font_BoldSmall, RV_FONT_TEXT the Legal page's RES_Font_RegularSmall,
 * RV_FONT_LARGE the preview caption's RES_Font_Bold. */
static void font(unsigned index)
{
    static const uint8_t sizes[3] = {0x3a, 0x3a, 0x4d}, bold[3] = {1, 0, 1};
    if (index > 2)
        index = 1;
    if (!fonts_built) {
        for (unsigned i = 0; i < 3; i++)
            osos_font_construct(fonts[i], "Helvetica", sizes[i], bold[i]);
        fonts_built = 1;
    }
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
    uint8_t fg[4];
    rgba(c, fg);
    font(i);
    osos_graphics_set_pen(gc, left + x, top + y + osos_graphics_font_ascent(gc));
    osos_graphics_set_color(gc, fg);
    osos_graphics_set_mode(gc, GraphicsDrawMode_Text);
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

/* The selection bar as View_DrawBackgroundAndBorder renders it: a line, the
 * gradient one row at a time, a line; under three rows the gradient alone. */
static void bar(const struct rect *r)
{
    if (!r || r->h <= 0)
        return;
    u32 start = color(8), end = color(9);
    int lines = r->h >= 3, n = r->h - 2 * lines;
    unsigned span = n > 1 ? (unsigned)n - 1 : 1; /* a lone row is `start` */
    struct rect line = {r->x, r->y, r->w, 1};
    if (lines) {
        fill(&line, color(7));
        line.y++;
    }
    for (unsigned i = 0; i < (unsigned)n; i++, line.y++) {
        u32 c = 0xff000000;
        for (int shift = 0; shift < 24; shift += 8) {
            unsigned a = start >> shift & 0xff, b = end >> shift & 0xff;
            c |= (u32)((a * (span - i) + b * i) / span) << shift;
        }
        fill(&line, c);
    }
    if (lines)
        fill(&line, color(10));
}

enum { ROW_HEIGHT = 25, ROW_TEXT_LEFT = 8, PARAGRAPH_SPACING = 3 };

static void row(int y, int width, const char *label, const char *value, int selected)
{
    u32 ink = color(1), detail = color(4);
    if (selected) {
        bar(&(struct rect){0, y, width, ROW_HEIGHT});
        ink = detail = color(3);
    }
    int baseline = y + (ROW_HEIGHT - height(0)) / 2;
    if (label)
        text(ROW_TEXT_LEFT, baseline, 0, ink, label);
    if (value && *value)
        text(width - ROW_TEXT_LEFT - measure(0, value), baseline, 0, detail, value);
}

/* Words break on spaces only; a word wider than the line runs past it. */
static int paragraph(int x, int y, int w, uint8_t i, u32 c, const char *s, u32 lines)
{
    char line[96];
    unsigned used = 0;
    int step = height(i) + PARAGRAPH_SPACING;
    if (!s)
        return y;
    while (*s && lines) {
        const char *word = s;
        while (*s && *s != ' ')
            s++;
        unsigned keep = used;
        if (used && used < sizeof line - 1)
            line[used++] = ' ';
        for (const char *p = word; p < s && used < sizeof line - 1; p++)
            line[used++] = *p;
        line[used] = 0;
        if (keep && measure(i, line) > w) {
            line[keep] = 0;
            text(x, y, i, c, line);
            y += step;
            lines--;
            used = 0;
            for (const char *p = word; p < s && used < sizeof line - 1; p++)
                line[used++] = *p;
            line[used] = 0;
        }
        while (*s == ' ')
            s++;
    }
    if (used && lines) {
        text(x, y, i, c, line);
        y += step;
    }
    return y;
}

/* A BMap whose bounds, rows and palette fit its payload. */
static int whole(const uint8_t *b)
{
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    u32 stride = *(const uint16_t *)(b + BMap_stride);
    u32 depth = *(const uint16_t *)(b + BMap_depth);
    u32 payload = *(const u32 *)(b + BMap_payload_bytes);
    u32 h = (u32)(bounds[2] - bounds[0]), w = (u32)(bounds[3] - bounds[1]);
    u32 palette_bytes = 0;
    if (*(const uint16_t *)(b + BMap_format) == BMapFormat_Indexed8) {
        u32 colors = payload >= 4 ? *(const u32 *)(b + BMap_payload) : 0x10000;
        palette_bytes = colors <= 256 ? 4 + 4 * colors : payload + 1;
    }
    return bounds[2] >= bounds[0] && bounds[3] >= bounds[1] && h <= 0xffff &&
           w <= 0xffff && depth <= 32 && w * depth <= stride * 8 &&
           palette_bytes <= payload && h * stride <= payload - palette_bytes;
}

static int is_mask(const uint8_t *b)
{
    uint16_t format = *(const uint16_t *)(b + BMap_format);
    return format == BMapFormat_Mask8 || format == BMapFormat_Mask4;
}

typedef void *(*bitmap_fn)(void *);
typedef void *(*source_fn)(void *, void *, const void *);
typedef void (*copy_fn)(void *, void *, const int32_t *, const int32_t *, u32, u32);

/* Copies the columns [0, columns) of the top `n` rows (all when 0) of a BMap
 * to (x, y) through a source context on it, at `alpha`/255 of its opacity; a
 * mask draws in the context's colour when the caller has set text mode. */
static void copy(int x, int y, const uint8_t *b, int n, int columns, u32 alpha)
{
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    int h = bounds[2] - bounds[0], w = bounds[3] - bounds[1];
    if (!n || n > h)
        n = h;
    if (n <= 0 || w <= 0)
        return;
    u32 bitmap[Bitmap_bytes / 4], src[GraphicsContext_bytes / 4];
    ((bitmap_fn)ADDR_Bitmap_Construct)(bitmap);
    ((source_fn)ADDR_GraphicsContext_InitFromBitmapResource)(src, bitmap, b);
    for (int at = 0; at < columns; at += w) {
        int part = columns - at < w ? columns - at : w;
        int32_t from[] = {bounds[0], bounds[1], bounds[0] + n, bounds[1] + part},
                to[] = {top + y, left + x + at, top + y + n, left + x + at + part};
        ((copy_fn)ADDR_GraphicsContext_CopyRect)(gc, src, from, to, alpha, 0);
    }
    ((bitmap_fn)ADDR_GraphicsContext_Destroy)(src);
    ((bitmap_fn)ADDR_Bitmap_Destroy)(bitmap);
}

static int width_of(const uint8_t *b)
{
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    return bounds[3] - bounds[1];
}

static void bmap(int x, int y, const void *b)
{
    if (b && whole(b)) {
        copy(x, y, b, 0, width_of(b), 255);
    }
}

/* The top `n` rows; a coverage mask draws in `tint` in text mode, as a bitmap
 * view draws it in its tint. */
static void rows(int x, int y, const void *b, int n, u32 tint)
{
    if (!b || !whole(b) || n <= 0)
        return;
    if (is_mask(b)) {
        uint8_t bytes[4];
        rgba(tint, bytes);
        osos_graphics_set_color(gc, bytes);
        osos_graphics_set_mode(gc, GraphicsDrawMode_Text);
    }
    copy(x, y, b, n, width_of(b), 255);
}

/* The top `n` rows at `a`; a mask draws nothing, since text mode takes no
 * opacity. */
static void blend(int x, int y, const void *b, int n, uint8_t a)
{
    if (!b || !whole(b) || n <= 0 || is_mask(b))
        return;
    copy(x, y, b, n, width_of(b), a);
}

static void tile(int x, int y, int w, const void *b)
{
    if (!b || !whole(b) || width_of(b) <= 0)
        return;
    copy(x, y, b, 0, w, 255);
}

struct cached_bitmap {
    struct cached_bitmap *next;
    u32 id;
    void *data;
    u32 size;
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

const void *rvx_frame_bitmap(struct frame *f, const struct bitmap *bitmap, u32 *size)
{
    if (!bitmap || !bitmap->name || !f)
        return NULL;
    u32 id = rvx_hash(bitmap->name);
    for (struct cached_bitmap *b = f->bitmaps; b; b = b->next)
        if (b->id == id) {
            if (size)
                *size = b->size;
            return b->data;
        }
    u32 bytes;
    void *data = rvx_bitmap_load(f->ctx.self, bitmap->name, &bytes);
    if (!data)
        return NULL;
    /* A BMap whose rows fit the block it was loaded into. */
    int fits = bytes >= BMap_payload && whole(data) &&
               *(const u32 *)((const uint8_t *)data + BMap_payload_bytes) <=
                   bytes - BMap_payload;
    struct cached_bitmap *b = fits ? osos_import_alloc(sizeof *b) : NULL;
    if (!b) {
        rvx_free(f->ctx.self, data);
        return NULL;
    }
    b->size = bytes;
    if (size)
        *size = bytes;
    b->id = id;
    b->data = data;
    b->next = f->bitmaps;
    f->bitmaps = b;
    return data;
}

static void bitmap(int x, int y, const struct bitmap *bitmap)
{
    bmap(x, y, rvx_frame_bitmap(rvx_top, bitmap, NULL));
}

/* The colours and the band come from Settings > About; the icon sits on the box
 * the Settings pane draws behind the stock icons, both without reflections. */
enum { CARD_LEFT = 18, CARD_BAND = 52, TITLE = 0 };

static void card(int width, int h, const char *left_text, const char *right_text,
                 const struct bitmap *icon)
{
    static const uint8_t from[3] = {0x84, 0x96, 0xa5}, to[3] = {0x42, 0x4d, 0x52};
    for (int y = 0; y < h; y += 4) {
        int t = y * 256 / h;
        u32 c = 0xff000000;
        for (int i = 0; i < 3; i++)
            c |= (u32)((from[i] * (256 - t) + to[i] * t) >> 8) << (16 - 8 * i);
        fill(&(struct rect){0, y, width, 4}, c);
    }
    if (icon) {
        const void *box =
            (const void *)osos_ui_bitmap(RES_SettingsPreview_Background_Image);
        u32 bytes = 0;
        const void *glyph = rvx_frame_bitmap(rvx_top, icon, &bytes);
        rows((width - 78) / 2, CARD_BAND - 39, box, 79, 0);
        if (rvx_is_pane_icon(glyph, bytes))
            rows((width - 78) / 2, CARD_BAND - 39, glyph, 79, 0xffffffff);
    }
    int y = CARD_BAND - height(TITLE) / 2;
    if (left_text)
        text(CARD_LEFT, y, TITLE, 0xffffffff, left_text);
    if (right_text)
        text(width - CARD_LEFT - measure(TITLE, right_text), y, TITLE,
             0xffffffff, right_text);
}

/* The context's clip is the region the firmware repaints, in surface
 * coordinates: the view's, moved by the origin. Back in canvas coordinates
 * and cut to the canvas; empty when the two do not meet. */
static const struct rect *clip(void)
{
    static struct rect part;
    const int32_t *c = (const int32_t *)gc + 0x34 / 4;
    int dx = ((const int32_t *)gc)[0x2c / 4] + left,
        dy = ((const int32_t *)gc)[0x30 / 4] + top;
    int y0 = c[0] - dy, x0 = c[1] - dx, y1 = c[2] - dy, x1 = c[3] - dx;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > area.w)
        x1 = area.w;
    if (y1 > area.h)
        y1 = area.h;
    part = (struct rect){(int16_t)x0, (int16_t)y0, (int16_t)(x1 > x0 ? x1 - x0 : 0),
                         (int16_t)(y1 > y0 ? y1 - y0 : 0)};
    return &part;
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
