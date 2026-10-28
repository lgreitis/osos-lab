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
#define osos_ui_service_get service
#define osos_ui_selection get_selection
#define osos_wheel_decode wheel_decode
#define osos_controller_handle_event delegate_event
#define osos_controller_deactivate deactivate
#define osos_controller_create_screen create_screen
#define osos_controller_registry get_registry
#define osos_ui_register_controllers register_controllers

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
static const struct cfw_ui_action action = {30, 3, labels, &status, 1};
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

static uint32_t osos_selection_index(void *p)
{
    return *(uint32_t *)p;
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

static int wheel_primed, wheel_remainder, wheel_constructions;

static void *osos_wheel_accumulator_init(void *state, int threshold,
                                         unsigned char accelerate, uint32_t interval)
{
    assert(threshold == 5 && !accelerate && interval == 350);
    assert(++wheel_constructions == 1);
    return state;
}

static void osos_wheel_accumulator_reset(void *state)
{
    (void)state;
    wheel_primed = wheel_remainder = 0;
}

static int osos_wheel_accumulator_step(void *state, int motion, uint32_t speed)
{
    (void)state;
    assert(speed == 0);
    if (!wheel_primed) {
        wheel_primed = 1;
        return 0;
    }
    int total = wheel_remainder + motion;
    wheel_remainder = total % 5;
    return total / 5;
}

static uint32_t osos_button_ui_key(void *event)
{
    return (((uint32_t *)event)[4] >> 16) & 0xff;
}

static uint32_t osos_controller_activate(void *controller, uint32_t entering)
{
    assert(controller == controller_storage);
    return entering;
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
    assert(cfw_ui_property(OSOS_PROPERTY_COUNT, 30, &out) && out == 0);
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
    cfw_ui_status(&action, 0);
    assert(cfw_ui_property(OSOS_PROPERTY_COUNT, 30, &out) && out == 0);
    cfw_ui_register_controllers();
    void *controller = range_create("Range", 200, 201, 2);
    assert(*(const uintptr_t **)controller == range_vtable + 3);
    assert(cfw_ui_property(0x436e746c, 20, &out));
    assert(((int32_t *)out)[0] == 1 && ((int32_t *)out)[1] == 0 &&
           ((int32_t *)out)[2] == 2);
    assert(range_activate(controller, 1) == 1);
    uint32_t event[] = {0, 0x15, 0, 0, 0};
    range_event(controller, event);
    event[3] = 2;
    range_event(controller, event);
    range_event(controller, event);
    assert(values[1] == 1);
    event[3] = 1;
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
    event[1] = 6;
    event[4] = 0x5a << 16;
    range_event(controller, event);
    assert(values[1] == 0);
    event[1] = 0x0d;
    range_event(controller, event);
    assert(values[1] == 1);
    range_event(controller, event);
    assert(values[1] == 2);
    event[4] = 0x59 << 16;
    range_event(controller, event);
    assert(values[1] == 1);
    range_event(controller, event);
    assert(values[1] == 0 && commits == 1);
    event[1] = 2;
    assert(range_event(controller, event) == 7 && delegated == 1);
    assert(range_deactivate(controller, 1) == 1 && deactivated == 1 && commits == 2);
    controller = range_create("Range", 200, 201, 2);
    range_activate(controller, 1);
    assert(!wheel_primed && !wheel_remainder);
    assert(cfw_ui_property(0x436e746c, 20, &out));
    assert(((int32_t *)out)[0] == 0 && ((int32_t *)out)[1] == 0 &&
           ((int32_t *)out)[2] == 2);
    cfw_ui_refresh(&fields[0]);
    assert(notifications == 4);
    return 0;
}
