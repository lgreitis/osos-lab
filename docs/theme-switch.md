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

## Tests

Run `python3 -m unittest patching.tests.test_payload` for sanitizer coverage of
synchronous switching, repeat input, sparse restoration, and failure handling.
