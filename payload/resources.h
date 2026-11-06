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

struct cfw_ui_menu_item {
    uint32_t table, after, size, metadata;
    unsigned char *data;
};

extern const struct cfw_ui_menu_item cfw_ui_menu_items[];
extern const uint32_t cfw_ui_menu_item_count;

/* BMap entries point at resident payload data: embedded bitmaps, or the catalog
 * bitmap block (cfw_catalog_bitmaps in assets.h) that themes replace in place. */
extern const struct cfw_resource cfw_resources[];
extern const uint32_t cfw_resource_count;
extern const struct cfw_resource_name cfw_resource_names[];
extern const uint32_t cfw_resource_name_count;

void cfw_install_resources(void *bank);
/* Returns the resident bytes of a CFW bitmap resource, or NULL. */
const void *cfw_resource_bitmap(uint32_t id);
const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size);
#endif
