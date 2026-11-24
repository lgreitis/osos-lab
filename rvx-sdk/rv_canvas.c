/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS payload/eq_canvas.c (d640115, 93f6a45) by lgreitis. */

#include "rv_canvas.h"

/* A legend swatch is as tall as the labels' capitals and sits on their
 * baseline: the text font's caps are 9 px, 6 px under the header line's top. */
enum {
    HEADER_TOP = 3,
    HEADER_HEIGHT = 24,
    SWATCH = 9,
    SWATCH_TOP = HEADER_TOP + 6,
    SCALE_WIDTH = 38
};

static const uint8_t roles[] = {RV_COLOR_BACKGROUND, RV_COLOR_ACCENT_TEXT,
                                RV_COLOR_TEXT, RV_COLOR_HIGHLIGHT_TEXT};

/* Greys are the background mixed toward the text by weight/255. */
static const uint8_t greys[][2] = {
    {RV_CANVAS_TRACK, 22}, {RV_CANVAS_GRID, 28},      {RV_CANVAS_BORDER, 46},
    {RV_CANVAS_ZERO, 89},  {RV_CANVAS_INACTIVE, 130}, {RV_CANVAS_MUTED, 185},
};

/* Weighted RGB brightness, scaled by 256. */
static int brightness(rv_color color)
{
    return 54 * (int)(color >> 16 & 0xff) + 183 * (int)(color >> 8 & 0xff) +
           19 * (int)(color & 0xff);
}

void rv_canvas_begin(struct rv_canvas *canvas, const struct rv_gfx *gfx, int width,
                     int height)
{
    rv_color *colors = canvas->colors;
    canvas->gfx = gfx;
    canvas->width = width;
    canvas->height = height;
    for (unsigned int i = 0; i < sizeof roles; i++)
        colors[i] = gfx->color(roles[i]);
    for (unsigned int i = 0; i < sizeof greys / sizeof greys[0]; i++)
        colors[greys[i][0]] =
            gfx->mix(colors[RV_CANVAS_BACKGROUND], colors[RV_CANVAS_TEXT], greys[i][1]);
    /* The selection tints the background toward the accent, further on a dark one. */
    int dark = brightness(colors[RV_CANVAS_BACKGROUND]) < 128 * 256;
    colors[RV_CANVAS_SELECTION] = gfx->mix(colors[RV_CANVAS_BACKGROUND],
                                           colors[RV_CANVAS_ACCENT], dark ? 64 : 40);
    rv_canvas_rect(canvas, 0, 0, width, height, RV_CANVAS_BACKGROUND);
}

void rv_canvas_rect(const struct rv_canvas *canvas, int x, int y, int width, int height,
                    uint8_t color)
{
    struct rv_rect r = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    canvas->gfx->fill(&r, canvas->colors[color]);
}

void rv_canvas_text(const struct rv_canvas *canvas, int x, int y, int width, int height,
                    const char *text, int large, int8_t align, uint8_t color)
{
    const struct rv_gfx *g = canvas->gfx;
    struct rv_rect r = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    uint8_t font = large && g->measure(RV_FONT_LARGE, text) <= width - 8 ? RV_FONT_LARGE
                                                                         : RV_FONT_TEXT;
    g->text_in(&r, font, canvas->colors[color], text, align);
}

void rv_canvas_header(const struct rv_canvas *canvas, const char *title,
                      const struct rv_canvas_legend *legend, unsigned int count)
{
    rv_canvas_text(canvas, 8, HEADER_TOP, canvas->width - 16, HEADER_HEIGHT, title, 0,
                   RV_ALIGN_LEFT, RV_CANVAS_TEXT);
    int x = canvas->width - 8;
    while (count--) {
        int width = canvas->gfx->measure(RV_FONT_TEXT, legend[count].label);
        x -= width;
        rv_canvas_text(canvas, x, HEADER_TOP, width, HEADER_HEIGHT, legend[count].label,
                       0, RV_ALIGN_LEFT, RV_CANVAS_MUTED);
        x -= 4 + SWATCH;
        rv_canvas_rect(canvas, x, SWATCH_TOP, SWATCH, SWATCH, legend[count].color);
        x -= 10;
    }
}

/* Rounded to the nearest row, a half row away from the row of value 0: a
 * value above zero goes up, one below goes down. */
int rv_plot_y(const struct rv_plot *plot, int value)
{
    int range = plot->top_value - plot->bottom_value, height = plot->bottom - plot->top;
    if (value > plot->top_value)
        value = plot->top_value;
    if (value < plot->bottom_value)
        value = plot->bottom_value;
    int twice = 2 * (plot->top_value - value) * height + range;
    return plot->top + (value > 0 ? twice - 1 : twice) / (2 * range);
}

void rv_plot_grid(const struct rv_canvas *canvas, const struct rv_plot *plot,
                  const char *const *scale, unsigned int lines)
{
    for (unsigned int i = 0; i < lines; i++) {
        int y = plot->top + (int)i * (plot->bottom - plot->top) / ((int)lines - 1);
        rv_canvas_rect(canvas, plot->left, y, plot->points, 1, RV_CANVAS_GRID);
        rv_canvas_text(canvas, 0, y - 9, SCALE_WIDTH, 18, scale[i], 0, RV_ALIGN_CENTER,
                       RV_CANVAS_MUTED);
    }
}

