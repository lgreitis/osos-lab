/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_RESOURCES_H
#define CFW_RESOURCES_H
#include <stdint.h>

struct cfw_resource {
    uint32_t type, id, size;
    const unsigned char *data;
};

/* Grayscale preview bitmaps are stored as zero runs: pairs of (zero count,
 * literal count) bytes followed by the literals, until size bytes are produced.
 * They are unpacked on demand into one shared buffer of cfw_packed_bitmap_bytes. */
struct cfw_packed_bitmap {
    uint32_t id, packed_size, size;
    const unsigned char *packed;
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

extern const struct cfw_resource cfw_resources[];
extern const uint32_t cfw_resource_count;
extern const struct cfw_packed_bitmap cfw_packed_bitmaps[];
extern const uint32_t cfw_packed_bitmap_count, cfw_packed_bitmap_bytes;
extern const struct cfw_resource_name cfw_resource_names[];
extern const uint32_t cfw_resource_name_count;

void cfw_install_resources(void *bank);
/* Returns payload or unpacked pixels for a CFW bitmap, or NULL. Unpacked bitmaps
 * share one buffer, so the result is valid until the next different bitmap. */
const void *cfw_resource_bitmap(uint32_t id);
const void *cfw_next_template(void *bank, uint32_t *id, uint32_t *size);
#endif
