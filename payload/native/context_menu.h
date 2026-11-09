/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Included through osos.h, after native bindings, integer types and osos_method. */

/* Display the prepared source layout without resetting its controls.
 * Songs/Genius, Playlists, Episodes and Now Playing populate this cache (2.0.5).
 * The native accessor can insert on a miss, so the layout must already be cached. */
static inline void osos_controller_restore_layout(void *controller,
                                                  unsigned layout_offset)
{
    typedef void *(*layout_fn)(void *, uint32_t);
    typedef void *(*manager_fn)(void);
    typedef void (*display_fn)(void *, void *);
    if (osos_method(controller, 0x120) == OSOS_CONTROLLER_SWITCH_LAYOUT_DEFAULT)
        return;
    uint8_t *stored = (uint8_t *)controller + layout_offset;
    void *layout = ((layout_fn)OSOS_CONTROLLER_CACHED_LAYOUT)(
        stored - OSOS_LAYOUT_CACHE_TO_ID, *(uint32_t *)stored);
    if (layout)
        ((display_fn)OSOS_UI_DISPLAY_LAYOUT)(((manager_fn)OSOS_UI_LAYOUT_MANAGER)(),
                                             layout);
}
