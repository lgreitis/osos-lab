#include "resources.h"
#include "osos.h"

const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size)
{
    const struct cfw_resource *body = 0;

    for (uint32_t i = 0; i < cfw_resource_count; i++) {
        if (cfw_resources[i].type == 0x56696577) { /* View */
            body = &cfw_resources[i];
            break;
        }
    }
    if (body && *id == body->id)
        return 0;

    const void *data = osos_resource_next(bank, id, size);
    if (data || !body)
        return data;

    /* Let Apple's template initializer create the CFW info body after stock views. */
    *id = body->id;
    *size = body->size;
    return body->data;
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
