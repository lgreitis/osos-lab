#ifndef CFW_CUSTOM_EQ_H
#define CFW_CUSTOM_EQ_H
#include <stdint.h>

#define CFW_EQ_FIELDS 13
#define CFW_EQ_DSP_PRESET 23

struct cfw_eq_field {
    uint32_t parent_table, parent_index, parent_item, selector_table, count,
        default_value;
    const uint32_t *labels, *marked, *summaries, *items;
};

extern const struct cfw_eq_field cfw_eq_fields[CFW_EQ_FIELDS];
extern const uint32_t cfw_eq_frequencies[];
extern const uint32_t cfw_eq_main_table, cfw_eq_save_item, cfw_eq_save_labels[3];

int cfw_settings_action(void *controller, const char *action, uint32_t argument);
void cfw_eq_load_preferences(void);
int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS]);
void cfw_eq_load(void *state);
void cfw_eq_process(void *state, int16_t *samples, uint32_t frames, int16_t **output,
                    uint32_t *output_frames);
#endif
