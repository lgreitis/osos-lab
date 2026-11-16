/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * A shortcut to the stock sleep timer. The Now Playing context menu gets a
 * Sleep Timer entry that pushes the native Extras > Alarms > Sleep Timer list
 * over Now Playing. The timer, its choices and its state are the stock ones;
 * nothing is stored or scheduled here, and Menu returns to Now Playing.
 */

#include "osos.h"
#include "sleep_timer.h"

/* Resolve a stock resource name through the global name map. */
static uint32_t resource_id(const char *text)
{
    struct osos_string name;
    osos_string_init(&name, text);
    uint32_t id = *osos_resource_name_slot(&name);
    osos_string_destroy(&name);
    return id;
}

/* Push the stock Alarms sleep timer list over Now Playing; Menu pops back to it. */
void cfw_sleep_timer_show(void *controller)
{
    uint32_t screen = resource_id("Alarms_Sleep_Timer_Screen");
    uint32_t layout = resource_id("Alarms_Sleep_Timer_Screen_Default");
    if (!screen || !layout)
        return;

    typedef void *(*registry)(void);
    typedef void *(*create)(void *, uint32_t, uint32_t, uint32_t);
    typedef void (*push)(void *, void *);
    void *menu = ((create)OSOS_CREATE_SCREEN_CONTROLLER)(
        ((registry)OSOS_CONTROLLER_REGISTRY)(), screen, layout,
        2); /* foregroundpush */
    ((push)osos_method(controller, 0xd0))(controller, menu);
}
