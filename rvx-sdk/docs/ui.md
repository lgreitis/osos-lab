# User interface

An extension declares its UI in the descriptor and the core draws it as stock screens. Each setting, screen and entry is one `rv_ui.h` macro, named after the kind it builds: `RV_ROW_PUSH` builds an `RV_ROW_KIND_PUSH` row, `RV_SCREEN_LIST` an `RV_SCREEN_KIND_LIST` screen.

## Rows

| Row | What it does |
|---|---|
| `RV_ROW_PUSH(label, screen)` | opens one of the extension's screens |
| `RV_ROW_ACTION(label, fn)` | calls `fn` |
| `RV_ROW_SETTING(label, key)` | shows a setting's value; flips a bool, steps a labelled int to its next label or opens the setting's screen |
| `RV_ROW_INFO(label, text)` | shows `text` (or `label`); does nothing when chosen |
| `RV_ROW_STOCK(stock)` | moves one of Apple's Settings rows (an `RV_SETTINGS_*`) into the list; see [Moved stock rows](menus.md#moved-stock-rows) |

Every row macro takes `.id = "name"` after its arguments, the name other extensions anchor [hosted entries](menus.md#hosted-entries) on.

## Settings

`RV_SETTING_BOOL`, `RV_SETTING_INT`, `RV_SETTING_LABELLED`, `RV_SETTING_CHOICE`, `RV_SETTING_SLIDER` and `RV_SETTING_MULTI` declare a setting, the last four with its labels. An `RV_ROW_SETTING` row shows it; choice, multi-choice and slider settings get a generated screen. See [Settings](settings.md).

## Screens

| Screen | What the user sees |
|---|---|
| `RV_SCREEN_LIST(id, title, rows)` | a menu, full width or beside the Settings preview pane as the screen it opens from decides; `.layout` overrides |
| `RV_SCREEN_LIST_PANES(id, title, rows, panes)` | the same with an `rv_pane` per row: the caption and icon shown while the row is highlighted, static or from `.caption_text` and `.bitmap_for`; the icon stands on the system's box, which a row without a bitmap, or with `.plain_icon`, leaves out |
| `RV_SCREEN_CHOICE`, `RV_SCREEN_MULTI_CHOICE`, `RV_SCREEN_SLIDER` | a screen over a setting, with its own title; beside Settings a choice screen's pane names the highlighted choice under its `.bitmap`, like Settings > EQ |
| `RV_SCREEN_TEXT(id, title, paragraphs)` | a text page of up to 12 paragraphs |
| `RV_SCREEN_DYNAMIC_LIST(id, title, count, row)` | rows from callbacks, with an inline or stacked detail; `.dynamic.select` when choosing a row does something |
| `RV_SCREEN_CANVAS(id, title, draw, input)` | the extension draws and reads wheel and key input |
| `RV_SCREEN_CONTEXT(rows)` | a long-press menu on a list or dynamic list |
| `RV_SCREEN_STOCK(id, title, screen)` | one of Apple's screens with its controller and bindings, under the extension's title. `RV_STOCK_SCREEN_GAMES` is the Games list, showing the games service's single list |
| `RV_SCREEN_DIALOG(id, title, message, detail, buttons, chosen)` | a question with one to three buttons, as the stock EU Volume Limit question, see [Dialogs](#dialogs) |

| Screen field | |
|---|---|
| `.layout` | `RV_LAYOUT_FULL` or `RV_LAYOUT_SPLIT` for a list kind; `RV_LAYOUT_AUTO` follows the screen it opens from |
| `.transition` | for a slider or canvas, `RV_TRANSITION_FROM_PANE`: opened from a split list it starts where the preview pane is and slides into place, as Extras > Stopwatch does |
| `.caption`, `.bitmap` | the line of text and the icon beside a list in the split layout |
| `.opened`, `.closed` | after the screen was pushed, and after it closed for good; a canvas's `.closed` runs after the last draw of its slide-out. Not called when the extension unloads under an open screen; `unload` covers that |
| `.dynamic.detail` | `RV_DETAIL_BESIDE` (the default), `RV_DETAIL_BELOW` or `RV_DETAIL_BELOW_SUBMENU` for a dynamic list's detail text |

A row's `.text`, when set, gives the label each time the row is drawn, for a value that changes.

Screen ids are local to the extension. `api->ui_push(self, id, arg)` opens one from the UI task; `ui_pop` closes the extension's own top screen. Labels and paragraphs are read from the module while a screen is open, not copied.

## Entries

`RV_MENU_ENTRY(menu, after, row)` puts a row into a stock menu, `RV_MENUS_ENTRY(menus, 0, row)` into every menu of a mask of `RV_MENU_BIT(menu)` bits; see [Menu placement](menus.md).

The menus are `RV_MENU_MAIN`, `RV_MENU_MUSIC`, `RV_MENU_EXTRAS`, `RV_MENU_SETTINGS` and `RV_MENU_VIDEOS`, and the context menus `RV_MENU_NOW_PLAYING_CONTEXT`, `RV_MENU_SONG_CONTEXT`, `RV_MENU_ALBUM_CONTEXT`, `RV_MENU_ARTIST_CONTEXT`, `RV_MENU_PODCAST_EPISODE_CONTEXT`, `RV_MENU_ITUNES_U_EPISODE_CONTEXT`, `RV_MENU_AUDIOBOOK_CONTEXT`, `RV_MENU_MOVIE_CONTEXT`, `RV_MENU_TV_EPISODE_CONTEXT` and `RV_MENU_VIDEO_PLAYLIST_CONTEXT`. A context menu row receives the item in its `rv_context`.

| Entry field | |
|---|---|
| `.pane`, `.extra` | the menu pane's content, an `RV_PANE(...)` and an `RV_PANE_EXTRA(...)`, see [Menu placement](menus.md); `.caption_setting` shows a setting's label as the caption |
| `.preview` | on a Main row, `RV_PREVIEW_MUSIC` or `RV_PREVIEW_VIDEOS`: without a pane, the row shows the stock Music or Videos pane |
| `.shown_by` | a bool setting that shows the row; Settings > Main Menu and Music Menu flip it |
| `.available` | decides whether a context menu offers the row, each time it opens |
| `.host`, `.host_screen`, `.host_after` | an extension's manifest id, one of its list screens and a row id in it: the row goes after that row while that extension runs, into the entry's menu otherwise; see [Hosted entries](menus.md#hosted-entries) |

## Callbacks and `rv_context`

Every UI callback receives a `struct rv_context`.

| Field | |
|---|---|
| `self` | the extension's handle |
| `controller` | the Apple controller of the screen or menu, for Apple calls |
| `arg` | what `ui_push` passed; NULL for a screen a row opened |
| `item`, `row`, `list` | in a context menu's action: the song, episode, audiobook or video, the model row and the list controller (`list` is NULL for Now Playing) |

## Dynamic lists

| | |
|---|---|
| `.dynamic.initial` | the row the list opens on |
| `.dynamic.status(ctx, index)` | `RV_ROW_STATUS_NONE` or `RV_ROW_STATUS_PLAYING`; the core supplies the stock speaker icons |
| `.dynamic.empty` | a centred message when the row count is zero |
| `api->ui_refresh(self, id)` | re-reads the rows and the empty message |
| `api->ui_select(self, id, row)` | highlights a row, clamped to the count, now or when a covered screen becomes active again |

`api->ui_redraw(self)` redraws everything on screen, after a colour or artwork change. `api->ui_pane_refresh(self)` recomputes panes that last showed one of the extension's rows, and its list screen's per-row pane (see [Menu placement](menus.md)).

An empty list has no selection.

## Context menus

`RV_SCREEN_CONTEXT(rows)` of `RV_CONTEXT_ROW(row, .available = predicate)` attaches to a list or dynamic list. Action and push rows get the highlighted index in `ctx->row`. Unavailable rows are hidden.

## Dialogs

`RV_SCREEN_DIALOG` (API 1.85) shows `message` in bold, `detail` under it (NULL for none) and the labels of the `buttons` array in the option bar.

| | |
|---|---|
| `.dialog.initial` | the button highlighted when the dialog opens, the first by default |
| `chosen(ctx, index)` | Select on button `index`; returns 1 to close the dialog, 0 to leave it up until `ui_pop` closes it, and 0 when it pushed or popped a screen itself. NULL: Select closes it |
| Menu | closes the dialog without calling `chosen` |

## Canvases

A canvas draws with the `rv_gfx` table: fills, text in the stock fonts, colour roles and mixes between them, wrapped paragraphs, list rows, the selection bar (`bar`, across any rectangle), and bitmaps (whole, the top rows, or tiled). A fill with alpha below 0xff blends, and `bmap_rows_blend` draws the top rows of a picture at an opacity. One extension at a time may replace the colour roles for every canvas with `api->ui_set_colors`, as a theme does; the `RV_COLOR_BAR_*` roles are the bar's top line, gradient and bottom line. The [canvas kit](canvas.md) adds palettes, plots, value cells and [computed pictures](canvas.md#pictures).

Input is wheel and key events, `RV_INPUT_LONG_PRESS` after a key is held for about a second (no release follows), and `RV_INPUT_HOLD` when the hold switch moves. The input callback returns 1 for a consumed event. A release goes where its press went, whatever the callback returns.

`ui_invalidate_rect` redraws part of the canvas, and `gfx->clip()` tells a draw which part it covers. `.closed` runs after the last draw of the slide-out, so free drawing resources there.

## Timers

On the UI task, `api->timer_start(self, ms, repeat, callback, arg)` calls `callback(arg)` on the UI task after `ms`, and every `ms` after that with `repeat`; NULL when the heap is full. `api->timer_stop(self, timer)` ends one; a one-shot timer ends by itself once it fired and must not be stopped after that. `rv_timer_cancel(api, self, &timer)` stops a timer the extension keeps and clears it. The core stops a module's timers before the module unloads.

## Resource ids

Screens and entries are limited by the heap and by 4,096 resource ids per extension. A load that runs out shows No memory in Settings > Extensions.

| Declaration | Ids |
|---|---|
| list | 19 + 2 per row |
| choice, multi-choice or dynamic list | 21 |
| stock screen | 8 |
| dialog | 11 + 1 per button |
| context menu | 5 + 1 per context row |
| text page | 8 + 1 per paragraph |
| slider | 10 |
| canvas | 8 (held bitmaps, in total across open canvases) |
| menu entry | 2, or 3 under Settings |
