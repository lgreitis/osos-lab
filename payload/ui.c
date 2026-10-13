/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui.h"
#include "osos.h"
#include "cfw.h"

int cfw_ui_list_count(const struct cfw_ui_list *list, unsigned int count)
{
    if (count > list->capacity)
        return 0;

    uint32_t size;
    const uint32_t *current =
        osos_resource_get(osos_resource_bank(), 0x4954454d, list->table, &size);
    if (current && size >= 4 && *current == count)
        return 1;

    /* ITEM rows are fixed 0xa0-byte blocks; omit unused rows from the table. */
    size = 4 + count * 0xa0;
    unsigned char *data = ((void *(*)(uint32_t))OSOS_IMPORT_ALLOC)(size);
    if (!data)
        return 0;

    cfw_copy_bytes(data, list->template, size);
    *(uint32_t *)data = count;
    osos_resource_set(osos_resource_bank(), 0x4954454d, list->table, data, size);
    ((void (*)(void *))OSOS_IMPORT_FREE)(data);
    return 1;
}

void cfw_ui_list_label(const struct cfw_ui_list *list, unsigned int index,
                       const char *label)
{
    if (index < list->capacity)
        cfw_ui_action_label(&list->rows[index], label);
}

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

void cfw_ui_action_label(const struct cfw_ui_action *action, const char *label)
{
    uint32_t size;
    const uint32_t *source =
        osos_resource_get(osos_resource_bank(), 0x534f5243, action->labels[0], &size);
    if (!source || size != 24 || source[0] != 1 || source[3] != 0x80)
        return;

    uint32_t length = 0;
    while (label[length])
        length++;

    osos_resource_set(osos_resource_bank(), 0x53747220, source[4], label, length + 1);
    cfw_ui_status(action, 0);
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
