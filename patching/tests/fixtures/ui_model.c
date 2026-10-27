/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../payload/ui.h"
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define PATCH_COPY(...)
#define OSOS_PROPERTY_STRING 0x53747220
#define OSOS_PROPERTY_COUNT 0x564d6178
#define OSOS_RESOURCE_BMAP 0x424d6170
#define OSOS_NOTIFY_ALL 0x2a2a2a2a
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#define OSOS_MENU_CHECKED_BITMAP 17
#define OSOS_UI_SERVICE_GET service
#define OSOS_UI_SELECTION get_selection
#define OSOS_WHEEL_DECODE wheel_decode
#define OSOS_WHEEL_STEP wheel_step
#define OSOS_CONTROLLER_HANDLE_EVENT delegate_event
#define OSOS_CONTROLLER_DEACTIVATE deactivate
#define OSOS_CONTROLLER_CREATE_SCREEN create_screen
#define OSOS_CONTROLLER_REGISTRY get_registry
#define OSOS_UI_REGISTER_CONTROLLERS register_controllers

static uint32_t values[] = {0, 1}, selected;
static unsigned changes, commits, notifications, delegated, deactivated;
static uintptr_t controller_storage[64];
static int model_object;

static void change(uint32_t field, uint32_t value)
{
    changes++;
    values[field] = value;
}

static void commit(void)
{
    commits++;
}
static const struct cfw_ui_model model = {values, change, commit};
static const char *const labels[] = {"Low", "Normal", "High"};
static char summary[14];
static struct cfw_ui_field_state states[] = {{.summary = summary},
                                             {.summary = summary}};
static const struct cfw_ui_field fields[] = {
    {10, 11, 100, 3, 0, 0, 0, "Choose", "Level", labels, &model, &states[0]},
    {20, 21, 200, 3, 1, 1, 1, "Adjust", "Level", labels, &model, &states[1]},
};
const struct cfw_ui_field *const cfw_ui_fields[] = {&fields[0], &fields[1]};
const uint32_t cfw_ui_field_count = 2;
static uint32_t status;
static const struct cfw_ui_action action = {30, 3, labels, &status};
const struct cfw_ui_action *const cfw_ui_actions[] = {&action};
const uint32_t cfw_ui_action_count = 1;

static void *service(uint32_t id)
{
    assert(id == 0x8900);
    return &model_object;
}

static void *get_selection(void)
{
    return &selected;
}

static uint32_t selected_index(void *p)
{
    return *(uint32_t *)p;
}

static uintptr_t osos_method(void *p, unsigned offset)
{
    assert(p == &selected && offset == 0x168);
    return (uintptr_t)selected_index;
}

static uintptr_t osos_ui_bitmap(uint32_t id)
{
    return id;
}

static void osos_model_notify(void *p, uint32_t type, uint32_t property)
{
    assert(p == &model_object && type == OSOS_NOTIFY_ALL && property);
    notifications++;
}

static void wheel_decode(void *event)
{
    (void)event;
}

static int wheel_step(uint32_t delta, uint32_t step)
{
    assert(step == 16);
    return (int32_t)delta;
}

static int delegate_event(void *controller, void *event)
{
    assert(controller == controller_storage && event);
    delegated++;
    return 7;
}

static uint32_t deactivate(void *controller, uint32_t leaving)
{
    assert(controller == controller_storage && leaving == 1 && commits);
    deactivated++;
    return 1;
}

static void *create_screen(const char *name, uint32_t screen, uint32_t layout,
                           uint32_t transition)
{
    assert(!strcmp(name, "Range") && layout == 201 && transition == 2);
    *(uint32_t *)((char *)controller_storage + 0xb4) = screen;
    return controller_storage;
}

static void *get_registry(void)
{
    return &model_object;
}

static void register_controllers(void *r, const char *name, void *ordinary,
                                 void *factory)
{
    assert(r == &model_object && !strcmp(name, "CFWRange") && !ordinary && factory);
}

#include "../../../payload/ui.c"
#include "../../../payload/ui_range.c"

int main(void)
{
    uintptr_t out = 0;
    assert(cfw_ui_property(OSOS_PROPERTY_COUNT, 10, &out) && out == 3);
    assert(cfw_ui_indexed_property(OSOS_PROPERTY_STRING, 10, 2, &out));
    assert(!strcmp((char *)out, "High"));
    assert(!cfw_ui_indexed_property(OSOS_PROPERTY_STRING, 10, 3, &out));
    assert(cfw_ui_property(OSOS_PROPERTY_STRING, 11, &out));
    assert(!strcmp((char *)out, "Level: Low"));
    assert(cfw_ui_handle_action("Choose", OSOS_ACTION_SUPPORT_QUERY));
    assert(!changes && !commits);
    selected = 2;
    assert(cfw_ui_handle_action("Choose", 0) && values[0] == 2 && changes == 1 &&
           commits == 1);
    selected = 3;
    assert(cfw_ui_handle_action("Choose", 0) && changes == 1);
    assert(!cfw_ui_handle_action("Missing", 0));
    assert(!cfw_ui_property(OSOS_PROPERTY_STRING, 999, &out));
    cfw_ui_status(&action, 2);
    assert(cfw_ui_property(OSOS_PROPERTY_STRING, 30, &out) &&
           !strcmp((char *)out, "High"));
    cfw_ui_status(&action, 3);
    assert(status == 2);
    assert(cfw_ui_property(OSOS_PROPERTY_COUNT, 30, &out) && out == 1);
    assert(cfw_ui_property(OSOS_PROPERTY_COUNT, 11, &out) && out == 1);
    assert(cfw_ui_indexed_property(OSOS_PROPERTY_STRING, 30, 0, &out));
    assert(!strcmp((char *)out, "High"));
    assert(!cfw_ui_indexed_property(OSOS_PROPERTY_STRING, 30, 1, &out));
    cfw_ui_register_controllers();
    void *controller = range_create("Range", 200, 201, 2);
    assert(*(const uintptr_t **)controller == range_vtable + 3);
    assert(cfw_ui_property(0x436e746c, 20, &out));
    assert(((int32_t *)out)[0] == 1 && ((int32_t *)out)[1] == 0 &&
           ((int32_t *)out)[2] == 2);
    uint32_t event[] = {0, 0x15, 0, 100};
    assert(range_event(controller, event) == 1 && values[1] == 2);
    assert(commits == 1); /* Wheel previews do not write preferences. */
    assert(cfw_ui_property(0x436e746c, 20, &out));
    assert(((int32_t *)out)[0] == 2 && ((int32_t *)out)[1] == 0 &&
           ((int32_t *)out)[2] == 2);
    event[3] = (uint32_t)-100;
    range_event(controller, event);
    assert(values[1] == 0);
    assert(cfw_ui_property(0x436e746c, 20, &out) && *(uint32_t *)out == 0);
    unsigned before = changes;
    range_event(controller, event);
    assert(changes == before);
    event[1] = 2;
    assert(range_event(controller, event) == 7 && delegated == 1);
    assert(range_deactivate(controller, 1) == 1 && deactivated == 1 && commits == 2);
    controller = range_create("Range", 200, 201, 2);
    assert(cfw_ui_property(0x436e746c, 20, &out));
    assert(((int32_t *)out)[0] == 0 && ((int32_t *)out)[1] == 0 &&
           ((int32_t *)out)[2] == 2);
    cfw_ui_refresh(&fields[0]);
    assert(notifications == 3);
    return 0;
}
