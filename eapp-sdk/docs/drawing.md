# Drawing

`eapp_draw.h` draws on a framebuffer directly. The frame's is `frame->framebuffer`, 320 x 240 RGB565 pixels. `EAPP_RGB565(r, g, b)` makes a colour from 8-bit components. Every call clips to the framebuffer.

| Call | Draws |
|---|---|
| `eapp_clear(fb, color)` | The whole framebuffer |
| `eapp_fill_rect(fb, x, y, width, height, color)` | A filled rectangle |
| `eapp_draw_rect(...)` | Its 1 px outline, just inside |
| `eapp_draw_pixel(fb, x, y, color)` | One pixel |
| `eapp_draw_line(fb, x0, y0, x1, y1, color)` | A 1 px line, both ends included |
| `eapp_fill_triangle(fb, x0, y0, x1, y1, x2, y2, color)` | A filled triangle |
| `eapp_blend_triangle(fb, x, y, alpha, color)` | `color` blended over a triangle with corners `x[i]`, `y[i]` in 16.16 pixels, the alpha going linearly from `alpha[i]` (0 to 255) at each corner. Only pixels whose centres lie inside are drawn, so triangles sharing an edge blend each pixel once |
| `eapp_draw_circle(fb, cx, cy, radius, color)`, `eapp_fill_circle(...)` | A 1 px circle, or a filled one |
| `eapp_draw_round_rect(fb, x, y, width, height, radius, color)`, `eapp_fill_round_rect(...)` | A rectangle with quarter-circle corners, outlined or filled |
| `eapp_draw_bitmap(fb, x, y, &bitmap)` | A `struct eapp_bitmap`, below |
| `eapp_draw_bitmap_part(fb, x, y, &bitmap, source_x, source_y, width, height)` | A part of it, for a sprite sheet |
| `eapp_draw_mono_bitmap(fb, x, y, bits, width, height, color)` | The set bits in `color`: one bit per pixel, most significant first, rows padded to a byte |
| `eapp_copy_rect(fb, x, y, source, source_x, source_y, width, height)` | A rectangle of the framebuffer `source`, cut to both. The two must not share pixels |

## Bitmaps and packs

A `struct eapp_bitmap` is RGB565 pixels with a width, height and stride, and a `transparent` colour that is skipped (-1 for none).

| Call | |
|---|---|
| `eapp_bitmap_load(EAPP_IMAGE_TILES)` | Reads a picture of `IMAGES` into the heap |
| `eapp_bitmap_free(bitmap)` | Frees it |
| `eapp_pack_load(EAPP_PACK_SCENE)` | Reads a whole pack of `IMAGE_PACKS` in one disk request |
| `eapp_pack_bitmap(pack, EAPP_PACK_SCENE_HERO)` | One entry, over the pack's memory |
| `eapp_pack_free(pack)` | Frees the pack and its entries |

Load packs when a scene starts, not in a frame. [Build](build.md) lists the Makefile variables, [Image](image.md) the file formats.

## Text

The SDK font is 5 x 7 with the printable ASCII characters 32 to 126. Any other byte leaves its cell empty. Every font pixel is `scale` screen pixels.

| Call | |
|---|---|
| `eapp_draw_text(fb, x, y, text, color, scale)` | One line |
| `eapp_draw_text_in(fb, x, y, width, text, color, scale, align)` | One line at the left, centre or right of the `width` pixels from `x` (`EAPP_ALIGN_LEFT`, `EAPP_ALIGN_CENTER`, `EAPP_ALIGN_RIGHT`) |
| `eapp_draw_text_wrapped(fb, x, y, width, line_height, text, color, scale)` | Lines of at most `width` pixels, `line_height` apart, broken at spaces, inside a word too long for a line, and at newlines. Spaces at a line's start are dropped, lines over 63 glyphs cut. Returns the line count; a NULL framebuffer only measures |
| `eapp_text_width(text, scale)`, `eapp_text_height(scale)` | The pixels a text spans. Glyph cells are `EAPP_FONT_CELL` (6) font pixels apart with no gap after the last, `EAPP_FONT_HEIGHT` (7) rows high |

## Off-screen framebuffers

`eapp_framebuffer_create(width, height)` makes a black framebuffer in the heap, the width as stride, sides up to `EAPP_FRAMEBUFFER_MAX_SIDE` (4096). Every call above takes it in place of the frame's. `eapp_framebuffer_free` frees it. A 320 x 240 framebuffer is 150 KiB of heap. `eapp_read_pixel(fb, x, y)` returns the value at (x, y), -1 outside the framebuffer.

The frame's buffer alternates between two. An app that keeps its picture off screen copies the whole of it to every frame; a copy of only what changed shows the frame before last around it.
