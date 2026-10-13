/* SPDX-License-Identifier: GPL-3.0-only */
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
    cfw_sleep_timer_sync();

    typedef void *(*registry)(void);
    typedef void *(*create)(void *, uint32_t, uint32_t, uint32_t);
    typedef void (*push)(void *, void *);
    void *menu = ((create)OSOS_CREATE_SCREEN_CONTROLLER)(
        ((registry)OSOS_CONTROLLER_REGISTRY)(), screen, layout,
        2); /* foregroundpush */
    ((push)osos_method(controller, 0xd0))(controller, menu);
}

/*
 * Center-button sleep selector. Audiobooks skip the shuffle selector after
 * Rating; instead they get it as Off / 30 min / 60 min, without the shuffle
 * icon, and choosing a value sets the stock sleep timer. Labels are swapped at
 * resource lookup (text views re-read them); the icon's bitmap is cached by
 * its view, so it is skipped at draw time instead.
 */
#include "patch.h"

#define TYPE_STR 0x53747220u

static int sleep_mode;
static int sleep_choice;
static void *sleep_slider;

/* Stock Alarms list rows (minutes 0/15/30/60/90/120) for our Off/30/60. */
static const uint8_t sleep_rows[] = {0, 2, 3};

static uint32_t label_ids[2];
static const char *const labels[2] = {"30 min", "60 min"};

static void set_sleep_mode(int sleep)
{
    if (sleep && !label_ids[0]) {
        label_ids[0] = resource_id("NowPlaying_ShuffleSongs_String");
        label_ids[1] = resource_id("NowPlaying_ShuffleAlbums_String");
    }
    sleep_mode = sleep;
}

/*
 * Views look strings up through the responder chain, where the language bank
 * answers before the base bank; the shared resource lookup hook (assets.c)
 * asks here first so the labels are replaced in every language.
 */
const void *cfw_sleep_timer_resource(uint32_t type, uint32_t id)
{
    if (sleep_mode && type == TYPE_STR && id) {
        for (int i = 0; i < 2; i++) {
            if (id == label_ids[i])
                return labels[i];
        }
    }
    return 0;
}

/*
 * The timer state lives in the Alarms controller singleton: its selected row
 * (+0x48) and its client key in the sleep service. Both shortcuts go through
 * its own choose method, so the stock list always shows the same state.
 */
static uint8_t *alarms(void)
{
    return ((uint8_t *(*)(void))OSOS_ALARMS_CONTROLLER)();
}

static void alarms_choose(int row)
{
    ((void (*)(void *, int))OSOS_ALARMS_SLEEP_CHOOSE)(alarms(), row);
}

/* Selected stock row, or 0 when no timer is armed (or it already fired). */
static int sleep_row(void)
{
    uint8_t *ctl = alarms();
    uint8_t *service = ((uint8_t *(*)(void))OSOS_SLEEP_SERVICE)();
    void *clients = *(void **)(service + 0x28);
    int row = *(int *)(ctl + 0x48);
    if (!((int (*)(void *, void *))OSOS_SLEEP_CLIENTS_ACTIVE)(clients, ctl))
        return 0;
    if (*(uint32_t *)((uint8_t *)clients + 0x48c) == 0xffff) {
        /* Fired: the stock list would still show the old row. */
        alarms_choose(0);
        return 0;
    }
    return row;
}

void cfw_sleep_timer_sync(void)
{
    sleep_row();
}

/* Off, or the nearest of 30/60 for any armed stock value. */
static int sleep_current_choice(void)
{
    switch (sleep_row()) {
    case 0:
        return 0;
    case 1:
    case 2:
        return 1;
    default:
        return 2;
    }
}

typedef int (*item_check)(void *);

/* The first of the Rating handler's checks; it decides the selector's mode. */
PATCH_ARM int cfw_sleep_skip_flag(void *item)
{
    int audiobook = ((item_check)OSOS_MEDIA_ITEM_IS_AUDIOBOOK)(item);
    set_sleep_mode(audiobook);
    return audiobook ? 0 : ((item_check)OSOS_MEDIA_ITEM_HAS_FLAG_8F01)(item);
}

PATCH_ARM int cfw_sleep_skip_kind(void *item)
{
    return sleep_mode ? 0 : ((item_check)OSOS_MEDIA_ITEM_HAS_KIND_200000)(item);
}

PATCH_ARM int cfw_sleep_skip_audiobook(void *item)
{
    return sleep_mode ? 0 : ((item_check)OSOS_MEDIA_ITEM_IS_AUDIOBOOK)(item);
}

PATCH_ARM void cfw_sleep_slider_init(void *slider, int index)
{
    if (sleep_mode) {
        sleep_slider = slider;
        sleep_choice = sleep_current_choice();
        index = sleep_choice;
    }
    ((void (*)(void *, int))OSOS_NOW_PLAYING_SLIDER_SET)(slider, index);
}

/* Replaces the shuffle commit; returns nonzero when the value changed. */
PATCH_ARM int cfw_sleep_commit(void *controller)
{
    if (!sleep_mode)
        return ((int (*)(void *))OSOS_NOW_PLAYING_SHUFFLE_COMMIT)(controller);
    uint8_t *state = controller;
    int changed = state[0xd9];
    *(int *)(state + 0xdc) = -1;
    state[0xd9] = 0;
    if (changed && sleep_slider) {
        int choice = ((int (*)(void *))OSOS_NOW_PLAYING_SLIDER_GET)(sleep_slider);
        if (choice >= 0 && choice < 3)
            alarms_choose(sleep_rows[choice]);
    }
    set_sleep_mode(0);
    return changed;
}

/* Bitmap view draw (vtable slot): hide the selector's shuffle icon. */
PATCH_ARM void cfw_sleep_bitmap_draw(void *view)
{
    static uint32_t shuffle_icon;
    if (sleep_mode) {
        if (!shuffle_icon)
            shuffle_icon = resource_id("NowPlaying_Large_Shuffle_Image");
        if (osos_bitmap_view_id(view) == shuffle_icon)
            return;
    }
    ((void (*)(void *))OSOS_BITMAP_VIEW_DRAW)(view);
}

