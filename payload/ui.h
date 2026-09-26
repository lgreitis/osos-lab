/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_UI_H
#define CFW_UI_H
#include <stdint.h>

struct cfw_ui_field {
    uint32_t parent_table, parent_index, parent_item, selector_table, count,
        default_value;
    const uint32_t *labels, *marked, *summaries, *items;
};

struct cfw_ui_action {
    uint32_t table, index, item, count;
    const uint32_t *labels;
};

void cfw_ui_refresh(const struct cfw_ui_field *field, unsigned int selected);
void cfw_ui_status(const struct cfw_ui_action *action, unsigned int status);
int cfw_ui_selection(const char *prefix, const char *action,
                     const struct cfw_ui_field *fields, unsigned int count,
                     unsigned int *field, unsigned int *value);
#endif
