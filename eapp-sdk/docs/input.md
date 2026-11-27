# Input

`eapp_application` holds up to five callbacks, all on the host's task.

| Callback | Runs |
|---|---|
| `init` | First, before the host has an OpenGL ES context |
| `start` | Once before the first frame and its input events, with the context current |
| `input` | Per button event |
| `frame` | Per frame |
| `shutdown` | Last |

`eapp_request_exit` ends the app after the current callback returns.

## Frames

`frame` gets a `struct eapp_frame`.

| Field | |
|---|---|
| `framebuffer` | `EAPP_SCREEN_WIDTH` x `EAPP_SCREEN_HEIGHT` (320 x 240) RGB565 pixels, valid for the call and shown when it returns. See [Drawing](drawing.md) |
| `ticks_ms` | The `eapp_ticks_ms` clock at the frame |
| `delta_ms` | Milliseconds since the previous frame, 0 at the first |
| `wheel` | The wheel since the previous frame, below |
| `held` | An `EAPP_HELD` bit per button down |

## Buttons

`input` gets a `struct eapp_input_event`: the button, `EAPP_INPUT_PRESS` or `EAPP_INPUT_RELEASE`, and a timestamp on the `eapp_ticks_ms` clock. The buttons are Menu, Center, Next, Previous and Play; a wheel touch and lift arrive as `EAPP_BUTTON_WHEEL`. Releases of Play, Next and Previous then go to the music player unless `handles_music_buttons` is set, see [Music](music.md).

`eapp_hold_locked` says whether the Hold switch is on.

## Wheel

`frame->wheel` has `delta`, the clockwise motion since the previous frame in 96ths of a turn, `position` (right 0, up 64, left 128, down 192) and `touched`. `eapp_wheel_directions` answers the position as one of eight sectors, as Up, Right, Down and Left bits.

`eapp_wheel_steps_consume(&steps, frame, threshold)` turns the motion into list steps the way the iPod's own lists do:

- one step per `threshold` 96ths of a turn, 4 for a list row and 16 for a slider,
- the remainder carries while the finger stays down and drops when it lifts,
- a slow turn, under one turn in four seconds, gives at most one step a frame,
- a fast turn in one direction doubles the motion every 350 ms, to at most 32 times, until the direction changes or the wheel rests for 400 ms.

## Menus

`eapp_ui.h` runs and draws a list with one row selected. Zero a `struct eapp_menu`, then set `items` and `count`. Each item has a `label` and an optional `value` drawn at the right. A `title` takes the first row.

| Call | |
|---|---|
| `eapp_menu_input(menu, event)` | Previous and Next move the selection. Answers `EAPP_MENU_SELECT` for Center and `EAPP_MENU_BACK` for Menu |
| `eapp_menu_scroll(menu, frame)` | Moves the selection by the wheel |
| `eapp_menu_draw(menu, fb)` | Clears the menu's region and draws the rows that fit, scrolled so the selection stays in view, a light bar under the selected row |

The region is the whole framebuffer, or `x`, `y`, `width` and `height` when set. `scale` and the five colours override the defaults when set. Rows are `EAPP_MENU_ROW_HEIGHT` (12) font pixels high. Hints and empty-list texts are the app's to draw after.

## Screens

`eapp_ui.h` also draws the screens around a menu, in the menu's style: zero the struct, set what differs, and the region, `scale` and colours (0 the default, 0x0001 black) work as the menu's. A title, where there is one, is drawn one size larger than the body. The defaults are a dark screen with white text, dim headings and a light bar with dark text.

| Call | |
|---|---|
| `eapp_message_draw(fb, text, scale, background, frame, color)` | The text centred in a framed box over the frame as drawn so far: `scale` pixels of frame, 8 font pixels of room either side, 20 font pixels high, centred in the framebuffer. For a pause or a splash over a playfield |
| `eapp_table_draw(table, fb)` | Rows of cells under column headings, with a title above when set and a bar under the `highlighted` row (-1 for none). Each `eapp_table_column` places its heading and cells by `align` across `width` pixels from `x` of the region; `cells` holds `rows` * `column_count` strings row by row, NULL for an empty cell. Rows are `EAPP_TABLE_ROW_HEIGHT` (13) font pixels and only whole rows are drawn; the columns place from the region's left edge, the bar keeps 4 font pixels in |
| `eapp_page_input(page, event)` | Previous and Next scroll a line. Answers `EAPP_MENU_BACK` for Menu |
| `eapp_page_scroll(page, frame)` | Scrolls by the wheel |
| `eapp_page_draw(page, fb)` | The page's `blocks` of text, each wrapped to the region and in the heading colour when `heading` is set, a blank line between them, from line `offset`, which the page keeps between 0 and the lines beyond the region once a draw has counted them; only whole lines are drawn. Lines are `line_height` pixels apart, `EAPP_PAGE_LINE_HEIGHT` (8) font pixels when 0 |
| `eapp_dialog_input(dialog, event)` | Previous and Next move the selection. Answers `EAPP_DIALOG_CHOOSE` for Center, with `selected` the answer, and `EAPP_DIALOG_CANCEL` for Menu |
| `eapp_dialog_scroll(dialog, frame)` | Moves the selection by the wheel, one option per sixth of a turn |
| `eapp_dialog_draw(dialog, fb)` | The title, the `message` wrapped below it and stopping above the options, and the `options` in a row along the bottom, each as wide as the widest, the selected one on a bar. Options the region cannot hold share its width and their text is cut at their edges |

## File picker

`eapp_file_picker_open(picker, title, directory, extension)` fills a menu with the files of that extension under a package folder and its subfolders, sorted by name. A row's label is the file name without the extension, at most 42 characters, and `paths[row]` the file's path inside the package, so `paths[menu.selected]` is the chosen file. At most 64 files fit. `eapp_file_picker_close` frees the labels and paths.
