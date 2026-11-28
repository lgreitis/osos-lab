# Menu placement

`RV_MENU_ENTRY(menu, after, row)` places a row in a stock menu after an anchor from `rv_menus.h` for that menu. An `after` of `0` uses the menu's default end anchor; on Main this is Sleep, so the entry sits at root level in the Main Menu editor. `RV_MENUS_ENTRY(menus, 0, row)` places the same row in every menu of a mask, `RV_MENU_BIT(RV_MENU_SONG_CONTEXT) | RV_MENU_BIT(RV_MENU_AUDIOBOOK_CONTEXT)`; an anchor belongs to one menu, so a mask of several needs an `after` of 0.

In Settings > Main Menu an entry takes its anchor's indentation. `RV_MAIN_AUDIOBOOKS`, for example, places a row under Music, between Audiobooks and Search.

## A row on Music and on Main

To offer a Music item on the Main Menu too, declare both entries, as Apple does for Search, each with its own `shown_by` setting. The Music entry alone adds nothing to Main.

```c
static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MAIN, .preview = RV_PREVIEW_MUSIC),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Queue in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Queue in Main Menu", 0),
};
```

The keys must be distinct and nonzero. `.preview = RV_PREVIEW_MUSIC` gives the Main row the stock Music pane (below).

An Extras item on Main works the same way with matching anchors, such as `RV_EXTRAS_STOPWATCH` and `RV_MAIN_STOPWATCH`. `RV_MENU_VIDEOS` places a row in Main > Videos after an `RV_VIDEOS_*` anchor, by default after Settings. Video anchors on Main, such as `RV_MAIN_MOVIES`, place a Main Menu row.

## The pane

Main, Music, Videos, Extras and Settings show a pane beside the list.

A music or video item without a `.pane`, `.extra` or `.caption_setting` shows the stock Music or Videos pane: the note or film picture over the library count, artwork when Menu Covers is on, or "No Music" or "No Videos" for an empty library. Rows in Music and Videos are such items; on Main, a row needs `.preview = RV_PREVIEW_MUSIC` or `RV_PREVIEW_VIDEOS` (read on Main only). Any of them gives the row its own pane instead. This is decided when the menu loads, so a `.bitmap_for` returning NULL shows the caption alone, not the stock pane.

An entry's pane, `.pane = RV_PANE(...)`, shows `.bitmap`, a picture from the extension's [assets](assets.md), with `.caption`; in shapes that have them `.extra = RV_PANE_EXTRA(...)` adds a `.line`, a `.number` and a 20 x 16 `.badge`. `.caption_text`, `.line_text`, `.number_text`, `.bitmap_for` and `.badge_for` compute them when the row is highlighted and on `api->ui_pane_refresh(self)` while a pane still shows the row, for example from a timer. A caption that only names a setting's value is the entry's `.caption_setting = key` instead: the setting's label, On or Off for a bool or an unlabelled int, which follows the setting when it changes. Captions take the stock capitalisation, a word or title-cased phrase like Apple's "3 Games" and "Adjust Your Sound": "Says Hello", "Make It Your Own". The picture's size picks the pane's shape:

| Picture | Shows | Where |
|---|---|---|
| 78 x 121 mask, `pane = "icon"` | the label, the caption below and the icon on the system's box (or without it, with the entry's `.plain_icon`) | Settings only; every Settings row |
| 119 x 119 | the picture with the caption beneath | Main, Music, Videos, Extras; the pane of a row without a picture on Main, Music and Videos that has another pane field, or on Main no `.preview` |
| 142 x 87 | the picture with the caption beneath | the same; the pane of a row without a picture under Extras |
| 130 x 128 | the label as a title line at the top, `.line` over the picture with `.number` in large digits under it, the caption beneath | Main, Music, Videos, Extras |
| 146 x 110 | the picture with the caption beneath it on a line about 90 pixels wide, and the badge before it | Main, Music, Videos, Extras |
| 132 x 145 | the picture in the clock's box over the World Clock pane's world map, the caption on the date line | Music, Videos, Extras |

