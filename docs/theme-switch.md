# Theme switching

Settings and boot use one [switcher](../payload/theme_switch.h), initialized with
stock artwork and called on the UI task. Switching is synchronous: check the pack
and baseline prefixes, save the selection, stream the pack into resident artwork,
restore what it omits, then apply colors. The caller refreshes cached playback
icons and invalidates the UI. Failures before saving leave the active theme
unchanged.

Nothing is staged in heap. Each record is read straight into its resident bitmap
and checked there: layout, palette indices, padding, and at the end tint coverage
and the pack checksum. Previous replacements the new pack omits are then restored
from `themes/classic/default.bin`. Records there are complete and sorted, so the
read stops after the last bitmap to restore, and each restored bitmap must match
its original checksum. See
[stock restoration data](../themes/AUTHORING.md#stock-restoration-data).

A failure after saving leaves artwork partly replaced. The switcher then restores
every bitmap the previous or the failed pack touched from the baseline, returns to
Classic light with the default accent, and saves that selection. A rejected record
is zeroed first, so no resident bitmap indexes past its palette. If the baseline
cannot restore everything, the affected bitmaps stay marked as replacements and
Settings hides theming for the session; the firmware reloads stock artwork at the
next boot.

## Input and Settings

Theme selection and Dark Mode share a UI-task latch. The first action latches
before loading; success or failure ends loading but leaves the latch set until
the native nonblocking dispatch reports an empty queue. Other messages retain
native dispatch behavior. This prevents queued presses from reversing a switch
without a fixed delay or renderer locks.

Theme opens a native half-screen Settings list with an image/text preview. The
CFW preview graphics are 8-bit masks listed after the firmware bitmaps in the
catalog, so theme packs replace and tint them like stock previews. The payload
stores their pixels run-packed behind one shared header and decodes the requested
preview into a single resident slot (`payload/catalog_bitmaps.h`). Their pack
records carry raw pixels like any other record. The switch reads each one into
the slot and run-packs it into a heap source when it differs from the built-in
art, freeing the previous theme's copy first; these copies are the only heap a
theme holds. A restoration selects the built-in source, so stock needs no
baseline bytes for them.

Classic comes first, followed by up to 64 packages ordered by ID. If the list is
full, the active package replaces the last entry. The active row is highlighted
on opening; there are no checkmarks. Discovery normalizes FAT name case and
retains only IDs and display names. Selection revalidates package metadata and
artwork, returning to the parent Theme row. Failures before saving keep the active
theme, and failures while writing artwork leave Classic light; both show an error
in the preview. Opening the list clears the error for retry.

Opening Settings hides Theme unless a custom package has valid metadata; the
scan stops at the first one. Only the Settings main screen checks, through its
`CFWSettingsMain` controller. The picker list is allocated when Theme opens and
freed with the Settings main controller, because the picker's view still renders
its rows during the pop transition. If allocation fails, the picker offers
Classic and an error row. Dark Mode changes the active theme's appearance and is
hidden for single-appearance packages. Selecting such a package uses its sole
appearance.

## Boot and installation

Install `themes/classic/default.bin` (complete stock baseline) and
`themes/classic/dark.bin` (Classic dark artwork). Boot reads only their prefixes:
magic, catalog fingerprint and appearance, the baseline's record count and body
size, and the Classic dark style. Records are checked when a switch applies or
restores them. Without both files the firmware stays in Classic light, hides both
settings, and leaves preferences unchanged. Restore the files and restart to enable
theming.
The `classic` directory is reserved and excluded from custom discovery.

Installer 0.2.9 or newer generates the complete baseline from verified Apple
firmware and uploads both files before the firmware. New bundles require that
installer version; older bundles retain their previous installation layout.
The firmware does not read the old `themes/default` and `themes/dark` paths.

Boot restores valid saved selections through the same switcher without rewriting
them. Invalid records or unavailable packages recover to Classic light. An absent
`CFWTheme` record selects Classic light without writing anything; the pre-theme
`CFWDark` record is ignored.

## Tint roles

Each catalog asset carries a tint role byte in the pack's style prefix: none, the
icon tint, or one of the base color roles. Accent palettes override roles as
they do everywhere else. Tinted mask bitmaps take their color when
`BitmapView_Draw` sets the drawing color (`cfw_theme_bitmap_color`): the hook
resolves the view's pixel data to a catalog asset (a firmware bitmap or the
payload's decode slot)
and consults the pack's tint roles. Tinted paletted and ARGB bitmaps are
recolored in place:
`cfw_theme_tint_refresh` rewrites the resident palette entries, or the RGB of
every ARGB pixel with nonzero alpha, after a switch and after an accent change,
so no pack is read again. Those assets are always replaced records, and the next
switch restores them through the ordinary record path.

A highlighted view draws tinted bitmaps in the `on_accent` role so icons stay
visible on a selection bar that matches the icon tint. Masks take the role in
place of the tint at the same color hook. Paletted and ARGB bitmaps are wrapped
at the blit calls of `BitmapView_Draw` and the list row icon drawer
(`cfw_theme_bitmap_blit_hook`): the asset's resident bytes are rewritten with
`on_accent` for that blit and restored to the tint afterwards.

Two native views label the OptionBar well and thumb slices from two colors
they hold, both system white, through their own `GraphicsContext_SetColor`
calls: the Search keyboard (`cfw_theme_search_key_color`, Black slices) and the
generic option bar such as the Clock's Add/Edit (`cfw_theme_option_bar_color`,
White or Black slices by its style byte; the Now Playing style keeps its own
colors). The highlighted label takes `on_accent`, matching the thumb slices,
which a theme may tint with the `selection` role. The other labels take
`text` or `background`, whichever differs more in brightness from the center
pixel of the resident well center slice for that style
(`cfw_theme_well_letter_color`), so a theme may paint the well light or dark.
Classic keeps the native colors.

