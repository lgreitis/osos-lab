/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui.h"
#include "osos.h"

static int set_label(uint32_t table, unsigned int index, uint32_t source)
{
    uint32_t size;
    unsigned char *data =
        osos_resource_get(osos_resource_bank(), 0x4954454d, table, &size);
    /* Generated ITEM blocks have an eight-byte header and a 0x98-byte body. */
    unsigned int offset = 12 + index * 0xa0;
    if (!data || size < offset + 0x98)
        return 0;
    if (*(uint32_t *)(data + offset + 0x68) == source)
        return 0;
    *(uint32_t *)(data + offset + 0x68) = source;
    return 1;
}

void cfw_ui_refresh(const struct cfw_ui_field *field, unsigned int selected)
{
    if (selected >= field->count)
        return;
    set_label(field->parent_table, field->parent_index, field->summaries[selected]);
    for (unsigned int i = 0; i < field->count; i++)
        set_label(field->selector_table, i,
                  i == selected ? field->marked[i] : field->labels[i]);
    osos_menu_item_changed(field->parent_item);
}

void cfw_ui_status(const struct cfw_ui_action *action, unsigned int status)
{
    if (status >= action->count)
        return;
    set_label(action->table, action->index, action->labels[status]);
    osos_menu_item_changed(action->item);
}

int cfw_ui_selection(const char *prefix, const char *action,
                     const struct cfw_ui_field *fields, unsigned int count,
                     unsigned int *field, unsigned int *value)
{
    unsigned int i = 0;
    if (!action)
        return 0;
    while (prefix[i] && action[i] == prefix[i])
        i++;
    if (prefix[i])
        return 0;
    const char *digits = action + i;
    for (unsigned int n = 0; n < 5; n++) {
        if (n == 2) {
            if (digits[n] != '_')
                return 0;
        } else if (digits[n] < '0' || digits[n] > '9') {
            return 0;
        }
    }
    if (digits[5])
        return 0;
    *field = (digits[0] - '0') * 10 + digits[1] - '0';
    *value = (digits[3] - '0') * 10 + digits[4] - '0';
    return *field < count && *value < fields[*field].count;
}
