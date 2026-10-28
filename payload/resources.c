/* SPDX-License-Identifier: GPL-3.0-only */

#include "resources.h"
#include "osos.h"
#include "patch.h"
#include "ui.h"

PATCH_ARM void cfw_ui_append_resource_block(void *list, void *data, uint32_t size,
                                            uint32_t metadata)
{
    osos_resource_block_append(list, data, size, metadata);
    const uint32_t *selectors = list;
    if (selectors[2] != 0x4954454d || size < 0x34)
        return;
    /* Registration and live menus borrow the same mutable row payloads. */
    uint32_t item = ((const uint32_t *)data)[0x30 / 4];
    for (uint32_t i = 0; i < cfw_ui_menu_item_count; i++) {
        const struct cfw_ui_menu_item *entry = &cfw_ui_menu_items[i];
        if (entry->table == selectors[3] && entry->after == item)
            osos_resource_block_append(list, entry->data, entry->size, entry->metadata);
    }
}

const void *cfw_resource_bitmap(uint32_t id)
{
    for (uint32_t i = 0; i < cfw_resource_count; i++) {
        const struct cfw_resource *resource = &cfw_resources[i];
        if (resource->type == OSOS_RESOURCE_BMAP && resource->id == id)
            return resource->data;
    }
    return 0;
}

PATCH_ARM const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size)
{
    uint32_t start = 0;
    for (uint32_t i = 0; i < cfw_resource_count; i++) {
        if (cfw_resources[i].type == 0x56696577 && cfw_resources[i].id == *id) {
            start = i + 1;
            break;
        }
    }
    if (!start) {
        const void *data = osos_resource_next(bank, id, size);
        if (data)
            return data;
    }
    for (uint32_t i = start; i < cfw_resource_count; i++) {
        const struct cfw_resource *resource = &cfw_resources[i];
        if (resource->type == 0x56696577) { /* View */
            *id = resource->id;
            *size = resource->size;
            return resource->data;
        }
    }
    return 0;
}

void cfw_install_resources(void *bank)
{
    cfw_ui_register_controllers();
    cfw_ui_register_canvas();
    for (uint32_t i = 0; i < cfw_resource_name_count; i++) {
        struct osos_string name;
        osos_string_init(&name, cfw_resource_names[i].name);
        *osos_resource_name_slot(&name) = cfw_resource_names[i].id;
        osos_string_destroy(&name);
    }
    for (uint32_t i = 0; i < cfw_resource_count; i++) {
        const struct cfw_resource *resource = &cfw_resources[i];
        /* Settings bitmap properties borrow pixels from the permanent payload. */
        if (resource->type == OSOS_RESOURCE_BMAP)
            continue;
        osos_resource_set(bank, resource->type, resource->id, resource->data,
                          resource->size);
    }
}
