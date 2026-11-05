# Theme switching

Settings and boot use one [switcher](../payload/theme_switch.h), initialized with
stock artwork and called on the UI task. Switching is synchronous: validate and
prepare artwork, save the selection, apply artwork and colors, then release the
buffers. The caller refreshes cached playback icons and invalidates the UI.
Preparation and storage failures leave the active theme unchanged.

The transaction owns the incoming pack and a separate buffer for previous
replacements omitted by the next theme. It streams those restorations from
`themes/classic/default.bin` with a 256-byte scratch buffer. Incoming artwork is
not copied into a second staging buffer; neither pack remains resident after
switching. See [stock restoration data](../themes/AUTHORING.md#stock-restoration-data).

## Input and Settings

Theme selection and Dark Mode share a UI-task latch. The first action latches
before loading; success or failure ends loading but leaves the latch set until
the native nonblocking dispatch reports an empty queue. Other messages retain
native dispatch behavior. This prevents queued presses from reversing a switch
without a fixed delay or renderer locks.

Theme opens a native half-screen Settings list with an image/text preview. The
CFW preview graphics are stored as zero-run encoded grayscale masks and unpacked
on demand into one shared buffer allocated on first use, so each additional preview
costs only its packed size.

Classic comes first, followed by up to 64 packages ordered by ID. If the list is
full, the active package replaces the last entry. The active row is highlighted
on opening; there are no checkmarks. Discovery normalizes FAT name case and
retains only IDs and display names. Selection revalidates package metadata and
artwork, returning to the parent Theme row. Failures keep the active theme and
show an error in its preview; opening the list clears the error for retry.

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
`themes/classic/dark.bin` (Classic dark artwork). Both are streamed and validated at
boot without loading them into memory; palette indices are checked when a pack is
read for switching. Otherwise the firmware stays in Classic light, hides both
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
