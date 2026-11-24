# Canvas kit

`rv_canvas.h` builds editor canvases on the `rv_gfx` table: palette, plots, value cells, Select/Menu input and computed pictures. `rv.mk` links `rv_canvas.c` into every module; a module carries only what it calls. The editors of the first-party audio effects use it.

## Palette

`rv_canvas_begin(&canvas, gfx, width, height)` fills a `struct rv_canvas` with the palette and clears the canvas. `canvas.width` and `canvas.height` are the drawable area under the title bar; size a bottom row from `canvas.height` to reach the screen edge. Call `rv_canvas_begin` on every draw, since a theme may change the colour roles.

| Colour | What it is |
|---|---|
| `RV_CANVAS_BACKGROUND`, `RV_CANVAS_TEXT` | the stock roles |
| `RV_CANVAS_ACCENT` | the accent as text: a curve, the selected label |
| `RV_CANVAS_ON_BAR` | text on the selection bar |
| `RV_CANVAS_MUTED`, `RV_CANVAS_INACTIVE`, `RV_CANVAS_ZERO`, `RV_CANVAS_BORDER`, `RV_CANVAS_GRID`, `RV_CANVAS_TRACK` | greys between the background and the text, nearest the background first |
| `RV_CANVAS_SELECTION` | the background tinted toward the accent |

## Drawing

| Call | Draws |
|---|---|
| `rv_canvas_rect(&canvas, x, y, width, height, color)` | a filled rectangle |
| `rv_canvas_text(&canvas, x, y, width, height, text, large, align, color)` | one line in the box, vertically centred; `large` asks for the large font and falls back to the text font when the text does not fit |
| `rv_canvas_header(&canvas, title, legend, count)` | the title at the left of the header line and, from the right, a swatch and label per `struct rv_canvas_legend` |
| `rv_canvas_cells(&canvas, y, height, titles, values, states, count)` | a row of cells across the canvas, a muted title over a large value each, with a border above and dividers between |
| `rv_canvas_fields(&canvas, y, height, titles, values, states, count)` | the same row with the title at the left of each cell and the value at the right, for a row too low for two lines |

A cell's state is `RV_CELL_IDLE`, `RV_CELL_SELECTED` (filled with `RV_CANVAS_SELECTION`) or `RV_CELL_EDITING` (a highlighted list row: the selection bar of `gfx->bar` with the title and value in `RV_CANVAS_ON_BAR`). Add `RV_CELL_MUTED` to draw a value that currently has no effect as inactive.

## Plots

A `struct rv_plot` gives the plot's columns, `points` from `left`, and the values at its `top` and `bottom` rows in the curve's unit.

| Call | Draws |
|---|---|
| `rv_plot_y(&plot, value)` | the row of a value, clamped to the plot |
| `rv_plot_grid(&canvas, &plot, scale, lines)` | horizontal grid lines evenly spaced from top to bottom, each with its `scale` label left of the plot |
| `rv_plot_ticks(&canvas, &plot, labels, count, y, height)` | vertical grid lines evenly spaced across the plot, each with its label under it at `y`; the last label is right-aligned to the canvas edge |
| `rv_plot_curve(&canvas, &plot, values, thickness, color)` | a curve through one value per column |

## Input

`rv_editor_input(&editor, input)` handles Select and Menu for an editor with `editor.depths` levels: one when the wheel edits the only value, two to choose and edit a value, three with a level above, such as the EQ's bands. Releasing Select goes one level deeper, or back one from the deepest; with one level it does nothing. Holding Select resets the highlighted value. Menu goes back one level; at level 0 the system pops the screen.

| Result | What the canvas does |
|---|---|
| `RV_EDITOR_IGNORED` | returns 0 from its input callback |
| `RV_EDITOR_CONSUMED` | returns 1; nothing changed (the hold switch, a Select press, a Select release with one level) |
| `RV_EDITOR_WHEEL` | moves what `editor.depth` selects by `input->delta`, then redraws |
| `RV_EDITOR_DEEPER`, `RV_EDITOR_BACK` | redraws; `editor.depth` has changed |
| `RV_EDITOR_RESET` | puts what `editor.depth` highlights back to its default, then redraws |

```c
static int input(const struct rv_context *ctx, const struct rv_input *input)
{
    switch (rv_editor_input(&editor.input, input)) {
    case RV_EDITOR_IGNORED:
        return 0;
    case RV_EDITOR_CONSUMED:
        return 1;
    case RV_EDITOR_WHEEL:
        wheel(input->delta);
        break;
    default:
        break;
    }
    api->ui_invalidate(self);
    return 1;
}
```

## Pictures

A canvas that computes its own pixels, such as a scaled cover, keeps them as a BMap, the format every `rv_gfx` bitmap call takes. `rv_canvas_bmap(block, width, height)` writes an RGB565 header into a word-aligned block of `RV_CANVAS_BMAP_BYTES(width, height)` and returns the first pixel. Rows are `RV_CANVAS_BMAP_PITCH(width)` pixels apart, padded to a word. The block is read only during the draw, so a stack block works for a strip drawn with `gfx->bmap_rows`; use `api->alloc` for a picture kept while the canvas is open.

```c
static uint32_t cover[RV_CANVAS_BMAP_BYTES(77, 77) / 4];
uint16_t *pixels = rv_canvas_bmap(cover, 77, 77);
/* fill pixels[y * RV_CANVAS_BMAP_PITCH(77) + x] */
g->bmap(x, y, cover);
```

## Value labels

`rv_libc.h` formats cell values: `rv_format_int` ("100 %"), `rv_format_tenths` ("-0.5 dB", "+12.0 dB") and `rv_format_hz` ("700 Hz", "1.5k Hz"). They do not pull in `rv_format`.
