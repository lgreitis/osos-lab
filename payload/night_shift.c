/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * Night Shift: warm the whole screen through the display pipe's per-channel
 * gamma tables (0x38900400, three 256-entry tables of 10-bit levels; the stock
 * ramp is i * 4). The hardware applies it to every pixel, artwork included,
 * at no per-frame cost. Table order red/green/blue matches the 6G (1.1.2)
 * display init, which loads the same identity ramp into all three.
 */

#include "night_shift.h"
#include "night_shift_ui.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "patch.h"

enum {
    NIGHT_SHIFT_OFF_RECORD = 0x30544e53, /* SNT0 */
    NIGHT_SHIFT_ON_RECORD = 0x31544e53,  /* SNT1 */
    GAMMA_ENTRIES = 256,
};

/* Per-channel gain /256 while enabled: about a mid iOS Night Shift setting. */
static const uint16_t warm_gain[3] = {256, 216, 150};

static unsigned int enabled;
static uint32_t gamma_tables[3][GAMMA_ENTRIES];
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWNight"};

typedef int (*set_gamma_fn)(const uint32_t *, const uint32_t *, const uint32_t *);

static int apply(void)
{
    for (int channel = 0; channel < 3; channel++) {
        uint32_t gain = enabled ? warm_gain[channel] : 256;
        for (uint32_t i = 0; i < GAMMA_ENTRIES; i++)
            gamma_tables[channel][i] = (i * 4 * gain) >> 8;
    }
    return ((set_gamma_fn)OSOS_DISPLAY_SET_GAMMA)(gamma_tables[0], gamma_tables[1], gamma_tables[2]);
}

void cfw_night_shift_load(void)
{
    uint32_t record;
    enabled = cfw_preferences_read(&settings_path, &record, sizeof(record)) &&
              record == NIGHT_SHIFT_ON_RECORD;
    apply();
}

static int save(unsigned int value)
{
    uint32_t record = value ? NIGHT_SHIFT_ON_RECORD : NIGHT_SHIFT_OFF_RECORD;
    return cfw_preferences_write(&settings_path, &record, sizeof(record));
}

/* Display init: the stock identity ramp is replaced by the current tables. */
PATCH_ARM int cfw_night_shift_init_gamma(const uint32_t *red, const uint32_t *green,
                                         const uint32_t *blue)
{
    (void)red, (void)green, (void)blue;
    return apply();
}

int cfw_night_shift_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_NightShift_Value)
        return 0;
    *out = osos_settings_on_off(enabled);
    return 1;
}

int cfw_night_shift_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_NightShift_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    if (save(!enabled)) {
        enabled = !enabled;
        apply();
        osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                          CFW_NightShift_Value);
    }
    return 1;
}
