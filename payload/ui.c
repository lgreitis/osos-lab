/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "osos.h"
#include "cfw.h"

static void notify(uint32_t property)
{
    void *model = osos_ui_service_get(0x8900);
    if (model)
        osos_model_notify(model, OSOS_NOTIFY_ALL, property);
}

void cfw_ui_refresh(const struct cfw_ui_field *field)
{
    notify(field->property);
    notify(field->summary_property);
}

void cfw_ui_status(const struct cfw_ui_action *action, unsigned int status)
{
    if (status >= action->count)
        return;
    *action->status = status;
    notify(action->property);
}

int cfw_ui_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    for (uint32_t i = 0; i < cfw_ui_action_count; i++) {
        const struct cfw_ui_action *action = cfw_ui_actions[i];
        if (property == action->property) {
            if (type == OSOS_PROPERTY_STRING)
                *out = (uintptr_t)action->labels[*action->status];
            else if (type == OSOS_PROPERTY_COUNT)
                *out = 1;
            else
                return 0;
            return 1;
        }
    }

    for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
        const struct cfw_ui_field *field = cfw_ui_fields[i];
        uint32_t selected = field->model->values[field->slot];
        if (selected >= field->count)
            selected = field->default_value;

        if (property == field->summary_property && type == OSOS_PROPERTY_COUNT) {
            *out = 1;
            return 1;
        }
        if (property == field->summary_property && type == OSOS_PROPERTY_STRING) {
            char *text = field->state->summary;
            const char *source = field->title;
            while (*source)
                *text++ = *source++;
            *text++ = ':';
            *text++ = ' ';
            source = field->labels[selected];
            do {
                *text++ = *source;
            } while (*source++);
            *out = (uintptr_t)field->state->summary;
            return 1;
        }

        if (property != field->property)
            continue;

        if (!field->numeric && type == OSOS_PROPERTY_COUNT) {
            *out = field->count;
        } else if (field->numeric && type == 0x436e746c) { /* Cntl */
            field->state->value = selected;
            field->state->minimum = 0;
            field->state->maximum = field->count - 1;
            *out = (uintptr_t)&field->state->value;
        } else if (field->numeric && type == OSOS_PROPERTY_STRING) {
            *out = (uintptr_t)field->labels[selected];
        } else {
            return 0;
        }
        return 1;
    }

    return 0;
}

int cfw_ui_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                            uintptr_t *out)
{
    for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
        const struct cfw_ui_field *field = cfw_ui_fields[i];
        if (property != field->property || field->numeric || index >= field->count)
            continue;

        if (type == OSOS_PROPERTY_STRING) {
            *out = (uintptr_t)field->labels[index];
        } else if (type == OSOS_RESOURCE_BMAP) {
            *out = index == field->model->values[field->slot]
                       ? osos_ui_bitmap(OSOS_MENU_CHECKED_BITMAP)
                       : 0;
        } else {
            return 0;
        }
        return 1;
    }

    return index == 0 ? cfw_ui_property(type, property, out) : 0;
}

int cfw_ui_handle_action(const char *action, uint32_t argument)
{
    for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
        const struct cfw_ui_field *field = cfw_ui_fields[i];
        if (field->numeric || !cfw_string_equal(action, field->action))
            continue;

        if (argument != OSOS_ACTION_SUPPORT_QUERY) {
            void *selection = osos_ui_selection();
            uint32_t index = osos_selection_index(selection);
            if (index < field->count) {
                field->model->change(field->slot, index);
                field->model->commit();
            }
        }
        return 1;
    }

    return 0;
}