void rv_plot_ticks(const struct rv_canvas *canvas, const struct rv_plot *plot,
                   const char *const *labels, unsigned int count, int y, int height)
{
    int span = plot->points - 1, steps = (int)count - 1;
    for (unsigned int i = 0; i < count; i++) {
        int x = plot->left + ((int)i * span + steps / 2) / steps;
        rv_canvas_rect(canvas, x, plot->top, 1, plot->bottom - plot->top + 1,
                       RV_CANVAS_GRID);
        /* The last label would run off the canvas centred on its tick, so its
         * box leans left and reaches the edge. */
        if (i + 1 < count)
            rv_canvas_text(canvas, x - 21, y, 42, height, labels[i], 0, RV_ALIGN_CENTER,
                           RV_CANVAS_MUTED);
        else
            rv_canvas_text(canvas, x - 33, y, canvas->width - (x - 33), height,
                           labels[i], 0, RV_ALIGN_CENTER, RV_CANVAS_MUTED);
    }
}

void rv_plot_curve(const struct rv_canvas *canvas, const struct rv_plot *plot,
                   const int16_t *values, int thickness, uint8_t color)
{
    int previous = rv_plot_y(plot, values[0]);
    for (unsigned int i = 0; i < plot->points; i++) {
        int y = rv_plot_y(plot, values[i]);
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        rv_canvas_rect(canvas, plot->left + (int)i, top, 1, bottom - top + thickness,
                       color);
        previous = y;
    }
}

static void cells(const struct rv_canvas *canvas, int y, int height,
                  const char *const *titles, const char *const *values,
                  const uint8_t *states, unsigned int count, int stacked)
{
    int width = canvas->width;
    rv_canvas_rect(canvas, 0, y - 1, width, 1, RV_CANVAS_BORDER);
    for (unsigned int i = 0; i < count; i++) {
        int left = (int)i * width / (int)count;
        int cell_width = (int)(i + 1) * width / (int)count - left;
        uint8_t state = states[i] & ~RV_CELL_MUTED;
        uint8_t title_color = RV_CANVAS_MUTED;
        uint8_t value_color =
            states[i] & RV_CELL_MUTED ? RV_CANVAS_INACTIVE : RV_CANVAS_TEXT;
        if (i)
            rv_canvas_rect(canvas, left, y, 1, height, RV_CANVAS_BORDER);
        if (state == RV_CELL_EDITING) {
            /* The bar covers the border row above with its top line and the
             * row below with its bottom line, which the next row's border or
             * the canvas edge then takes. */
            struct rv_rect r = {(int16_t)left, (int16_t)(y - 1), (int16_t)cell_width,
                                (int16_t)(height + 2)};
            canvas->gfx->bar(&r);
            title_color = value_color = RV_CANVAS_ON_BAR;
        } else if (state == RV_CELL_SELECTED) {
            rv_canvas_rect(canvas, left, y, cell_width, height, RV_CANVAS_SELECTION);
        }
        if (stacked) {
            rv_canvas_text(canvas, left, y + 5, cell_width, 16, titles[i], 0,
                           RV_ALIGN_CENTER, title_color);
            rv_canvas_text(canvas, left, y + 21, cell_width, 22, values[i], 1,
                           RV_ALIGN_CENTER, value_color);
        } else {
            rv_canvas_text(canvas, left + 12, y, cell_width - 24, height, titles[i], 0,
                           RV_ALIGN_LEFT, title_color);
            rv_canvas_text(canvas, left + 12, y, cell_width - 24, height, values[i], 0,
                           RV_ALIGN_RIGHT, value_color);
        }
    }
}

void rv_canvas_cells(const struct rv_canvas *canvas, int y, int height,
                     const char *const *titles, const char *const *values,
                     const uint8_t *states, unsigned int count)
{
    cells(canvas, y, height, titles, values, states, count, 1);
}

void rv_canvas_fields(const struct rv_canvas *canvas, int y, int height,
                      const char *const *titles, const char *const *values,
                      const uint8_t *states, unsigned int count)
{
    cells(canvas, y, height, titles, values, states, count, 0);
}

enum rv_editor_event rv_editor_input(struct rv_editor *editor,
                                     const struct rv_input *input)
{
    switch (input->kind) {
    case RV_INPUT_WHEEL:
        return RV_EDITOR_WHEEL;
    case RV_INPUT_HOLD:
        return RV_EDITOR_CONSUMED;
    case RV_INPUT_PRESS:
        if (input->button == RV_BUTTON_CENTER)
            return RV_EDITOR_CONSUMED;
        if (input->button == RV_BUTTON_MENU && editor->depth) {
            editor->depth--;
            return RV_EDITOR_BACK;
        }
        return RV_EDITOR_IGNORED;
    case RV_INPUT_RELEASE:
        if (input->button != RV_BUTTON_CENTER)
            return RV_EDITOR_IGNORED;
        if (editor->depths <= 1)
            return RV_EDITOR_CONSUMED;
        if (editor->depth + 1 < editor->depths) {
            editor->depth++;
            return RV_EDITOR_DEEPER;
        }
        editor->depth--;
        return RV_EDITOR_BACK;
    case RV_INPUT_LONG_PRESS:
        return input->button == RV_BUTTON_CENTER ? RV_EDITOR_RESET : RV_EDITOR_IGNORED;
    default:
        return RV_EDITOR_IGNORED;
    }
}

uint16_t *rv_canvas_bmap(void *block, int width, int height)
{
    uint8_t *b = block;
    uint32_t stride = 2 * RV_CANVAS_BMAP_PITCH(width);
    int32_t *bounds = (int32_t *)(b + BMap_bounds);
    memset(b, 0, BMap_payload);
    *(uint16_t *)(b + BMap_format) = BMapFormat_RGB565;
    *(uint16_t *)(b + BMap_stride) = (uint16_t)stride;
    *(uint16_t *)(b + BMap_depth) = 16;
    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = height;
    bounds[3] = width;
    *(uint32_t *)(b + BMap_payload_bytes) = stride * (uint32_t)height;
    return (uint16_t *)(b + BMap_payload);
}
