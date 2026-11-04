/* SPDX-License-Identifier: GPL-3.0-only */
#include "chapters.h"
#include "osos.h"
#include "cfw.h"
#include "chapters_ui.h"
#include "native/chapters.h"

/* MediaItem is a non-owning, generation-checked native value (24 bytes).
 * Keep the list tied to the track opened, even when playback advances. */
static struct {
    struct osos_media_item item;
    uint32_t count;
    void *episode_controller;
    int episode_row;
    char time[12];   /* Maximum uint32 millisecond position: 1193:02:47. */
    char label[768]; /* 255 UTF-16 units need at most 765 UTF-8 bytes plus NUL. */
} chapters;

/* +0x10 (reserved[1]) is the database generation. */
int cfw_chapters_same_item(struct osos_media_item *a, struct osos_media_item *b)
{
    return osos_media_item_valid(a) && osos_media_item_valid(b) &&
           a->record == b->record && a->reserved[1] == b->reserved[1];
}

uint32_t cfw_chapters_count(struct osos_media_item *item)
{
    if (!item->record || !osos_media_item_valid(item) ||
        osos_media_item_has_kind_8062(item))
        return 0;
    uint32_t count = osos_media_item_chapter_count(item);
    return count <= INT32_MAX ? count : 0;
}

void cfw_chapters_menu(struct osos_context_menu *menu)
{
    struct osos_player *player = osos_player_get();
    struct osos_media_item item;
    osos_media_item_init(&item);
    osos_player_lock(player);
    osos_player_current_item(player, &item);
    uint32_t count = cfw_chapters_count(&item);
    osos_player_unlock(player);
    if (count)
        osos_context_menu_append(menu, CFW_CHAPTERS_LABEL, "CFW_NowPlayingChapters", 1);
}

static int push_chapters(void *controller)
{
    if (!chapters.count)
        return 0;
    void *screen = osos_create_screen_controller(
        osos_controller_registry(), CFW_Chapters_Screen, CFW_Chapters_Layout, 2);
    if (!screen)
        return 0;
    osos_controller_push(controller, screen);
    return 1;
}

int cfw_chapters_show_episode(void *controller, struct osos_media_item *item, int row)
{
    chapters.item = *item;
    chapters.count = cfw_chapters_count(&chapters.item);
    chapters.episode_controller = controller;
    chapters.episode_row = row;
    return push_chapters(controller);
}

static int open_chapters(void *controller)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    chapters.episode_controller = NULL;
    osos_media_item_init(&chapters.item);
    osos_player_current_item(player, &chapters.item);
    chapters.count = cfw_chapters_count(&chapters.item);
    osos_player_unlock(player);
    return push_chapters(controller);
}

static void select_chapter(uint32_t index)
{
    if (index >= chapters.count)
        return;
    if (chapters.episode_controller) {
        cfw_episode_play_chapter(chapters.episode_controller, chapters.episode_row,
                                 &chapters.item, index);
        return;
    }
    struct osos_player *player = osos_player_get();
    struct osos_media_item current;
    osos_media_item_init(&current);
    osos_player_lock(player);
    osos_player_current_item(player, &current);
    /* Never seek a replacement track. */
    if (cfw_chapters_same_item(&current, &chapters.item) &&
        index < cfw_chapters_count(&current)) {
        uint32_t position = osos_media_item_chapter_start(&current, index);
        if (position < osos_media_item_duration(&current))
            osos_player_seek(player, position);
    }
    osos_player_unlock(player);
}

int cfw_chapters_action(void *controller, const char *action, uint32_t argument)
{
    int open = cfw_string_equal(action, "CFW_NowPlayingChapters");
    if (!open && !cfw_string_equal(action, "CFW_Chapters_Select"))
        return 0;
    if (argument != OSOS_ACTION_SUPPORT_QUERY) {
        if (open) {
            if (!open_chapters(controller))
                osos_controller_restore_layout(controller,
                                                OSOS_NOW_PLAYING_LAYOUT_OFFSET);
        } else {
            select_chapter(osos_selection_index(osos_ui_selection()));
        }
    }
    return 1;
}

int cfw_chapters_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (property != CFW_Chapters_Rows || type != OSOS_PROPERTY_COUNT)
        return 0;
    *out = chapters.count;
    return 1;
}

static const char *chapter_label(uint32_t index)
{
    if (!osos_media_item_valid(&chapters.item))
        return "Chapter unavailable";
    struct osos_native_string title;
    osos_native_string_init(&title);
    osos_media_item_chapter_title(
        &chapters.item, osos_media_item_chapter_start(&chapters.item, index), &title);
    const char *text = osos_native_string_data(&title);
    unsigned size = 0;
    if (text) {
        /* Copy complete UTF-8 characters if a malformed title exceeds the native limit. */
        while (*text) {
            unsigned char lead = (unsigned char)*text;
            unsigned bytes = lead < 0xc0 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            if (size + bytes >= sizeof(chapters.label))
                break;
            for (unsigned i = 0; i < bytes && *text; i++)
                chapters.label[size++] = *text++;
        }
    }
    osos_native_string_destroy(&title);
    if (!size) {
        const char *prefix = "Chapter ";
        while (*prefix)
            chapters.label[size++] = *prefix++;
        char digits[10];
        unsigned length = 0;
        uint32_t number = index + 1;
        do {
            digits[length++] = '0' + number % 10;
            number /= 10;
        } while (number);
        while (length)
            chapters.label[size++] = digits[--length];
    }
    chapters.label[size] = 0;
    return chapters.label;
}

static const char *chapter_time(uint32_t index)
{
    if (!osos_media_item_valid(&chapters.item))
        return "";
    uint32_t seconds = osos_media_item_chapter_start(&chapters.item, index) / 1000;
    uint32_t hours = seconds / 3600;
    uint32_t leading = hours ? hours : seconds / 60;
    char digits[10];
    unsigned length = 0, size = 0;
    do {
        digits[length++] = '0' + leading % 10;
        leading /= 10;
    } while (leading);
    while (length)
        chapters.time[size++] = digits[--length];
    if (hours) {
        chapters.time[size++] = ':';
        chapters.time[size++] = '0' + seconds / 60 % 60 / 10;
        chapters.time[size++] = '0' + seconds / 60 % 10;
    }
    chapters.time[size++] = ':';
    chapters.time[size++] = '0' + seconds % 60 / 10;
    chapters.time[size++] = '0' + seconds % 10;
    chapters.time[size] = 0;
    return chapters.time;
}

int cfw_chapters_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                                  uintptr_t *out)
{
    if (index >= chapters.count)
        return 0;
    if (property == CFW_Chapters_Detail && type == OSOS_PROPERTY_STRING) {
        *out = (uintptr_t)chapter_time(index);
        return 1;
    }
    if (property != CFW_Chapters_Rows)
        return 0;
    if (type == OSOS_PROPERTY_STRING)
        *out = (uintptr_t)chapter_label(index);
    else
        return 0;
    return 1;
}