The Search text box (`Search_Input_Text`) draws its typed text and caret through
the general text color hook. Under a custom style its dark text shades take
`text` or `background` by the same contrast rule against the resident
`System_Input_Field_Middle_Image` center pixel (`cfw_theme_field_text_color`),
and the stock blue caret takes `accent_text`. Classic keeps the stock light field
art, so the view stays on the native-color list there. Both rules read the center
pixel through `cfw_theme_asset_center_pixel`, which handles paletted and ARGB
assets.

Now Playing draws the cover with the firmware's GLES renderer into a 320 by
240 color buffer: first the tilted cover and its reflection, then antialiased
lines along the cover's slanted top and bottom edges. With `now_playing_art`
set to `flat`, `cfw_theme_album_art_draw` copies the cover's 128 by 128 pixels
into the buffer at (18, 38) instead, and `cfw_theme_album_art_edge_lines` skips
the edge lines. The pixels are the image the view would otherwise upload as a
texture; the draw hook fetches it from the view that
`cfw_theme_album_art_view_draw` records for the duration of each draw.

The hooks check the theme on every draw, so a theme switch shows up the next
time Now Playing draws. Cover Flow uses the same renderer through other calls
and is not affected.

## Preference storage

`iPod_Control/Device/CFWTheme` contains a 56-byte RTP1 record: little-endian magic,
version, pending flag, 32-byte padded theme ID, appearance, accent, and CRC32 of
the first 52 bytes. Decode validates length, checksum, version, flags, and
selection fields without modifying its output on failure. Pending records
select Classic light at boot.

Only native open error -43 means absent. Other errors, malformed records, and
trailing data are invalid and recover to Classic light. Writes check transfer length, explicitly
sync, set the length to 56 bytes, sync again, close, and verify by readback.
The native write wrapper discards its internal sync result, so the explicit
sync is required. Storage errors return UNKNOWN and may leave uncertain disk
contents; invalid input returns UNCHANGED without opening the file.
