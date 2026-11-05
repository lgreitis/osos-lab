/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_input.h"
#include "native/bindings.h"
#include "patch.h"

/* The UI loop repeats this nonblocking dispatch until the queue is empty.
 * Preserve normal message handling; only theme actions consult the latch. */
PATCH_ARM int cfw_theme_input_dispatch(uint32_t wait, void *quit)
{
    typedef int (*dispatch_fn)(uint32_t, void *);
    int handled = ((dispatch_fn)OSOS_UI_PROCESS_MESSAGE)(wait, quit);
    if (!handled)
        cfw_theme_input_drained();
    return handled;
}
