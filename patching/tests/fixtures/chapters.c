/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_PATCH_H
#define PATCH_ARM
#define CFW_OSOS_H
#define CFW_NATIVE_CHAPTERS_H
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#define OSOS_PROPERTY_COUNT 0x564d6178
#define OSOS_PROPERTY_STRING 0x53747220
#define OSOS_RESOURCE_BMAP 0x424d6170
#include "chapters_ui.h"
#include "../../../payload/native/bindings.h"

struct osos_media_item {
    void *record;
    uint32_t reserved[3];
};

struct osos_player {
    int unused;
};

struct osos_context_menu {
    int unused;
};

struct osos_native_string {
    const char *text;
};
static struct osos_player player;
static int record_a, record_b, controller;
static struct osos_media_item playing = {&record_a, {0, 1, 0}};
static uint32_t count = 9, selected, duration = 9000, seek_to;
static unsigned locks, seeks, pushes, menus, creates;
static unsigned restores, restored_offset;
static void *restored_controller;

static void osos_controller_restore_layout(void *owner, unsigned offset)
{
    assert(!locks);
    restores++;
    restored_offset = offset;
    restored_controller = owner;
}

static uint32_t time_offset;
static int valid = 1, video, unnamed, create_failed;
static const char *title_text = "Überblick 🎧";

static struct osos_player *osos_player_get(void)
{
    return &player;
}

static void osos_player_lock(struct osos_player *p)
{
    assert(p == &player);
    locks++;
}

static void osos_player_unlock(struct osos_player *p)
{
    assert(p == &player && locks);
    locks--;
}

static void osos_media_item_init(struct osos_media_item *i)
{
    memset(i, 0, sizeof(*i));
}

static void osos_player_current_item(struct osos_player *p, struct osos_media_item *i)
{
    assert(p == &player && locks);
    *i = playing;
}

static int osos_media_item_valid(struct osos_media_item *i)
{
    return valid && i->record && i->reserved[1] == playing.reserved[1];
}

static int osos_media_item_has_kind_8062(struct osos_media_item *i)
{
    (void)i;
    return video;
}

static uint32_t osos_media_item_chapter_count(struct osos_media_item *i)
{
    assert(i->record);
    return count;
}

static uint32_t osos_media_item_chapter_start(struct osos_media_item *i, uint32_t n)
{
    assert(i->record && n < count);
    return n * 1000 + time_offset;
}

static uint32_t osos_media_item_duration(struct osos_media_item *i)
{
    (void)i;
    return duration;
}

static void osos_player_seek(struct osos_player *p, uint32_t time)
{
    assert(p == &player && locks);
    seeks++;
    seek_to = time;
}

static void osos_context_menu_append(struct osos_context_menu *m, uint32_t label,
                                     const char *action, uint32_t flags)
{
    (void)m;
    if (!strcmp(action, "HandleCancel")) {
        assert(label == 123 && flags == 0);
        return;
    }
    assert(
        label == CFW_CHAPTERS_LABEL &&
        (!strcmp(action, "CFW_NowPlayingChapters") || !strcmp(action, "CFW_EpisodeChapters")) &&
        flags == 1);
    menus++;
}

static void *osos_controller_registry(void)
{
    return &controller;
}

static void *osos_create_screen_controller(void *registry, uint32_t screen,
                                           uint32_t layout, uint32_t transition)
{
    assert(registry == &controller && screen == CFW_Chapters_Screen &&
           layout == CFW_Chapters_Layout && transition == 2);
    creates++;
    return create_failed ? NULL : &controller;
}

static void osos_controller_push(void *c, void *s)
{
    assert(c == &controller && s == &controller);
    pushes++;
}

static void *osos_ui_selection(void)
{
    return &selected;
}

static uint32_t osos_selection_index(void *s)
{
    return *(uint32_t *)s;
}

static void osos_native_string_init(struct osos_native_string *s)
{
    s->text = "";
}

static void osos_media_item_chapter_title(struct osos_media_item *i, uint32_t p,
                                          struct osos_native_string *s)
{
    assert(i->record && p < duration);
    s->text = unnamed ? "" : title_text;
}

static const char *osos_native_string_data(struct osos_native_string *s)
{
    return s->text;
}

static void osos_native_string_destroy(struct osos_native_string *s)
{
    (void)s;
}

static struct osos_media_item library_item = {&record_b, {0, 1, 0}};
static unsigned episode_plays;
static int model_missing;

static void *osos_song_controller_model(void *c)
{
    assert(c == &controller);
    return model_missing ? NULL : c;
}

static void osos_song_model_get_item(void *model, int row, struct osos_media_item *out)
{
    assert(model == &controller && row == 4);
    *out = library_item;
}

static int osos_episode_menu(void *c, int row)
{
    assert(c == &controller && (row == 4 || row == -1));
    return 42;
}

