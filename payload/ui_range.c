/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "osos.h"
#include "patch.h"

static PATCH_ARM int range_event(void *controller, void *event)
{
    if (*(uint32_t *)((unsigned char *)event + 4) == 0x15) {
        uint32_t screen = *(uint32_t *)((unsigned char *)controller + 0xb4);
        for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
            const struct cfw_ui_field *field = cfw_ui_fields[i];
            if (field->screen != screen || !field->numeric)
                continue;

            osos_wheel_decode(event);
            int delta =
                osos_wheel_step(*(uint32_t *)((unsigned char *)event + 0xc), 0x10);
            int64_t value = (int64_t)field->model->values[field->slot] + delta;
            if (value < 0)
                value = 0;
            if (value >= field->count)
                value = field->count - 1;

            if ((uint32_t)value != field->model->values[field->slot])
                field->model->change(field->slot, (uint32_t)value);
            return 1;
        }
        return 1;
    }

    return osos_controller_handle_event(controller, event);
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
    [(12 + 0xf0) / 4] = (uintptr_t)range_deactivate,
};
PATCH_COPY(range_vtable, 0, OSOS_CONTROLLER_SCREEN_VTABLE - 12, 12 + 0x30);
PATCH_COPY(range_vtable, 12 + 0x34, OSOS_CONTROLLER_SCREEN_VTABLE + 0x34, 0xf0 - 0x34);
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