In the 119 x 119, 142 x 87 and 146 x 110 shapes a missing caption falls back to the label; the others show the label on a title line. A picture of a size the menu has no pane for, such as an icon outside Settings, or one an [asset provider](assets.md#asset-provider) serves at a different size, shows the caption alone. Panes are built the first time they are needed, so a newly enabled extension gets them at once. An Extras item mirrored onto Main uses the same picture in both. The 142 x 87, 130 x 128, 146 x 110 and 132 x 145 shapes are copies of Apple's pane templates whose views take new ids; `api->ui_stock_view(view)` (API 1.91) gives a copied view's stock id, for [hooks](modules.md#hooks) that tell views apart.

These lines use Apple's pane fonts, which leave characters out. Of printable ASCII the 146 x 110 caption draws digits, the space, `.` and `/`, the capitals A, D to H, J to P, S and T, and the small letters except q and x, so a count such as "12/40" suits it; text past its width is cut off. The 130 x 128 `.line` and caption draw every letter except capital X, digits, the space and `. , : - / % & ( ) '`. Its `.number` draws digits and `:`, about five of them, and drops spaces, like the day on Apple's Calendars pane.

## Hosted entries

An entry can put its row into another extension's list screen instead of a stock menu, after one of that screen's rows, the way a stock entry follows an anchor. The host needs no code and does not know its guests: it only gives the rows guests may follow an id.

```c
static const struct rv_row rows[] = {
    RV_ROW_PUSH("Presets", "presets", .id = "presets"),
    RV_ROW_SETTING("Limiter", KEY_LIMITER, .id = "limiter"),
};
```

A guest names the host's manifest id, the screen id and the row id; its stock menu and anchor stay as the fallback:

```c
RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_EQ, RV_ROW_SETTING("Crossfeed", KEY_MODE),
              .pane = RV_PANE(.caption_text = caption, .bitmap = RV_BITMAP("icon")),
              .host = "equalizer", .host_screen = "equalizer", .host_after = "limiter"),
```

| While | The row is |
|---|---|
| the host runs and has the screen and row | in the host's list after that row; guests after the same row follow it in the order they loaded |
| the host is missing or failed to start, or the screen or row id names nothing | in the stock menu after `after` |
| `.host_after` is NULL | after the host list's last row, so a host that gives no row an id still takes guests at the end |
| `menus` is 0 (`RV_MENUS_ENTRY(0, 0, row, .host = ..., ...)`) | nowhere without the host |

The screen must be one of the host's main module. In both places the row behaves as a menu row: `.caption_setting` and `.shown_by` apply, and `RV_ROW_SETTING` shows its value inline. A host list declared with `RV_SCREEN_LIST_PANES` shows the guest's `.pane` caption and icon beside it like its own rows' (without the box for a guest with `.plain_icon` or no bitmap); a list with one pane of its own keeps that. On a list with guests `ctx->row` is the highlighted position, which counts the guests before it, in the host's context menu rows and in `api->ui_select`'s row.

The place is decided when the extensions start at boot: installing or removing the host takes effect after a restart. A guest with `.shown_by` appears and disappears in an open host list as the setting changes.

A hosted entry costs one row record and, once the host's list has been built, one row copy of 152 bytes; the host pays nothing.

## Moved stock rows

A list screen of the main module can take one of Apple's Settings rows: `RV_ROW_STOCK(RV_SETTINGS_SOUND_CHECK, .id = "sound-check")`. While the extension is installed the row is in that list and hidden in Settings; without the extension, after a restart, the row is back in Settings. Rows anchored on it in Settings keep their place: a hidden row still anchors. The row stays Apple's in both places: its title, its value and what choosing it does (Sound Check flips in place, EQ opens its screen, Volume Limit opens its screen or the EU question, and both return to the list). In a list declared with `RV_SCREEN_LIST_PANES` the row's highlight shows its Settings pane, through a clone of the row's own Settings layout. The `rv_pane` at its index is unused. The list needs an `id`.

One extension at a time may move a row; a second that names the same row fails to load. Apple shows and hides Volume Limit and Radio Regions itself as their state changes; the copy in the list follows.

A moved row costs its 152-byte row copy once the list has been built, the same as a declared row, Apple's bindings for it in the list's events and, once the split layout is built, the clone of its Settings layout.
