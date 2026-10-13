/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include "native/bindings.h"
#include "patch.h"

/*
 * Waking from the Now Playing idle clock (shown while playing on battery), the
 * firmware switches the backlight on while the clock is still on screen, so its
 * white page flashes before Now Playing slides back in. The clock controller is
 * already gone by then (the slide animates a snapshot), so hold the backlight
 * requests and apply them from a short timer instead.
 */
enum {
    WAKE_DELAY_MS = 350,
    TIMER_EVENT = 15,
};

static unsigned int idle_clocks;
static unsigned int waking;
static void *pending_backlight;
static int pending_level = -1;
static uint32_t wake_timer[16];
static unsigned int wake_timer_ready;

static uint8_t *sleep_service(void)
{
    return ((uint8_t *(*)(void))OSOS_SLEEP_SERVICE)();
}

/* A firmware timer owned by the sleep service; its event comes back below. */
static void arm_wake_timer(void)
{
    if (!wake_timer_ready) {
        uint8_t *service = sleep_service();
        /* Same task as the service's own timers. */
        void *task = *(void **)(service + OSOS_SLEEP_SERVICE_TIMER + 0x28);
        ((void (*)(void *, int, void *, void *))OSOS_TIMER_INIT)(wake_timer, 0, service, task);
        wake_timer_ready = 1;
    }
    ((void (*)(void *))OSOS_TIMER_STOP)(wake_timer);
    ((void (*)(void *, uint32_t))OSOS_TIMER_SET_INTERVAL)(wake_timer, WAKE_DELAY_MS);
    ((void (*)(void *))OSOS_TIMER_START)(wake_timer);
}

static int holding(void)
{
    return idle_clocks || waking;
}

static void hold(void *backlight)
{
    if (!pending_backlight)
        arm_wake_timer();
    pending_backlight = backlight;
}

static void release(void)
{
    void *backlight = pending_backlight;
    int level = pending_level;
    waking = 0;
    pending_backlight = 0;
    pending_level = -1;
    if (!backlight)
        return;
    if (level >= 0)
        ((int (*)(void *, int))OSOS_BACKLIGHT_LEVEL)(backlight, level);
    ((int (*)(void *, int))OSOS_BACKLIGHT_ENABLE)(backlight, 1);
}

/* Setting a nonzero level also switches the backlight on. */
PATCH_ARM int cfw_backlight_level(void *backlight, int level)
{
    if (level && holding()) {
        hold(backlight);
        pending_level = level;
        return 0;
    }
    return ((int (*)(void *, int))OSOS_BACKLIGHT_LEVEL)(backlight, level);
}

PATCH_ARM int cfw_backlight_enable(void *backlight, int on)
{
    if (on && holding()) {
        hold(backlight);
        return 0;
    }
    if (!on) {
        waking = 0;
        pending_backlight = 0;
        pending_level = -1;
    }
    return ((int (*)(void *, int))OSOS_BACKLIGHT_ENABLE)(backlight, on);
}

PATCH_ARM int cfw_sleep_service_event(void *service, uint8_t *event)
{
    if (*(uint32_t *)(event + 4) == TIMER_EVENT &&
        *(void **)(event + 8) == (void *)wake_timer) {
        release();
        return 1;
    }
    return ((int (*)(void *, void *))OSOS_SLEEP_SERVICE_EVENT)(service, event);
}

PATCH_ARM void *cfw_idle_clock_create(void *a, void *b, void *c, void *d)
{
    typedef void *(*create)(void *, void *, void *, void *);
    void *clock = ((create)OSOS_IDLE_CLOCK_CREATE)(a, b, c, d);
    if (clock)
        idle_clocks++;
    return clock;
}

static void idle_clock_gone(void)
{
    if (idle_clocks && !--idle_clocks)
        waking = 1;
}

PATCH_ARM void *cfw_idle_clock_destroy(void *clock)
{
    clock = ((void *(*)(void *))OSOS_IDLE_CLOCK_DESTROY)(clock);
    idle_clock_gone();
    return clock;
}

/* The deleting destructor calls the plain one directly, not through the slot. */
PATCH_ARM void cfw_idle_clock_delete(void *clock)
{
    ((void (*)(void *))OSOS_IDLE_CLOCK_DELETE)(clock);
    idle_clock_gone();
}
