/* SPDX-License-Identifier: GPL-3.0-only */

/* What an editor canvas draws with: a palette from the stock colour roles, a
 * plot with its grid, ticks and curves, a row of value cells, the Select and
 * Menu input that moves through them, and a picture the extension computes
 * itself. Everything here is pixel work on the rv_gfx table a draw callback
 * receives.
 *
 * Adapted from RepriseOS payload/eq_canvas.c (d640115, 93f6a45) by lgreitis. */

#ifndef RV_CANVAS_H
#define RV_CANVAS_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"

/* Four stock roles, then greys between the background and the text and a
 * tint of the background toward the accent, derived on every rv_canvas_begin
 * since a theme may change the roles. */
enum rv_canvas_color {
    RV_CANVAS_BACKGROUND,
    RV_CANVAS_ACCENT, /* the accent as text: a curve, the selected label */
    RV_CANVAS_TEXT,
    RV_CANVAS_ON_BAR, /* text on the selection bar */
    RV_CANVAS_MUTED,  /* titles, scales */
    RV_CANVAS_INACTIVE,
    RV_CANVAS_ZERO, /* the zero line of a plot */
    RV_CANVAS_BORDER,
    RV_CANVAS_GRID,
    RV_CANVAS_TRACK, /* the groove a bar moves in */
    RV_CANVAS_SELECTION, /* the background tinted toward the accent: the selected cell or band */
    RV_CANVAS_COLOR_COUNT,
};

struct rv_canvas {
    const struct rv_gfx *gfx;
    int width, height;
    rv_color colors[RV_CANVAS_COLOR_COUNT];
};

/* Loads the palette and clears the canvas to the background. */
RV_MODULE_CALL void rv_canvas_begin(struct rv_canvas *canvas, const struct rv_gfx *gfx,
                                    int width, int height);

RV_MODULE_CALL void rv_canvas_rect(const struct rv_canvas *canvas, int x, int y,
                                   int width, int height, uint8_t color);

/* `text` on one line in the box, vertically centred, across it by `align`
 * (rv_align). `large` asks for the large font and falls back to the text font
 * when the text does not fit the box with 4 px on either side. */
RV_MODULE_CALL void rv_canvas_text(const struct rv_canvas *canvas, int x, int y,
                                   int width, int height, const char *text, int large,
                                   int8_t align, uint8_t color);

/* The header line of a canvas: `title` at the left and, from the right edge, a
 * swatch and label per legend entry in order. */
struct rv_canvas_legend {
    const char *label;
    uint8_t color;
};

RV_MODULE_CALL void rv_canvas_header(const struct rv_canvas *canvas, const char *title,
                                     const struct rv_canvas_legend *legend,
                                     unsigned int count);

/* A plot: `points` columns from `left`, values from `top_value` at `top` to
 * `bottom_value` at `bottom`, in whatever unit the values are (the editors use
 * tenths of a decibel). The two values differ. */
struct rv_plot {
    int16_t left, top, bottom;
    uint16_t points;
    int16_t top_value, bottom_value;
};

/* The row of `value`, clamped to the plot and rounded to the nearest pixel. */
RV_MODULE_CALL int rv_plot_y(const struct rv_plot *plot, int value);

/* `lines` (at least two) horizontal grid lines evenly spaced from top to
 * bottom, each with its `scale` label centred in the 38 px left of the plot. */
RV_MODULE_CALL void rv_plot_grid(const struct rv_canvas *canvas,
                                 const struct rv_plot *plot, const char *const *scale,
                                 unsigned int lines);

/* `count` (at least two) vertical grid lines evenly spaced across the plot,
 * each with its label centred under it in a box `height` tall at `y`; the last
 * label's box leans left to the canvas edge so it stays inside. */
RV_MODULE_CALL void rv_plot_ticks(const struct rv_canvas *canvas,
                                  const struct rv_plot *plot, const char *const *labels,
                                  unsigned int count, int y, int height);

/* A curve through `values`, one per column: each column a run from the row of
 * the column before to its own, `thickness` pixels at the far end. */
RV_MODULE_CALL void rv_plot_curve(const struct rv_canvas *canvas,
                                  const struct rv_plot *plot, const int16_t *values,
                                  int thickness, uint8_t color);

/* A row of cells across the canvas, each a muted title over a large value,
 * with a border above and dividers between. A state is one of the first
 * three, with RV_CELL_MUTED added when the value plays no part in what is
 * chosen. */
enum rv_cell_state {
    RV_CELL_IDLE,
    RV_CELL_SELECTED,  /* tinted toward the accent: the wheel would edit it */
    RV_CELL_EDITING,   /* a highlighted list row: the wheel edits it */
    RV_CELL_MUTED = 4, /* the value reads inactive */
};

RV_MODULE_CALL void rv_canvas_cells(const struct rv_canvas *canvas, int y, int height,
                                    const char *const *titles,
                                    const char *const *values, const uint8_t *states,
                                    unsigned int count);
/* The same row with each title at the left of its cell and the value at the
 * right, for a row too low for two lines. */
RV_MODULE_CALL void rv_canvas_fields(const struct rv_canvas *canvas, int y, int height,
                                     const char *const *titles,
                                     const char *const *values, const uint8_t *states,
                                     unsigned int count);

/* The Select and Menu input of an editor whose wheel works at `depths` levels:
 * one when the wheel edits the only value and a Select release does nothing,
 * two for choosing a value and editing it, three with a level above. Select,
 * when released, goes one level deeper, from the deepest back one; held, it
 * asks for what is highlighted to go back to its default. Menu goes back one
 * and at level 0 leaves the key to the system, which pops the screen. The
 * wheel is reported with the level it turned at in `depth`. Other releases
 * and the playback keys are the system's; the hold switch is consumed. */
enum rv_editor_event {
    RV_EDITOR_IGNORED, /* the input callback returns 0 */
    RV_EDITOR_CONSUMED,
    RV_EDITOR_WHEEL, /* input->delta at depth */
    RV_EDITOR_DEEPER,
    RV_EDITOR_BACK,
    RV_EDITOR_RESET, /* what `depth` highlights returns to its default */
};

struct rv_editor {
    uint8_t depth, depths;
};
RV_MODULE_CALL enum rv_editor_event rv_editor_input(struct rv_editor *editor,
                                                    const struct rv_input *input);

/* A picture the extension computes itself, as a BMap for rv_gfx.bmap and
 * bmap_rows: RGB565 rows padded to a word after the BMap header, in a word
 * aligned block RV_CANVAS_BMAP_BYTES(width, height) long. The core copies the
 * pixels to the screen on every draw, converting to the screen's format, so
 * the block is read during the draw only. */
#define RV_CANVAS_BMAP_PITCH(width) (((width) + 1) & ~1) /* pixels per row */
#define RV_CANVAS_BMAP_BYTES(width, height)                                            \
    (BMap_payload + 2 * RV_CANVAS_BMAP_PITCH(width) * (height))

/* Writes the header of a `width` by `height` picture into `block` and returns
 * its first pixel; row y starts RV_CANVAS_BMAP_PITCH(width) pixels after row
 * y - 1. */
RV_MODULE_CALL uint16_t *rv_canvas_bmap(void *block, int width, int height);

#endif
