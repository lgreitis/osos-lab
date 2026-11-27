/* SPDX-License-Identifier: MIT */

/* Browses the iPod's library: artists and playlists lead to their tracks,
 * shown with the highlighted track's cover and length; Center plays the
 * tracks from the highlighted one on (the host queues them as files, so Now
 * Playing shows no titles for them). Photos steps through the photo library
 * with the wheel. Players lists the games' player list: Center on NEW PLAYER
 * adds one, Play removes the highlighted player. Menu goes back and exits
 * from the first page. Only the visible rows are read from the library. The
 * SDK font draws letters in upper case, and other characters leave gaps. */

#include "eapp_draw.h"
#include "eapp_media.h"
#include "eapp_ui.h"

enum page { HOME, ARTISTS, PLAYLISTS, TRACKS, PHOTOS, PLAYERS };
enum { ROWS = 10, ROW_HEIGHT = 18, TOP = 34, QUEUE_MAX = 100, COVER = 100 };

static enum page page, tracks_from;
static uint32_t count, selected, first;
static struct eapp_wheel_steps wheel_steps;
static char rows[ROWS][48];
static uint32_t rows_first = 0xffffffff;
static char title[48], detail[32];
static struct eapp_media_picture *picture;
static uint32_t picture_index = 0xffffffff;
/* The playlist the tracks page shows in its own order, NULL for an artist. */
static struct eapp_media_playlist *playlist;

static const char *const home_rows[] = {"ARTISTS", "PLAYLISTS", "PHOTOS", "PLAYERS"};

