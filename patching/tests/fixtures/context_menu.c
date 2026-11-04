/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
#define CFW_PATCH_H
#define PATCH_ARM
#include "../../../payload/context_menu.c"

static unsigned displays, lookups, expected_cache;
static int missing;
static uint8_t *owner;
static int manager;

static struct {
    uint32_t id;
} cached;

static void *cached_layout(void *cache, uint32_t id)
{
    assert(cache == owner + expected_cache);
    assert(id == cached.id);
    lookups++;
    return missing ? NULL : &cached;
}

static void *layout_manager(void)
{
    return &manager;
}

static void display_layout(void *ui, void *layout)
{
    assert(ui == &manager && layout == &cached);
    displays++;
}

static void unexpected_switch_layout(void)
{
    assert(0 && "Restoration must preserve prepared control state");
}

static uintptr_t osos_method(void *controller, unsigned offset)
{
    assert(offset == 0x120);
    return **(uintptr_t **)controller;
}

#undef OSOS_CONTROLLER_CACHED_LAYOUT
#undef OSOS_UI_LAYOUT_MANAGER
#undef OSOS_UI_DISPLAY_LAYOUT
#define OSOS_CONTROLLER_CACHED_LAYOUT ((uintptr_t)cached_layout)
#define OSOS_UI_LAYOUT_MANAGER ((uintptr_t)layout_manager)
#define OSOS_UI_DISPLAY_LAYOUT ((uintptr_t)display_layout)
#include "../../../payload/native/context_menu.h"

static void restore_layout(void)
{
    uintptr_t method = (uintptr_t)unexpected_switch_layout;

    union {
        uintptr_t *vtable;
        uint32_t words[80];
    } controller = {.vtable = &method};

    owner = (uint8_t *)&controller;

    /* Native layout ID and cache member offsets (2.0.5). */
    const struct {
        unsigned offset, stored, cache;
    } offsets[] = {
        {OSOS_NOW_PLAYING_LAYOUT_OFFSET, 0x11c, 0xec},
        {OSOS_SONG_LAYOUT_OFFSET, 0x108, 0xd8},
        {OSOS_PLAYLIST_LAYOUT_OFFSET, 0x120, 0xf0},
        {OSOS_EPISODE_LAYOUT_OFFSET, 0x100, 0xd0},
    };

    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
        expected_cache = offsets[i].cache;
        cached.id = 100 + i;
        controller.words[offsets[i].stored / 4] = cached.id;
        osos_controller_restore_layout(&controller, offsets[i].offset);
        assert(displays == i + 1 && lookups == i + 1);
    }
    missing = 1;
    osos_controller_restore_layout(&controller, OSOS_EPISODE_LAYOUT_OFFSET);
    assert(displays == 4 && lookups == 5);
    method = OSOS_CONTROLLER_SWITCH_LAYOUT_DEFAULT;
    uintptr_t *bare_controller = &method;
    osos_controller_restore_layout(&bare_controller, OSOS_NOW_PLAYING_LAYOUT_OFFSET);
    assert(displays == 4 && lookups == 5);
}

int main(void)
{
    restore_layout();
    const char *screens[] = {
        "HandleStartGenius",
        "CFW_SongInfo",
        "CFW_NowPlayingChapters",
        "CFW_EpisodeChapters",
    };
    for (unsigned i = 0; i < sizeof(screens) / sizeof(*screens); i++)
        assert(cfw_context_menu_screen_action(screens[i], "HandleStartGenius") == 0);

    const char *actions[] = {
        "HandleCancel",
        "HandleAddToOTG",
        "HandleAudiobookSlower",
        "CFW_PlayNext",
        "CFW_PlayLast",
        "CFW_SongInfoExtra",
        "CFW_Chapters_Select",
        "CFW_NowPlayingChaptersExtra",
        "CFW_EpisodeChaptersExtra",
        "",
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        assert(cfw_context_menu_screen_action(actions[i], "HandleStartGenius") != 0);
    assert(cfw_context_menu_screen_action(NULL, "HandleStartGenius") != 0);
    assert(cfw_context_menu_screen_action("HandleViewAlbum", "HandleViewAlbum") == 0);
    return 0;
}
