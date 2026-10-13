/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#include "../../../payload/ui.h"

static unsigned char item_data[4 + 3 * 0xa0];
static uint32_t item_size = 4;
static uint32_t source[6] = {1, 12, 0x534f5243, 0x80, 101, 1};
static char label_data[64];
static int fail_alloc, allocations, writes, notifications;

static void *bank(void)
{
    return item_data;
}

static void *get(void *owner, uint32_t type, uint32_t id, uint32_t *size)
{
    assert(owner == item_data);
    if (type == 0x4954454d) {
        assert(id == 1);
        *size = item_size;
        return item_data;
    }
    assert(type == 0x534f5243 && id == 100);
    *size = sizeof(source);
    return source;
}

static void set(void *owner, uint32_t type, uint32_t id, const void *data,
                uint32_t size)
{
    assert(owner == item_data);
    if (type == 0x4954454d) {
        assert(id == 1 && size <= sizeof(item_data));
        memcpy(item_data, data, size);
        item_size = size;
        writes++;
    } else {
        assert(type == 0x53747220 && id == 101 && size <= sizeof(label_data));
        memcpy(label_data, data, size);
    }
}

static void changed(uint32_t item)
{
    assert(item == 10);
    notifications++;
}

static void *allocate(uint32_t size)
{
    if (fail_alloc)
        return NULL;
    allocations++;
    return malloc(size);
}

static void release(void *data)
{
    allocations--;
    free(data);
}

#define osos_resource_bank bank
#define osos_resource_get get
#define osos_resource_set set
#define osos_menu_item_changed changed
#undef OSOS_IMPORT_ALLOC
#undef OSOS_IMPORT_FREE
#define OSOS_IMPORT_ALLOC allocate
#define OSOS_IMPORT_FREE release
#include "../../../payload/ui.c"

int main(void)
{
    unsigned char template[sizeof(item_data)] = {0};
    *(uint32_t *)template = 3;
    for (unsigned i = 0; i < 3; i++) {
        *(uint32_t *)(template + 4 + i * 0xa0) = 0x98;
        *(uint32_t *)(template + 12 + i * 0xa0 + 0x30) = 10 + i;
        *(uint32_t *)(template + 12 + i * 0xa0 + 0x68) = 100;
    }
    const uint32_t labels[] = {100};
    const struct cfw_ui_action rows[] = {{1, 0, 10, 1, labels}};
    const struct cfw_ui_list list = {1, 3, template, rows};

    assert(cfw_ui_list_count(&list, 1));
    assert(item_size == 164 && *(uint32_t *)item_data == 1 && !allocations);
    cfw_ui_list_label(&list, 0, "Default *");
    assert(!strcmp(label_data, "Default *") && notifications == 1);
    assert(cfw_ui_list_count(&list, 3));
    assert(item_size == sizeof(item_data) &&
           !memcmp(item_data, template, sizeof(template)));

    assert(cfw_ui_list_count(&list, 1));
    assert(item_size == 164 && *(uint32_t *)item_data == 1);

    int previous = writes;
    assert(cfw_ui_list_count(&list, 1) && writes == previous);
    assert(!cfw_ui_list_count(&list, 4) && writes == previous);

    fail_alloc = 1;
    assert(!cfw_ui_list_count(&list, 3));
    assert(item_size == 164 && *(uint32_t *)item_data == 1 && !allocations);

    fail_alloc = 0;
    assert(cfw_ui_list_count(&list, 0));
    assert(item_size == 4 && *(uint32_t *)item_data == 0);
}
