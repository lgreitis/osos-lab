/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Included through osos.h, after native bindings, integer types and osos_method. */

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
