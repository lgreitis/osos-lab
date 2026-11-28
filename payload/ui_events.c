/* SPDX-License-Identifier: GPL-3.0-only */
#include "rvx/runtime.h"
#include "osos.h"
#include "patch.h"

/* After Apple loaded a layout's events, the runtime adds the extensions'. */
PATCH_ARM void cfw_ui_load_events(void *layout)
{
    osos_ui_load_layout_events(layout);
    unsigned char *object = layout;
    uint32_t *screen = *(uint32_t **)(object + 0x58);
    cfw_rvx_menu_events(object + 0x38, screen ? screen[1] : 0,
                        *(uint32_t *)(object + 8));
}
