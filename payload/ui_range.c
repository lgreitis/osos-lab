/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "osos.h"
#include "patch.h"

/* Native input settings retain the accumulator address for the UI lifetime. */
static uint32_t range_wheel[0x34 / 4];
static int range_wheel_initialized;

static PATCH_ARM uint32_t range_activate(void *controller, uint32_t entering)
{
    if (!range_wheel_initialized) {
        osos_wheel_accumulator_init(range_wheel, 5, 0, 350);
        range_wheel_initialized = 1;
    }
    osos_wheel_accumulator_reset(range_wheel);
    return osos_controller_activate(controller, entering);
}

static PATCH_ARM int range_event(void *controller, void *event)
{
    const uint32_t *words = event;
    int delta;
    if (words[1] == 0x15) {
        osos_wheel_decode(event);
        delta = osos_wheel_accumulator_step(range_wheel, (int32_t)words[3], 0);
    } else if (words[1] == 6 || words[1] == 7 || words[1] == 8 || words[1] == 0x0d) {
        uint32_t key = osos_button_ui_key(event);
        if (key != 0x59 && key != 0x5a)
            return osos_controller_handle_event(controller, event);
        if (words[1] != 0x0d)
            return 1;
        delta = key == 0x59 ? -1 : 1;
        osos_wheel_accumulator_reset(range_wheel);
    } else {
        return osos_controller_handle_event(controller, event);
    }
    uint32_t screen = *(uint32_t *)((unsigned char *)controller + 0xb4);
    for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
        const struct cfw_ui_field *field = cfw_ui_fields[i];
        if (field->screen != screen || !field->numeric)
            continue;
        int64_t value = (int64_t)field->model->values[field->slot] + delta;
        if (value < 0)
            value = 0;
        if (value >= field->count)
            value = field->count - 1;
        if ((uint32_t)value != field->model->values[field->slot])
            field->model->change(field->slot, (uint32_t)value);
        break;
    }
    return 1;
}

static PATCH_ARM uint32_t range_deactivate(void *controller, uint32_t leaving)
{
    uint32_t screen = *(uint32_t *)((unsigned char *)controller + 0xb4);
    for (uint32_t i = 0; i < cfw_ui_field_count; i++)
        if (cfw_ui_fields[i]->screen == screen)
            cfw_ui_fields[i]->model->commit();

    return osos_controller_deactivate(controller, leaving);
}

/* Preserve the native RTTI prefix, resource-screen lifecycle and deleting destructor. */
static const uintptr_t range_vtable[(12 + 0x128) / 4] = {
    [(12 + 0x30) / 4] = (uintptr_t)range_event,
    [(12 + 0xec) / 4] = (uintptr_t)range_activate,
    [(12 + 0xf0) / 4] = (uintptr_t)range_deactivate,
};
PATCH_COPY(range_vtable, 0, OSOS_CONTROLLER_SCREEN_VTABLE - 12, 12 + 0x30);
PATCH_COPY(range_vtable, 12 + 0x34, OSOS_CONTROLLER_SCREEN_VTABLE + 0x34, 0xec - 0x34);
PATCH_COPY(range_vtable, 12 + 0xf4, OSOS_CONTROLLER_SCREEN_VTABLE + 0xf4, 0x128 - 0xf4);

static PATCH_ARM void *range_create(const char *name, uint32_t screen, uint32_t layout,
                                    uint32_t transition)
{
    unsigned char *controller =
        osos_controller_create_screen(name, screen, layout, transition);
    *(const uintptr_t **)controller = range_vtable + 3;
    return controller;
}

void cfw_ui_register_controllers(void)
{
    void *registry = osos_controller_registry();
    osos_ui_register_controllers(registry, "CFWRange", 0, range_create);
}