static char *number(char *out, uint32_t value)
{
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

static void copy(char *out, size_t size, const char *text)
{
    size_t n = 0;
    for (; text[n] && n + 1 < size; ++n)
        out[n] = text[n];
    out[n] = 0;
}

static void drop_picture(void)
{
    eapp_media_picture_free(picture);
    picture = NULL;
    picture_index = 0xffffffff;
}

static struct eapp_media_track *open_track(uint32_t index)
{
    return playlist ? eapp_media_playlist_track_open(playlist, index)
                    : eapp_media_track_open(index);
}

static uint32_t page_count(void)
{
    switch (page) {
    case HOME:
        return sizeof home_rows / sizeof home_rows[0];
    case ARTISTS:
        return eapp_media_count(EAPP_MEDIA_ARTISTS);
    case PLAYLISTS:
        return eapp_media_count(EAPP_MEDIA_PLAYLISTS);
    case TRACKS:
        return playlist ? eapp_media_playlist_track_count(playlist)
                        : eapp_media_count(EAPP_MEDIA_TRACKS);
    case PHOTOS:
        return eapp_media_photo_count(-1);
    case PLAYERS:
        return eapp_user_count() + 1;
    }
    return 0;
}

static void open_page(enum page next, const char *heading)
{
    page = next;
    copy(title, sizeof title, heading);
    count = page_count();
    selected = first = 0;
    rows_first = 0xffffffff;
    detail[0] = 0;
    drop_picture();
}

static void row_text(uint32_t index, char *out, size_t size)
{
    out[0] = 0;
    switch (page) {
    case HOME:
        copy(out, size, home_rows[index]);
        break;
    case ARTISTS:
        eapp_media_name(EAPP_MEDIA_ARTISTS, index, out, size);
        break;
    case PLAYLISTS:
        eapp_media_name(EAPP_MEDIA_PLAYLISTS, index, out, size);
        break;
    case TRACKS:
        if (playlist) {
            struct eapp_media_track *track = open_track(index);
            eapp_media_track_title(track, out, size);
            eapp_media_track_close(track);
        } else {
            eapp_media_name(EAPP_MEDIA_TRACKS, index, out, size);
        }
        break;
    case PHOTOS:
        break;
    case PLAYERS:
        if (index + 1 == count)
            copy(out, size, "NEW PLAYER");
        else
            eapp_user_name(eapp_user_id(index), out, size);
        break;
    }
}

/* The visible rows, read again only when the window moves. */
static void load_rows(void)
{
    if (rows_first == first)
        return;
    rows_first = first;
    for (uint32_t i = 0; i < ROWS; ++i)
        if (first + i < count)
            row_text(first + i, rows[i], sizeof rows[i]);
}

/* The highlighted track's cover and length, or the current photo. */
static void load_picture(void)
{
    if (picture_index == selected || (page != TRACKS && page != PHOTOS) || !count)
        return;
    drop_picture();
    picture_index = selected;
    struct eapp_media_image *image;
    if (page == PHOTOS) {
        image = eapp_media_photo_open(-1, selected);
        picture = eapp_media_image_load(image, 320, 240);
    } else {
        struct eapp_media_track *track = open_track(selected);
        uint32_t seconds = eapp_media_track_duration_ms(track) / 1000;
        char *end = number(detail, seconds / 60);
        *end++ = ':';
        *end++ = (char)('0' + seconds % 60 / 10);
        *end++ = (char)('0' + seconds % 10);
        *end = 0;
        image = eapp_media_artwork_find(eapp_media_track_id(track));
        picture = eapp_media_image_load(image, COVER, COVER);
        eapp_media_track_close(track);
    }
    eapp_media_image_close(image);
}

/* Plays the tracks from the highlighted one on, up to QUEUE_MAX. */
static void play_from_selected(void)
{
    eapp_music_clear_list();
    for (uint32_t i = 0; i < QUEUE_MAX && selected + i < count; ++i) {
        struct eapp_media_track *track = open_track(selected + i);
        eapp_music_append(track);
        eapp_media_track_close(track);
    }
    eapp_music_play_track(0);
}

static void choose(void)
{
    char name[48];
    switch (page) {
    case HOME:
        if (selected == 0) {
            eapp_media_reset();
            open_page(ARTISTS, "ARTISTS");
        } else if (selected == 1) {
            eapp_media_reset();
            open_page(PLAYLISTS, "PLAYLISTS");
        } else if (selected == 2) {
            open_page(PHOTOS, "PHOTOS");
        } else {
            open_page(PLAYERS, "PLAYERS");
        }
        break;
    case ARTISTS:
    case PLAYLISTS:
        if (!count)
            break;
        if (page == PLAYLISTS && !(playlist = eapp_media_playlist_open(selected)))
            break;
        if (page == ARTISTS)
            eapp_media_select(EAPP_MEDIA_ARTISTS, selected);
        tracks_from = page;
        row_text(selected, name, sizeof name);
        open_page(TRACKS, name);
        break;
    case TRACKS:
        play_from_selected();
        break;
    case PLAYERS:
        if (selected + 1 == count) {
            char player[16] = "PLAYER ";
            number(player + 7, count);
            eapp_user_add(player);
            uint32_t keep = selected;
            open_page(PLAYERS, "PLAYERS");
            selected = keep;
        }
        break;
    case PHOTOS:
        break;
    }
}

static void back(void)
{
    if (page == HOME) {
        eapp_request_exit();
    } else if (page == TRACKS) {
        eapp_media_playlist_close(playlist);
        playlist = NULL;
        eapp_media_reset();
        open_page(tracks_from, tracks_from == ARTISTS ? "ARTISTS" : "PLAYLISTS");
    } else {
        open_page(HOME, "LIBRARY");
    }
}

static void input(const struct eapp_input_event *event)
{
    if (event->type != EAPP_INPUT_PRESS) {
        if (page != PLAYERS || event->button != EAPP_BUTTON_PLAY)
            eapp_music_pass_button(event);
        return;
    }
    if (event->button == EAPP_BUTTON_MENU)
        back();
    else if (event->button == EAPP_BUTTON_CENTER)
        choose();
    else if (event->button == EAPP_BUTTON_PLAY && page == PLAYERS && selected + 1 < count) {
        eapp_user_remove(eapp_user_id(selected));
        open_page(PLAYERS, "PLAYERS");
    }
}

static void init(void)
{
    open_page(HOME, "LIBRARY");
}

static void frame(const struct eapp_frame *frame)
{
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    int32_t steps = eapp_wheel_steps_consume(&wheel_steps, frame, 4);
    for (; steps > 0 && selected + 1 < count; --steps)
        ++selected;
    for (; steps < 0 && selected; ++steps)
        --selected;
    if (selected < first)
        first = selected;
    else if (selected >= first + ROWS)
        first = selected - ROWS + 1;

    eapp_clear(fb, 0);
    eapp_draw_text(fb, 10, 10, title, 0xffff, 2);
    if (page == PHOTOS) {
        load_picture();
        if (picture) {
            const struct eapp_bitmap *bitmap = eapp_media_picture_bitmap(picture);
            eapp_draw_bitmap(fb, (int)(fb->width - bitmap->width) / 2,
                             (int)(fb->height - bitmap->height) / 2, bitmap);
        }
        char text[25];
        char *end = number(text, count ? selected + 1 : 0);
        copy(end, 5, " OF ");
        number(end + 4, count);
        eapp_draw_text(fb, 10, fb->height - 14, count ? text : "NO PHOTOS", 0xffff, 1);
        return;
    }
    load_rows();
    int list_width = page == TRACKS ? 196 : 300;
    for (uint32_t i = 0; i < ROWS && first + i < count; ++i) {
        int y = TOP + (int)i * ROW_HEIGHT;
        if (first + i == selected)
            eapp_fill_rect(fb, 6, y - 4, list_width, ROW_HEIGHT - 2, 0x3186);
        /* Text cells are 6 pixels wide. */
        char shown[49];
        copy(shown, (size_t)(list_width - 12) / 6 + 1, rows[i]);
        eapp_draw_text(fb, 10, y, shown, 0xffff, 1);
    }
    if (!count)
        eapp_draw_text(fb, 10, TOP, "NOTHING HERE", 0x8410, 1);
    if (page == TRACKS) {
        load_picture();
        if (picture)
            eapp_draw_bitmap(fb, 210, TOP, eapp_media_picture_bitmap(picture));
        eapp_draw_text(fb, 210, TOP + COVER + 8, detail, 0xffff, 2);
    }
    if (page == PLAYERS)
        eapp_draw_text(fb, 10, fb->height - 14, "CENTER: ADD   PLAY: REMOVE", 0x8410, 1);
}

static void shutdown(void)
{
    drop_picture();
    eapp_media_playlist_close(playlist);
}

const struct eapp eapp_application = {
    .init = init, .input = input, .frame = frame,
    .shutdown = shutdown, .handles_music_buttons = 1,
};
