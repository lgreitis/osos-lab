/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_UI_H
#define CFW_UI_H
#include <stdint.h>

struct cfw_ui_events {
    uint32_t type, id, size;
    const unsigned char *data;
};

struct cfw_ui_event_alias {
    uint32_t type, id, original;
};
extern const struct cfw_ui_events cfw_ui_events[];
extern const uint32_t cfw_ui_event_count;
extern const struct cfw_ui_event_alias cfw_ui_event_aliases[];
extern const uint32_t cfw_ui_event_alias_count;

struct cfw_ui_model {
    uint32_t *values;
    void (*change)(uint32_t field, uint32_t value);
    void (*commit)(void);
};

struct cfw_ui_field_state {
    /* Native Cntl resources contain three signed words in this order. */
    int32_t value, minimum, maximum;
    char *summary;
};

struct cfw_ui_field {
    uint32_t property, summary_property, screen, count, default_value, slot, numeric;
    const char *action, *title;
    const char *const *labels;
    const struct cfw_ui_model *model;
    struct cfw_ui_field_state *state;
};

struct cfw_ui_action {
    uint32_t property, count;
    const char *const *labels;
    uint32_t *status;
};

extern const struct cfw_ui_field *const cfw_ui_fields[];
extern const uint32_t cfw_ui_field_count;
extern const struct cfw_ui_action *const cfw_ui_actions[];
extern const uint32_t cfw_ui_action_count;

void cfw_ui_refresh(const struct cfw_ui_field *field);
void cfw_ui_status(const struct cfw_ui_action *action, unsigned int status);
int cfw_ui_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_ui_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                            uintptr_t *out);
int cfw_ui_handle_action(const char *action, uint32_t argument);
void cfw_ui_register_controllers(void);
#endif
