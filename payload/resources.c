/* SPDX-License-Identifier: GPL-3.0-only */

#include "resources.h"
#include "osos.h"
#include "patch.h"
#include "ui.h"
#include <stdarg.h>

const void *cfw_resource_service_get_original(void *service, uint32_t type, uint32_t id);

PATCH_ARM const void *cfw_resource_service_get(void *service, uint32_t type, uint32_t id)
{
    if (type == 0x53747220) {
        const void *translated = cfw_translated_string(id);
        if (translated)
            return translated;
    }
    return cfw_resource_service_get_original(service, type, id);
}

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
const void *cfw_translated_string(uint32_t id)
{
    int language = -1;
    uint32_t first = 0, end = cfw_translation_count;
    while (first < end) {
        uint32_t middle = first + (end - first) / 2;
        const struct cfw_translation *translation = &cfw_translations[middle];
        if (translation->id < id) {
            first = middle + 1;
        } else if (translation->id > id) {
            end = middle;
        } else {
            if (language < 0)
                language = osos_ui_language();
            if (language < 0)
                return 0;
            if (translation->language < (uint32_t)language)
                first = middle + 1;
            else if (translation->language > (uint32_t)language)
                end = middle;
            else
                return translation->data;
        }
    }
    return 0;
}

const char *cfw_string(uint32_t id)
{
    const char *translated = cfw_translated_string(id);
    if (translated)
        return translated;
    for (uint32_t index = 0; index < cfw_resource_count; index++) {
        if (cfw_resources[index].type == 0x53747220 && cfw_resources[index].id == id)
            return (const char *)cfw_resources[index].data;
    }
    return "";
}

struct format_buffer {
    char *data;
    unsigned capacity, size;
    int full;
};

static void format_append(struct format_buffer *buffer, const char *text)
{
    if (!text)
        return;
    while (*text && !buffer->full) {
        unsigned bytes = 1;
        unsigned char first = (unsigned char)*text;
        if (first >= 0xc2 && first <= 0xf4) {
            unsigned width = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            for (unsigned index = 1; index < width; index++) {
                if (((unsigned char)text[index] & 0xc0) != 0x80)
                    break;
                bytes++;
            }
        }
        if (bytes >= buffer->capacity - buffer->size) {
            buffer->full = 1;
            break;
        }
        for (unsigned index = 0; index < bytes; index++)
            buffer->data[buffer->size++] = *text++;
    }
    buffer->data[buffer->size] = 0;
}

unsigned cfw_format(char *out, unsigned capacity, uint32_t id, ...)
{
    if (!capacity)
        return 0;
    out[0] = 0;
    const char *types = 0;
    for (uint32_t index = 0; index < cfw_format_count; index++) {
        if (cfw_formats[index].id == id) {
            types = cfw_formats[index].types;
            break;
        }
    }
    if (!types)
        return 0;
    union { const char *text; uint32_t number; } arguments[8];
    unsigned count = 0;
    va_list values;
    va_start(values, id);
    while (types[count] && count < 8) {
        if (types[count] == 's')
            arguments[count].text = va_arg(values, const char *);
        else
            arguments[count].number = va_arg(values, uint32_t);
        count++;
    }
    va_end(values);
    struct format_buffer buffer = {out, capacity, 0, 0};
    const char *cursor = cfw_string(id);
    unsigned sequential = 0;
    while (*cursor && !buffer.full) {
        if (*cursor != '%') {
            char literal[5] = {0};
            unsigned bytes = 0;
            do {
                literal[bytes++] = *cursor++;
            } while (((unsigned char)*cursor & 0xc0) == 0x80 && bytes < 4);
            format_append(&buffer, literal);
            continue;
        }
        cursor++;
        if (*cursor == '%') {
            cursor++;
            format_append(&buffer, "%");
            continue;
        }
        unsigned argument = sequential++;
        if (*cursor >= '1' && *cursor <= '8' && cursor[1] == '$') {
            argument = *cursor - '1';
            cursor += 2;
        }
        if (argument >= count || *cursor != types[argument]) {
            out[0] = 0;
            return 0;
        }
        if (*cursor++ == 's') {
            format_append(&buffer, arguments[argument].text);
        } else {
            char digits[11];
            unsigned start = sizeof(digits) - 1;
            uint32_t number = arguments[argument].number;
            digits[start] = 0;
            do {
                digits[--start] = '0' + number % 10;
                number /= 10;
            } while (number);
            format_append(&buffer, digits + start);
        }
    }
    return buffer.size;
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
