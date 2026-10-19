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
struct cfw_translation {
    uint32_t id, language;
    const unsigned char *data;
};

struct cfw_format_spec {
    uint32_t id;
    const char *types;
};

extern const struct cfw_resource cfw_resources[];
extern const uint32_t cfw_resource_count;
extern const struct cfw_resource_name cfw_resource_names[];
extern const uint32_t cfw_resource_name_count;
/* Sorted by resource ID, then language, for binary lookup. */
extern const struct cfw_translation cfw_translations[];
extern const uint32_t cfw_translation_count;
extern const struct cfw_format_spec cfw_formats[];
extern const uint32_t cfw_format_count;

const void *cfw_translated_string(uint32_t id);
const char *cfw_string(uint32_t id);
unsigned cfw_format(char *out, unsigned capacity, uint32_t id, ...);
void cfw_install_resources(void *bank);
const void *cfw_resource_bitmap(uint32_t id);
const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size);
#endif
