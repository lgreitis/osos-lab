/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
#define CFW_PATCH_H
#define PATCH_ARM
static int native_dispatch(uint32_t wait, void *quit);
#undef OSOS_UI_PROCESS_MESSAGE
#define OSOS_UI_PROCESS_MESSAGE ((uintptr_t)native_dispatch)
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_input_hook.c"

static unsigned remaining, accepted, other_actions;
static int quit_flag;

static int native_dispatch(uint32_t wait, void *quit)
{
    assert(!wait && quit == &quit_flag);
    if (!remaining)
        return 0;
    remaining--;
    other_actions++;
    if (cfw_theme_input_begin()) {
        accepted++;
        /* Nested dispatch seeing an empty queue cannot unlock active loading. */
        unsigned saved = remaining;
        remaining = 0;
        assert(!cfw_theme_input_dispatch(0, quit));
        assert(!cfw_theme_input_begin());
        remaining = saved;
        cfw_theme_input_finish();
    }
    return 1;
}

int main(void)
{
    remaining = 8;
    while (cfw_theme_input_dispatch(0, &quit_flag)) {
    }
    assert(accepted == 1 && other_actions == 8);
    remaining = 1; /* a new press after the queued burst is accepted */
    while (cfw_theme_input_dispatch(0, &quit_flag)) {
    }
    assert(accepted == 2 && other_actions == 9);
    return 0;
}
