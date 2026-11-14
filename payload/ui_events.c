/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "osos.h"
#include "patch.h"

static uint32_t original_events(uint32_t type, uint32_t id)
{
    for (uint32_t i = 0; i < cfw_ui_event_alias_count; i++)
        if (cfw_ui_event_aliases[i].type == type && cfw_ui_event_aliases[i].id == id)
            return cfw_ui_event_aliases[i].original;
    return id;
}

PATCH_ARM void *cfw_ui_event_resource(void *bank, uint32_t type, uint32_t id,
                                      uint32_t *size)
{
    return osos_resource_get(bank, type, original_events(type, id), size);
}

static void append_events(void *map, uint32_t type, uint32_t id)
{
    uint32_t original = original_events(type, id);
    if (original != id)
        append_events(map, type, original);
    for (uint32_t i = 0; i < cfw_ui_event_count; i++) {
        const struct cfw_ui_events *events = &cfw_ui_events[i];
        if (events->type != type || events->id != id)
            continue;
        struct osos_string bytes;
        uint32_t allocator = 0;
        uint32_t stream[31];
        osos_string_init_bytes(&bytes, events->data, events->size, &allocator);
        osos_string_stream_init(stream, &bytes, 0xc);
        osos_string_destroy(&bytes);
        osos_ui_event_map_read(stream, map);
        osos_string_stream_destroy(stream, 2, 0);
    }
}

PATCH_ARM void cfw_ui_load_events(void *layout)
{
    osos_ui_load_layout_events(layout);
    unsigned char *object = layout;
    void *map = object + 0x38;
    uint32_t **instance = *(uint32_t ***)(object + 0x10);
    uint32_t **end = *(uint32_t ***)(object + 0x14);
    for (; instance != end; instance++)
        append_events(map, 0x54455654, (*instance)[2]);
    append_events(map, 0x53455654, *(uint32_t *)(object + 8));
    uint32_t *screen = *(uint32_t **)(object + 0x58);
    if (screen)
        append_events(map, 0x43455654, screen[1]);
}