static int osos_episode_action(void *c, const char *action, uint32_t argument)
{
    (void)c;
    (void)action;
    (void)argument;
    return 43;
}

static void osos_episode_play_chapter(void *c, int row, uint32_t start)
{
    assert(c == &controller && row == 4 && start == 1000);
    episode_plays++;
}

#include "../../../payload/chapters.c"
#include "../../../payload/episode_actions.c"

int main(void)
{
    uintptr_t out;
    assert(!cfw_chapters_action(&controller, "Other", 0));
    assert(cfw_chapters_action(&controller, "CFW_NowPlayingChapters", OSOS_ACTION_SUPPORT_QUERY));
    assert(!creates && !locks);
    cfw_chapters_menu(0);
    assert(menus == 1);
    count = 0;
    cfw_chapters_menu(0);
    assert(menus == 1);
    cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
    assert(!pushes);
    count = 9;
    video = 1;
    cfw_chapters_menu(0);
    assert(menus == 1);
    video = 0;
    valid = 0;
    cfw_chapters_menu(0);
    assert(menus == 1);
    valid = 1;
    cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
    assert(pushes == 1);
    assert(cfw_chapters_property(OSOS_PROPERTY_COUNT, CFW_Chapters_Rows, &out) &&
           out == 9);
    assert(!cfw_chapters_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, &out));
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 0,
                                         &out));
    assert(!strcmp((char *)out, "Überblick 🎧"));
    char long_title[1025];
    for (unsigned i = 0; i < 255; i++)
        memcpy(long_title + i * 3, "界", 3);
    long_title[765] = 0;
    title_text = long_title;
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 0,
                                         &out));
    assert(strlen((char *)out) == 765 && !strcmp((char *)out, long_title));
    for (unsigned i = 0; i < 256; i++)
        memcpy(long_title + i * 4, "🎧", 4);
    long_title[1024] = 0;
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 0,
                                         &out));
    assert(strlen((char *)out) == 764);
    assert(!memcmp((char *)out, long_title, 764));
    title_text = "Überblick 🎧";
    const uint32_t times[] = {0,       35999,   59999,   60000,
                              3599999, 3600000, 3661000, UINT32_MAX};
    const char *labels[] = {"0:00",  "0:35",    "0:59",    "1:00",
                            "59:59", "1:00:00", "1:01:01", "1193:02:47"};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
        time_offset = times[i];
        assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Detail,
                                             0, &out));
        assert(!strcmp((char *)out, labels[i]));
    }
    time_offset = 0;
    assert(!cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Detail, 9,
                                          &out));
    assert(!cfw_chapters_indexed_property(OSOS_RESOURCE_BMAP, CFW_Chapters_Detail, 0,
                                          &out));
    unnamed = 1;
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 8,
                                         &out));
    assert(!strcmp((char *)out, "Chapter 9"));
    assert(!cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 9,
                                          &out));
    selected = 8;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", OSOS_ACTION_SUPPORT_QUERY);
    assert(!seeks);
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 1 && seek_to == 8000);
    selected = 0;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 2 && seek_to == 0);
    playing.record = &record_b;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 2);
    playing.record = &record_a;
    playing.reserved[1]++;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 2);
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Rows, 0,
                                         &out));
    assert(!strcmp((char *)out, "Chapter unavailable"));
    assert(cfw_chapters_indexed_property(OSOS_PROPERTY_STRING, CFW_Chapters_Detail, 0,
                                         &out));
    assert(!strcmp((char *)out, ""));
    playing.reserved[1]--;
    selected = 9;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 2);
    selected = 8;
    duration = 8000;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 2);
    count = 2000;
    duration = 2000000;
    cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
    assert(cfw_chapters_property(OSOS_PROPERTY_COUNT, CFW_Chapters_Rows, &out) &&
           out == 2000);
    selected = 1999;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 3 && seek_to == 1999000 && !locks);
    unsigned previous_menus = menus, previous_pushes = pushes;
    assert(cfw_episode_action(&controller, "Other", 0) == 43);
    assert(cfw_episode_action(&controller, "CFW_EpisodeChapters",
                              OSOS_ACTION_SUPPORT_QUERY));
    assert(pushes == previous_pushes && !episode_plays);
    assert(cfw_episode_menu(&controller, 4) == 42);
    cfw_episode_menu_cancel(NULL, 123, "HandleCancel", 0);
    assert(menus == previous_menus + 1);
    int other_controller;
    cfw_episode_action(&other_controller, "CFW_EpisodeChapters", 0);
    assert(pushes == previous_pushes && !episode_plays);
    create_failed = 1;
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(pushes == previous_pushes && !episode_plays);
    create_failed = 0;
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(pushes == previous_pushes + 1 && !episode_plays && seeks == 3);
    assert(chapters.item.record ==
           &record_b); /* Selected episode, not playing record_a. */
    selected = 1;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", OSOS_ACTION_SUPPORT_QUERY);
    assert(!episode_plays);
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(episode_plays == 1 && seeks == 3);
    library_item.record = &record_a;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(episode_plays == 1);
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(episode_plays == 1 && pushes == previous_pushes + 1);
    library_item.record = &record_b;
    library_item.reserved[1]++;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(episode_plays == 1);
    library_item.reserved[1]--;
    selected = count;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(episode_plays == 1);
    selected = 1;
    duration = 1000;
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(episode_plays == 1);
    cfw_episode_menu(&controller,
                     -1); /* All row must not become a chaptered episode. */
    cfw_episode_menu_cancel(NULL, 123, "HandleCancel", 0);
    assert(menus == previous_menus + 1);
    model_missing = 1;
    cfw_episode_menu(&controller, 4);
    cfw_episode_menu_cancel(NULL, 123, "HandleCancel", 0);
    assert(menus == previous_menus + 1);
    model_missing = 0;
    count = 0;
    cfw_episode_menu(&controller, 4);
    cfw_episode_menu_cancel(NULL, 123, "HandleCancel", 0);
    assert(menus == previous_menus + 1);
    count = 9;
    duration = 9000;
    cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
    cfw_chapters_action(&controller, "CFW_Chapters_Select", 0);
    assert(seeks == 4 && episode_plays == 1); /* Now Playing clears episode origin. */
    previous_menus = menus;
    previous_pushes = pushes;
    create_failed = 1;
    cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
    assert(pushes == previous_pushes && !locks);
    create_failed = 0;
    count = INT32_MAX;
    assert(cfw_chapters_count(&playing) == INT32_MAX);
    struct osos_media_item boundary_item;
    assert(episode_chapter_count(&controller, 4, &boundary_item) == INT32_MAX);
    const uint32_t invalid_counts[] = {(uint32_t)INT32_MAX + 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); i++) {
        count = invalid_counts[i];
        cfw_chapters_menu(NULL);
        cfw_chapters_action(&controller, "CFW_NowPlayingChapters", 0);
        assert(cfw_chapters_property(OSOS_PROPERTY_COUNT, CFW_Chapters_Rows, &out));
        assert(out == 0 && menus == previous_menus && pushes == previous_pushes);
        cfw_episode_menu(&controller, 4);
        cfw_episode_menu_cancel(NULL, 123, "HandleCancel", 0);
        cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
        assert(menus == previous_menus && pushes == previous_pushes &&
               episode_plays == 1);
    }
    /* A suppressed dismissal must either push a screen or restore its source. */
    count = 9;
    valid = 1;
    model_missing = 0;
    create_failed = 0;
    playing.record = &record_a;
    library_item.record = &record_b;
    library_item.reserved[1] = playing.reserved[1];
    for (unsigned episode = 0; episode < 2; episode++) {
        const char *action = episode ? "CFW_EpisodeChapters" : "CFW_NowPlayingChapters";
        int (*handler)(void *, const char *, uint32_t) =
            episode ? cfw_episode_action : cfw_chapters_action;
        unsigned offset = episode ? OSOS_EPISODE_LAYOUT_OFFSET
                                  : OSOS_NOW_PLAYING_LAYOUT_OFFSET;
        if (episode)
            cfw_episode_menu(&controller, 4);
        restores = 0;
        unsigned before_pushes = pushes, before_creates = creates;
        handler(&controller, action, OSOS_ACTION_SUPPORT_QUERY);
        assert(!restores && pushes == before_pushes && creates == before_creates);
        handler(&controller, action, 0);
        assert(!restores && pushes == before_pushes + 1);
        create_failed = 1;
        handler(&controller, action, 0);
        assert(restores == 1 && restored_offset == offset);
        assert(restored_controller == &controller && pushes == before_pushes + 1);
        create_failed = 0;
        count = 0;
        handler(&controller, action, 0);
        assert(restores == 2 && restored_offset == offset);
        count = 9;
        valid = 0;
        handler(&controller, action, 0);
        assert(restores == 3 && restored_offset == offset);
        valid = 1;
        assert(pushes == before_pushes + 1 && !locks);
    }
    restores = 0;
    library_item.record = &record_a;
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(restores == 1 && restored_offset == OSOS_EPISODE_LAYOUT_OFFSET);
    library_item.record = &record_b;
    library_item.reserved[1]++;
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(restores == 2);
    library_item.reserved[1]--;
    model_missing = 1;
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(restores == 3);
    model_missing = 0;
    cfw_episode_action(&other_controller, "CFW_EpisodeChapters", 0);
    assert(restores == 4 && restored_controller == &other_controller);
    cfw_episode_menu(&controller, -1);
    cfw_episode_action(&controller, "CFW_EpisodeChapters", 0);
    assert(restores == 5 && restored_controller == &controller);
    cfw_episode_action(&controller, "CFW_EpisodeChapters", OSOS_ACTION_SUPPORT_QUERY);
    assert(restores == 5);
    return 0;
}
