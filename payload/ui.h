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

/* One choice list from a <setting>; its slot is the index in the generated array. */
struct cfw_ui_field {
    uint32_t count, default_value;
    const char *title;
    const char *const *labels;
};

void cfw_ui_register_canvas(void);
#endif
