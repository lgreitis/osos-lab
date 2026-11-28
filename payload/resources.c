/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patch.h"
#include "rvx/runtime.h"

/* Registration and live menus borrow the same mutable row payloads. */
PATCH_ARM void cfw_ui_append_resource_block(void *list, void *data, uint32_t size,
                                            uint32_t metadata)
{
    osos_resource_block_append(list, data, size, metadata);
    const uint32_t *selectors = list;
    if (selectors[2] != 0x4954454d || size < 0x34)
        return;
    uint32_t item = ((const uint32_t *)data)[0x30 / 4];
    cfw_rvx_menu_row(list, selectors[3], item, data, size, metadata);
}
