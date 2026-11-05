# Theme switching

The [theme switcher](../payload/theme_switch.h) switches synchronously,
using the same ordering as Dark Mode: validate and prepare the artwork, save the
selection, apply artwork and colors, then release temporary storage. The caller
refreshes cached icons and invalidates the UI after success.

Preparation restores previous replacements omitted by the next theme from the
stock baseline. All allocation and validation happen before saving or changing
live artwork. Failed preparation or storage leaves the active theme unchanged.
A storage error can leave uncertain disk contents; startup validates the saved
record. Temporary staging is released before returning to the caller.

One UI-task owner maintains the switcher and replacement set. Initialize with
stock artwork and do not combine it with the legacy assets writer. This follows
the existing UI-action mutation model; it does not establish hardware display
completion or add renderer locks.

## Repeated input

Theme selection and Dark Mode share a UI-task latch. The first action latches
before loading. Completion or failure ends loading but leaves the latch set.
The native UI loop drains queued messages normally; additional theme actions
are ignored until its nonblocking message dispatch reports an empty queue.
Other actions and notifications retain native dispatch behavior. This prevents
queued presses from reversing the selection when synchronous loading returns,
without a fixed delay or LCD completion signal.

## Boot and storage

Only an absent CFWTheme record permits CFWDark migration. Invalid records and
incomplete preference records select Classic light. A valid saved selection is a
candidate to load, not proof that its artwork is active.

Run `python3 -m unittest patching.tests.test_payload` for sanitizer coverage of
switching, repeated presses, storage and allocation failures, boot selection,
sparse restoration, and repeated custom/Classic switches.

## Preference storage

`theme_preferences.c` stores preferences at `iPod_Control/Device/CFWTheme`.
The 56-byte RTP1 record uses little-endian fields: magic, version, pending flag,
32-byte padded theme ID, appearance, accent, then CRC32 of the first 52 bytes.
Decode validates length, checksum, version, flags, and the switcher's selection
rules. It leaves the output unchanged on failure. A pending record is
syntactically valid; `cfw_theme_boot_selection` decides recovery.

Only native open error -43 means absent. Other open/read errors and malformed or
trailing data are invalid, so they cannot trigger legacy migration. Writes check
transfer length, sync buffered data, set the file length to 56 bytes, sync again,
close, and verify the entire record by readback.
Any storage failure returns UNKNOWN; invalid input returns UNCHANGED without
opening the file. Setting the exact length lets recovery replace oversized or
unsupported records without leaving trailing data.

`osos_file_sync` returns the native buffer/backend sync result. The write wrapper
discards its internal sync result, so preference storage calls sync explicitly.
The storage success criterion is a successful write, sync, and matching readback.
