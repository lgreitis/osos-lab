/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Included through osos.h, after native bindings, integer types and osos_method. */

/* Display the prepared source layout without resetting its controls.
 * Songs/Genius, Playlists and Now Playing activation populate this cache (2.0.5).
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

/* Contextual menus as the native list menus build them: initialize, append rows,
 * show from the list controller, then destroy. */
struct osos_context_menu;

static inline void osos_context_menu_init(struct osos_context_menu *menu)
{
    typedef void (*fn)(struct osos_context_menu *, uint32_t);
    ((fn)OSOS_CONTEXT_MENU_INIT)(menu, 0);
}

static inline void osos_context_menu_show(void *controller,
                                          struct osos_context_menu *menu)
{
    typedef void (*fn)(void *, struct osos_context_menu *);
    ((fn)OSOS_CONTEXT_MENU_SHOW)(controller, menu);
}

static inline void osos_context_menu_destroy(struct osos_context_menu *menu)
{
    typedef void (*fn)(struct osos_context_menu *);
    ((fn)OSOS_CONTEXT_MENU_DESTROY)(menu);
}
