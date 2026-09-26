/* SPDX-License-Identifier: GPL-3.0-only */

#include "resources.h"
#include "osos.h"
#include "patch.h"

PATCH_CALL(0x081C7410, OSOS_RESOURCE_BANK_INIT_OVERRIDES, cfw_resource_hook);
PATCH_CALL(0x081AEDD8, 0x08111E0C, cfw_next_template);

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
    for (uint32_t i = 0; i < cfw_resource_name_count; i++) {
        struct osos_string name;
        osos_string_init(&name, cfw_resource_names[i].name);
        *osos_resource_name_slot(&name) = cfw_resource_names[i].id;
        osos_string_destroy(&name);
    }
    for (uint32_t i = 0; i < cfw_resource_count; i++) {
        const struct cfw_resource *resource = &cfw_resources[i];
        osos_resource_set(bank, resource->type, resource->id, resource->data,
                          resource->size);
    }
}
