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

An entry's pane, `.pane = RV_PANE(...)`, shows `.bitmap`, a picture from the extension's [assets](assets.md), with `.caption`; in shapes that have them `.extra = RV_PANE_EXTRA(...)` adds two `.lines` and a 20 x 16 `.badge`. `.caption_text`, `.line_text(ctx, line)`, `.bitmap_for` and `.badge_for` compute them when the row is highlighted and on `api->ui_pane_refresh(self)` while a pane still shows the row, for example from a timer. A caption that only names a setting's value is the entry's `.caption_setting = key` instead: the setting's label, On or Off for a bool or an unlabelled int, which follows the setting when it changes. The picture's size picks the pane's shape:

| Picture | Shows | Where |
|---|---|---|
| 78 x 121 mask, `pane = "icon"` | the label, the caption below and the icon on the system's box (or without it, with the entry's `.plain_icon`) | Settings only; every Settings row |
| 119 x 119 | the picture with the caption beneath | Main, Music, Videos, Extras; the pane of a row without a picture on Main, Music and Videos that has another pane field, or on Main no `.preview` |
| 142 x 87 | the picture with the caption beneath | the same; the pane of a row without a picture under Extras |
| 130 x 128 | the label as a title line at the top, the two lines over the picture, the caption beneath | Main, Music, Videos, Extras |
| 146 x 110 | the picture with the caption as the short count line beneath it, about 90 pixels wide, and the badge before it | Main, Music, Videos, Extras |
| 132 x 145 | the picture in the clock's box over the World Clock pane's world map, the caption on the date line | Music, Videos, Extras |

In the 119 x 119, 142 x 87 and 146 x 110 shapes a missing caption falls back to the label; the others show the label on a title line. A picture of a size the menu has no pane for, such as an icon outside Settings, or one an [asset provider](assets.md#asset-provider) serves at a different size, shows the caption alone. Panes are built the first time they are needed, so a newly enabled extension gets them at once. An Extras item mirrored onto Main uses the same picture in both.
