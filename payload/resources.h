/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_RESOURCES_H
#define CFW_RESOURCES_H
#include <stdint.h>

struct cfw_resource {
    uint32_t type, id, size;
    const unsigned char *data;
};

struct cfw_resource_name {
    const char *name;
    uint32_t id;
};

extern const struct cfw_resource cfw_resources[];
extern const uint32_t cfw_resource_count;
extern const struct cfw_resource_name cfw_resource_names[];
extern const uint32_t cfw_resource_name_count;

void cfw_install_resources(void *bank);
const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size);
#endif
