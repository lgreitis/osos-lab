/* SPDX-License-Identifier: GPL-3.0-only */

/* Host stand-ins for the games' player list that users.c drives and the
 * Metadata and Audio entries media.c and media_image.c drive, and the rules
 * they keep: players are found by id, never by a pointer the app holds;
 * names the host could not read back are refused; cut text stays whole
 * UTF-8 characters; a list is read only after the host built it for the
 * current selection; every host object the runtime opens for itself is
 * destroyed again; only plain RGB565 thumbnails become bitmaps; a queued
 * track carries its own duration. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "eapp_media.h"
#include "eapp_draw.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The player list: a vector of records, ids from a counter that starts at 0
 * (the host's first id is not known, so 0 must work as an id). */
struct GameUser {
    char name[128];
    uint16_t id;
};

static struct GameUser user_records[8];
static struct GameUser *users[8];
static uint32_t user_count;
static uint16_t next_user_id;

uint32_t GameUser_GetCount(void)
{
    return user_count;
}

struct GameUser *GameUser_GetAt(uint32_t index)
{
    return index < user_count ? users[index] : NULL;
}

struct GameUser *GameUser_FindById(uint32_t id)
{
    for (uint32_t i = 0; i < user_count; ++i)
        if (users[i]->id == id)
            return users[i];
    return NULL;
}

void GameUser_Remove(struct GameUser *user)
{
    for (uint32_t i = 0; i < user_count; ++i)
        if (users[i] == user) {
            memmove(&users[i], &users[i + 1], (user_count - i - 1) * sizeof users[0]);
            --user_count;
            return;
        }
    assert(!"removed a player not on the list");
}

struct GameUser *GameUser_Add(const char *name)
{
    assert(strlen(name) < sizeof user_records[0].name);
    struct GameUser *user = &user_records[next_user_id];
    strcpy(user->name, name);
    user->id = next_user_id++;
    users[user_count++] = user;
    return user;
}

const char *GameUser_GetName(struct GameUser *user)
{
    return user->name;
}

uint16_t GameUser_GetId(struct GameUser *user)
{
    return user->id;
}

void GameUser_SetName(struct GameUser *user, const char *name)
{
    assert(strlen(name) < sizeof user->name);
    strcpy(user->name, name);
}

static void check_users(void)
{
    char name[EAPP_USER_NAME_SIZE];
    assert(eapp_user_count() == 0 && eapp_user_id(0) == -1);
    assert(eapp_user_name(0, name, sizeof name) == -1);

    int32_t anna = eapp_user_add("Anna");
    int32_t ben = eapp_user_add("Ben");
    assert(anna == 0 && ben == 1 && eapp_user_count() == 2);
    assert(eapp_user_id(0) == anna && eapp_user_id(1) == ben && eapp_user_id(2) == -1);
    assert(eapp_user_name(anna, name, sizeof name) == 4 && !strcmp(name, "Anna"));

    /* Names the host would cut when it loads the list are refused. */
    char long_name[EAPP_USER_NAME_SIZE + 1];
    memset(long_name, 'x', sizeof long_name - 1);
    long_name[EAPP_USER_NAME_SIZE] = 0;
    assert(eapp_user_add(long_name) == -1 && eapp_user_add("") == -1);
    assert(eapp_user_add(NULL) == -1);
    long_name[EAPP_USER_NAME_SIZE - 1] = 0;
    int32_t longest = eapp_user_add(long_name);
    assert(longest == 2 && eapp_user_count() == 3);
    assert(eapp_user_rename(ben, long_name) == 0);
    long_name[EAPP_USER_NAME_SIZE - 1] = 'x';
    assert(eapp_user_rename(ben, long_name) == -1 && eapp_user_rename(ben, "") == -1);

    /* A cut name ends before the character the cut went through. */
    assert(eapp_user_rename(anna, "J\xc3\xb6rg") == 0);
    char small[3];
    assert(eapp_user_name(anna, small, sizeof small) == 5 && !strcmp(small, "J"));
    char fits[4];
    assert(eapp_user_name(anna, fits, sizeof fits) == 5 && !strcmp(fits, "J\xc3\xb6"));
    assert(eapp_user_name(anna, name, 0) == -1 && eapp_user_name(anna, NULL, 4) == -1);

    /* Removing moves the indexes, not the ids. */
    assert(eapp_user_remove(anna) == 0 && eapp_user_remove(anna) == -1);
    assert(eapp_user_count() == 2 && eapp_user_id(0) == ben && eapp_user_id(1) == longest);
    assert(eapp_user_name(anna, name, sizeof name) == -1);
    assert(eapp_user_rename(anna, "Anna") == -1 && eapp_user_remove(-1) == -1);
}

/* The app heap. */
static unsigned live_blocks;

void *eapp_alloc(size_t size)
{
    ++live_blocks;
    return malloc(size);
}

void eapp_free(void *pointer)
{
    if (pointer)
        --live_blocks;
    free(pointer);
}

/* The host's handle tables, one vector for every kind: a handle is the index,
 * a destroyed slot is never reused, so a stale handle stays detectable. */
enum kind { LIBRARY = 1, FILTER, TRACK, PLAYLIST, ARTWORK, PHOTOS, ALBUM, IMAGE, DATA };
static struct object {
    enum kind kind;
    int live;
    int32_t value; /* A filter's list << 8 | index, a track, playlist, image id or format. */
} objects[512];
static int32_t object_count;

static int32_t new_object(enum kind kind, int32_t value)
{
    assert(object_count < (int32_t)(sizeof objects / sizeof objects[0]));
    objects[object_count] = (struct object){kind, 1, value};
    return object_count++;
}

static struct object *object(int32_t handle, enum kind kind)
{
    return handle >= 0 && handle < object_count && objects[handle].live &&
                   objects[handle].kind == kind
               ? &objects[handle]
               : NULL;
}

static void destroy(int32_t handle, enum kind kind)
{
    struct object *o = object(handle, kind);
    assert(o);
    o->live = 0;
}

static unsigned live(enum kind kind)
{
    unsigned n = 0;
    for (int32_t i = 0; i < object_count; ++i)
        n += objects[i].live && objects[i].kind == kind;
    return n;
}

/* The library: five tracks by three artists and one playlist. Selecting an
 * artist narrows the tracks; a list is readable once counted. */
static const struct fake_track {
    const char *title, *path;
    int artist;
    uint32_t duration_ms;
    uint64_t id;
} tracks[] = {
    {"Alpha", "iPod_Control\\Music\\F00\\A.mp3", 0, 1000, 100},
    {"Beta", "iPod_Control\\Music\\F00\\B.mp3", 0, 2000, 101},
    {"Gamma", "iPod_Control\\Music\\F00\\C.mp3", 0, 3000, 102},
    {"Delta", "iPod_Control\\Music\\F00\\D.mp3", 1, 4000, 103},
    {"\xc3\x89psilon", "iPod_Control\\Music\\F00\\E.mp3", 2, 5000, 104},
    /* The file-only record the host makes for a queued path. */
    {"", "", -1, 0, 0},
};
static const char *const artists[] = {"Abba", "Beck", "Cher"};
static const int mix[] = {4, 0};
static int selected_artist = -1, selected_playlist = -1;
static int32_t selection[5];
static uint32_t selection_count, built_lists, count_calls;
static int32_t rating[5] = {40, 0, 0, 0, 0}, bookmark[5], plays[5], skips[5];
static int mix_count = 2;

/* The kinds the library lists; a new library keeps what the last menu set,
 * here the Videos menu, and Reset leaves them. */
static uint32_t include_kinds = GameMediaKind_Movie, exclude_kinds;

static void build_tracks(void)
{
    selection_count = 0;
    /* Every track of the stand-in is a song. */
    if (!(include_kinds & GameMediaKind_Music) || (exclude_kinds & GameMediaKind_Music))
        return;
    for (int i = 0; i < 5; ++i) {
        int in_playlist = selected_playlist < 0;
        for (int j = 0; j < mix_count && !in_playlist; ++j)
            in_playlist = mix[j] == i;
        if ((selected_artist < 0 || tracks[i].artist == selected_artist) && in_playlist)
            selection[selection_count++] = i;
    }
}

int32_t GameMetadata_LibraryCreate(void)
{
    /* A new library inherits what the Music menus left selected. */
    selected_artist = 2;
    return new_object(LIBRARY, 0);
}

void GameMetadata_LibraryReset(int32_t library)
{
    assert(object(library, LIBRARY));
    selected_artist = selected_playlist = -1;
    built_lists = 0;
}

/* The order selector each SetOrderMode entry writes; a reset keeps it. */
static int order_selector = -1;

#define SET_ORDER(n)                                                                           \
    void GameMetadata_LibrarySetOrderMode##n(int32_t library)                                  \
    {                                                                                          \
        assert(object(library, LIBRARY));                                                      \
        order_selector = n;                                                                    \
        built_lists = 0;                                                                       \
    }
SET_ORDER(1)
SET_ORDER(2)
SET_ORDER(3)
SET_ORDER(4)
SET_ORDER(5)

void GameMetadata_LibrarySetKindMasks(int32_t library, uint32_t include, uint32_t exclude)
{
    assert(object(library, LIBRARY));
    include_kinds = include;
    exclude_kinds = exclude;
    built_lists = 0;
}

#define COUNT(name, list, value)                                                               \
    uint32_t GameMetadata_LibraryGet##name##Count(int32_t library)                             \
    {                                                                                          \
        assert(object(library, LIBRARY));                                                      \
        ++count_calls;                                                                         \
        built_lists |= 1u << (list);                                                           \
        return (value);                                                                        \
    }

COUNT(Artist, EAPP_MEDIA_ARTISTS, 3)
COUNT(Album, EAPP_MEDIA_ALBUMS, 1)
COUNT(Genre, EAPP_MEDIA_GENRES, 1)
COUNT(Composer, EAPP_MEDIA_COMPOSERS, 1)
COUNT(TVShow, EAPP_MEDIA_TV_SHOWS, 1)
COUNT(Season, EAPP_MEDIA_SEASONS, 1)
COUNT(Playlist, EAPP_MEDIA_PLAYLISTS, 1)

uint32_t GameMetadata_LibraryGetTrackCount(int32_t library)
{
    assert(object(library, LIBRARY));
    ++count_calls;
    built_lists |= 1u << EAPP_MEDIA_TRACKS;
    build_tracks();
    return selection_count;
}

static int32_t value_at(int32_t library, enum eapp_media_list list, uint32_t index)
{
    assert(object(library, LIBRARY) && (built_lists & (1u << list)));
    return new_object(FILTER, (int32_t)(list << 8 | index));
}

#define VALUE_AT(name, list)                                                                   \
    int32_t GameMetadata_LibraryGet##name##At(int32_t library, uint32_t index)                 \
    {                                                                                          \
        return value_at(library, list, index);                                                 \
    }

VALUE_AT(Artist, EAPP_MEDIA_ARTISTS)
VALUE_AT(Album, EAPP_MEDIA_ALBUMS)
VALUE_AT(Genre, EAPP_MEDIA_GENRES)
VALUE_AT(Composer, EAPP_MEDIA_COMPOSERS)
VALUE_AT(TVShow, EAPP_MEDIA_TV_SHOWS)

uint32_t GameMetadata_LibraryGetSeasonNumberAt(int32_t library, uint32_t index)
{
    assert(object(library, LIBRARY) && (built_lists & (1u << EAPP_MEDIA_SEASONS)));
    return index == 0 ? 2 : 0;
}

/* strncpy into the buffer, then the whole length, as the host does. */
static void host_copy(const char *text, char *buffer, uint32_t *inout_bytes)
{
    strncpy(buffer, text, *inout_bytes);
    *inout_bytes = (uint32_t)strlen(text);
}

void GameMetadata_FilterCopyName(int32_t filter, char *buffer, uint32_t *inout_bytes)
{
    struct object *o = object(filter, FILTER);
    assert(o);
    int list = o->value >> 8, index = o->value & 0xff;
    host_copy(list == EAPP_MEDIA_ARTISTS ? artists[index] : "Other", buffer, inout_bytes);
}

void GameMetadata_FilterDestroy(int32_t filter)
{
    destroy(filter, FILTER);
}

static void select_value(int32_t library, int32_t filter, enum eapp_media_list list)
{
    struct object *o = object(filter, FILTER);
    assert(object(library, LIBRARY) && o && o->value >> 8 == (int)list);
    if (list == EAPP_MEDIA_ARTISTS)
        selected_artist = o->value & 0xff;
    built_lists = 0;
}

#define SELECT(name, list)                                                                     \
    void GameMetadata_LibrarySelect##name(int32_t library, int32_t filter)                     \
    {                                                                                          \
        select_value(library, filter, list);                                                   \
    }

SELECT(Artist, EAPP_MEDIA_ARTISTS)
SELECT(Album, EAPP_MEDIA_ALBUMS)
SELECT(Genre, EAPP_MEDIA_GENRES)
SELECT(Composer, EAPP_MEDIA_COMPOSERS)
SELECT(TVShow, EAPP_MEDIA_TV_SHOWS)

void GameMetadata_LibrarySelectSeasonAt(int32_t library, uint32_t index)
{
    assert(object(library, LIBRARY) && index == 0);
    built_lists = 0;
}

void GameMetadata_LibrarySelectPlaylist(int32_t library, int32_t playlist)
{
    assert(object(library, LIBRARY) && object(playlist, PLAYLIST));
    selected_playlist = 0;
    built_lists = 0;
}

int32_t GameMetadata_LibraryGetTrackAt(int32_t library, uint32_t index)
{
    assert(object(library, LIBRARY) && (built_lists & (1u << EAPP_MEDIA_TRACKS)));
    return index < selection_count ? new_object(TRACK, selection[index]) : -1;
}

int32_t GameMetadata_LibraryFindTrack(int32_t library, uint64_t persistent_id)
{
    assert(object(library, LIBRARY));
    for (int i = 0; i < 5; ++i)
        if (tracks[i].id == persistent_id)
            return new_object(TRACK, i);
    return -1;
}

int32_t GameMetadata_LibraryGetPlaylistAt(int32_t library, uint32_t index)
{
    assert(object(library, LIBRARY) && (built_lists & (1u << EAPP_MEDIA_PLAYLISTS)));
    return index == 0 ? new_object(PLAYLIST, 0) : -1;
}

int32_t GameMetadata_LibraryFindPlaylist(int32_t library, uint64_t persistent_id)
{
    assert(object(library, LIBRARY));
    return persistent_id == 900 ? new_object(PLAYLIST, 0) : -1;
}

int32_t GameMetadata_LibraryGetAlbumImage(int32_t library, uint32_t index)
{
    assert(object(library, LIBRARY) && (built_lists & (1u << EAPP_MEDIA_ALBUMS)));
    return index == 0 ? new_object(IMAGE, 7) : -1;
}

static const struct fake_track *track(int32_t handle)
{
    struct object *o = object(handle, TRACK);
    return o ? &tracks[o->value] : NULL;
}

static int track_index(int32_t handle)
{
    struct object *o = object(handle, TRACK);
    assert(o);
    return o->value;
}

void GameMetadata_TrackDestroy(int32_t handle)
{
    destroy(handle, TRACK);
}

/* The host's validity checks: an open object of the type, unless the
 * database moved on. */
static int database_moved;

int GameMetadata_TrackIsValid(int32_t handle)
{
    return !database_moved && object(handle, TRACK) != NULL;
}

int GameMetadata_PlaylistIsValid(int32_t handle)
{
    return !database_moved && object(handle, PLAYLIST) != NULL;
}

void GameMetadata_TrackCopyTitle(int32_t handle, char *buffer, uint32_t *inout_bytes)
{
    if (track(handle))
        host_copy(track(handle)->title, buffer, inout_bytes);
    else
        *inout_bytes = 0;
}

void GameMetadata_TrackCopyArtist(int32_t handle, char *buffer, uint32_t *inout_bytes)
{
    host_copy(artists[track(handle)->artist], buffer, inout_bytes);
}

/* The track whose path comes back empty, or -1. */
static int empty_path = -1;

void GameMetadata_TrackCopyPath(int32_t handle, char *buffer, uint32_t *inout_bytes)
{
    host_copy(track_index(handle) == empty_path ? "" : track(handle)->path, buffer, inout_bytes);
}

#define EMPTY_TEXT(name)                                                                       \
    void GameMetadata_TrackCopy##name(int32_t handle, char *buffer, uint32_t *inout_bytes)     \
    {                                                                                          \
        assert(track(handle));                                                                 \
        host_copy("", buffer, inout_bytes);                                                    \
    }

EMPTY_TEXT(Album)
EMPTY_TEXT(Genre)
EMPTY_TEXT(Composer)
EMPTY_TEXT(Grouping)
EMPTY_TEXT(FileType)
EMPTY_TEXT(Comment)
EMPTY_TEXT(Category)
EMPTY_TEXT(Description)
EMPTY_TEXT(ShowName)
EMPTY_TEXT(PodcastRssUrl)

uint32_t GameMetadata_TrackGetDurationMs(int32_t handle)
{
    return track(handle)->duration_ms;
}

uint64_t GameMetadata_TrackGetPersistentId(int32_t handle)
{
    return track(handle)->id;
}

int32_t GameMetadata_TrackGetRating(int32_t handle)
{
    return rating[track_index(handle)];
}

void GameMetadata_TrackSetRating(int32_t handle, int32_t value)
{
    assert(value >= 0 && value <= 100);
    rating[track_index(handle)] = value;
}

uint32_t GameMetadata_TrackGetBookmarkMs(int32_t handle)
{
    return (uint32_t)bookmark[track_index(handle)];
}

void GameMetadata_TrackSetBookmarkMs(int32_t handle, uint32_t milliseconds)
{
    bookmark[track_index(handle)] = (int32_t)milliseconds;
}

uint32_t GameMetadata_TrackGetPlayCount(int32_t handle)
{
    return (uint32_t)plays[track_index(handle)];
}

void GameMetadata_TrackIncrementPlayCount(int32_t handle)
{
    ++plays[track_index(handle)];
}

uint32_t GameMetadata_TrackGetSkipCount(int32_t handle)
{
    return (uint32_t)skips[track_index(handle)];
}

void GameMetadata_TrackIncrementSkipCount(int32_t handle)
{
    ++skips[track_index(handle)];
}

#define ZERO(type, name)                                                                       \
    type GameMetadata_Track##name(int32_t handle)                                              \
    {                                                                                          \
        assert(track(handle));                                                                 \
        return 0;                                                                              \
    }

ZERO(uint32_t, GetStartTimeMs)
ZERO(uint32_t, GetStopTimeMs)
ZERO(uint32_t, GetFileSizeBytes)
ZERO(uint16_t, GetBitrateKbps)
ZERO(uint16_t, GetTrackNumber)
ZERO(uint16_t, GetDiscNumber)
ZERO(uint32_t, GetSeasonNumber)
ZERO(int, IsVideo)
ZERO(int, IsTVShow)
ZERO(int, IsMusicVideo)

uint16_t GameMetadata_TrackGetYear(int32_t handle)
{
    return track(handle) ? 1999 : 0;
}

int GameMetadata_TrackIsAudiobook(int32_t handle)
{
    return track_index(handle) == 3;
}

int GameMetadata_TrackRemembersPosition(int32_t handle)
{
    return track_index(handle) == 3;
}

/* A DateTime: second, minute, hour, day, month, then the year as a halfword. */
static void fake_date(struct DateTime *out, uint8_t day)
{
    uint8_t *record = (uint8_t *)out;
    memset(record, 0xee, DateTime_bytes);
    record[DateTime_second] = 5;
    record[DateTime_minute] = 4;
    record[DateTime_hour] = 3;
    record[DateTime_day] = day;
    record[DateTime_month] = 11;
    record[DateTime_year] = 2026 & 0xff;
    record[DateTime_year + 1] = 2026 >> 8;
}

#define DATE(name, day)                                                                        \
    void GameMetadata_TrackGet##name(struct DateTime *out, int32_t handle)                     \
    {                                                                                          \
        assert(track(handle));                                                                 \
        fake_date(out, day);                                                                   \
    }

DATE(LastPlayed, 1)
DATE(LastSkipped, 2)
DATE(DateAdded, 3)
DATE(DateModified, 4)

int GameMetadata_TrackGetReleaseDate(int32_t handle, struct DateTime *out)
{
    if (track_index(handle) != 0)
        return 0;
    fake_date(out, 9);
    return 1;
}

void GameMetadata_PlaylistDestroy(int32_t playlist)
{
    destroy(playlist, PLAYLIST);
}

uint16_t GameMetadata_PlaylistGetCount(int32_t playlist)
{
    assert(object(playlist, PLAYLIST));
    return (uint16_t)mix_count;
}

void GameMetadata_PlaylistCopyName(int32_t playlist, char *buffer, uint32_t *inout_bytes)
{
    assert(object(playlist, PLAYLIST));
    host_copy("Mix", buffer, inout_bytes);
}

uint64_t GameMetadata_PlaylistGetPersistentId(int32_t playlist)
{
    return object(playlist, PLAYLIST) ? 900 : 0;
}

int32_t GameMetadata_PlaylistGetTrackAt(int32_t playlist, uint32_t index, uint32_t order_mode)
{
    assert(object(playlist, PLAYLIST) && order_mode == 0 && index < (uint32_t)mix_count);
    return new_object(TRACK, mix[index]);
}

int GameMetadata_PlaylistRemoveEntryAt(int32_t playlist, uint32_t index, uint32_t order_mode,
                                       int reverse)
{
    assert(object(playlist, PLAYLIST) && order_mode == 0 && !reverse && index == 0);
    mix_count = 1;
    return 1;
}

static unsigned refreshes;

int GameMetadata_PlaylistRefresh(int32_t playlist)
{
    assert(object(playlist, PLAYLIST));
    ++refreshes;
    return 1;
}

/* The playback list: what was queued, with the durations given, and the
 * host's mark that the list changed, which every edit sets. */
static struct {
    unsigned clears;
    uint8_t changed;
    char paths[8][64];
    uint32_t durations[8];
    unsigned count;
} playback;

void GameAudio_ClearPlaybackList(void)
{
    ++playback.clears;
    playback.count = 0;
    playback.changed = 1;
}

void GameAudio_SetPlaybackChanged(uint8_t changed)
{
    playback.changed = changed;
}

void GameAudio_RemoveEntryAtIndex(uint32_t index)
{
    assert(index < playback.count);
    for (unsigned i = index; i + 1 < playback.count; ++i) {
        strcpy(playback.paths[i], playback.paths[i + 1]);
        playback.durations[i] = playback.durations[i + 1];
    }
    --playback.count;
    playback.changed = 1;
}

void GameAudio_AppendMediaSelection(const char *path, void *record, uint8_t use_record,
                                    uint32_t duration_ms)
{
    assert(path && !record && !use_record && playback.count < 8);
    strcpy(playback.paths[playback.count], path);
    playback.durations[playback.count++] = duration_ms;
    playback.changed = 1;
}

uint32_t GameAudio_GetEntryCount(void)
{
    return playback.count;
}

/* Entry `index` as a Track of the same table: a queued path's record knows
 * no title or persistent id. */
int32_t GameAudio_GetItemHandleAtIndex(uint32_t index)
{
    assert(index < playback.count);
    return new_object(TRACK, 5);
}

/* Artwork: track 100 and album 0 have image 7 with thumbnails of three
 * formats; 1020 is byte-swapped and must not become a bitmap. A format
 * outside the host's table stops the iPod. */
static uint16_t thumb_pixels[64];

int32_t GameMetadata_ArtworkLibraryCreate(void)
{
    return new_object(ARTWORK, 0);
}

int32_t GameMetadata_ArtworkLibraryGetTrackImage(int32_t artwork_library, uint64_t persistent_id)
{
    assert(object(artwork_library, ARTWORK));
    return persistent_id == 100 ? new_object(IMAGE, 7) : -1;
}

void GameMetadata_ImageDestroy(int32_t image)
{
    destroy(image, IMAGE);
}

uint32_t GameMetadata_ImageGetId(int32_t image)
{
    struct object *o = object(image, IMAGE);
    return o ? (uint32_t)o->value : 0;
}

static int in_table(int16_t id)
{
    static const int16_t table[] = {
        ArtworkFormatId_1009, ArtworkFormatId_1013, ArtworkFormatId_1015, ArtworkFormatId_1016,
        ArtworkFormatId_1017, ArtworkFormatId_1020, ArtworkFormatId_1021, ArtworkFormatId_1023,
        ArtworkFormatId_1024, ArtworkFormatId_1027, ArtworkFormatId_1028, ArtworkFormatId_1029,
        ArtworkFormatId_1031, ArtworkFormatId_1032, ArtworkFormatId_1035, ArtworkFormatId_1036,
        ArtworkFormatId_1050, ArtworkFormatId_1051, ArtworkFormatId_1052, ArtworkFormatId_1053,
        ArtworkFormatId_1054, ArtworkFormatId_1055, ArtworkFormatId_1060, ArtworkFormatId_1061,
        ArtworkFormatId_1065, ArtworkFormatId_1066, ArtworkFormatId_1068, ArtworkFormatId_1069,
    };
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; ++i)
        if (table[i] == id)
            return 1;
    return 0;
}

int32_t GameMetadata_ImageCountThumbs(int32_t image, int16_t format_id)
{
    assert(object(image, IMAGE));
    return format_id == ArtworkFormatId_1055 || format_id == ArtworkFormatId_1061 ||
           format_id == ArtworkFormatId_1020;
}

static void format_size(int16_t id, uint32_t *width, uint32_t *height, uint32_t *row_bytes)
{
    *width = id == ArtworkFormatId_1055 ? 4 : id == ArtworkFormatId_1061 ? 2 : 6;
    *height = *width;
    *row_bytes = id == ArtworkFormatId_1061 ? 6 : *width * 2;
}

void GameMetadata_ArtworkLibraryGetFormat(struct GameImageFormat *out, int32_t artwork_library,
                                          int16_t format_id)
{
    assert(object(artwork_library, ARTWORK) && in_table(format_id));
    uint8_t *record = (uint8_t *)out;
    uint32_t width, height, row_bytes, bits = 16;
    format_size(format_id, &width, &height, &row_bytes);
    /* The host leaves the bytes after the pixel format unwritten. */
    memset(record, 0x5a, GameImageFormat_bytes);
    memcpy(record + GameImageFormat_width, &width, 4);
    memcpy(record + GameImageFormat_height, &height, 4);
    memcpy(record + GameImageFormat_row_bytes, &row_bytes, 4);
    memcpy(record + GameImageFormat_bits_per_pixel, &bits, 4);
    record[GameImageFormat_pixel_format] = format_id == ArtworkFormatId_1020
                                               ? GameImagePixelFormat_RGB565ByteSwapped
                                               : GameImagePixelFormat_RGB565;
}

/* With thumb_missing set the load fails and leaves the header unset. */
static int thumb_missing;

int32_t GameMetadata_ImageGetThumbAt(int32_t image, int16_t format_id, int load, uint32_t index)
{
    assert(object(image, IMAGE) && load && index == 0);
    return new_object(DATA, format_id);
}

const void *GameMetadata_ImageDataGetLoadedHeader(int32_t image_data)
{
    assert(object(image_data, DATA));
    return thumb_missing ? NULL : thumb_pixels;
}

const void *GameMetadata_ImageDataGetPixels(int32_t image_data)
{
    assert(object(image_data, DATA));
    return thumb_pixels;
}

uint32_t GameMetadata_ImageDataGetByteCount(int32_t image_data)
{
    struct object *o = object(image_data, DATA);
    uint32_t width, height, row_bytes;
    format_size((int16_t)o->value, &width, &height, &row_bytes);
    return row_bytes * height;
}

void GameMetadata_ImageDataDestroy(int32_t image_data)
{
    destroy(image_data, DATA);
}

/* Photos: one album "Trip" of two images (ids 20, 21) and a third image
 * outside it. */
int32_t GameMetadata_PhotoLibraryCreate(void)
{
    return new_object(PHOTOS, 0);
}

uint32_t GameMetadata_PhotoLibraryGetAlbumCount(int32_t photo_library)
{
    assert(object(photo_library, PHOTOS));
    return 1;
}

uint32_t GameMetadata_PhotoLibraryGetImageCount(int32_t photo_library)
{
    assert(object(photo_library, PHOTOS));
    return 3;
}

int32_t GameMetadata_PhotoLibraryFindAlbum(int32_t photo_library, const char *name)
{
    assert(object(photo_library, PHOTOS));
    return strcmp(name, "Trip") ? -1 : 0;
}

int32_t GameMetadata_PhotoLibraryGetAlbumAt(int32_t photo_library, uint32_t index)
{
    assert(object(photo_library, PHOTOS) && index == 0);
    return new_object(ALBUM, 0);
}

int32_t GameMetadata_PhotoLibraryGetImageAt(int32_t photo_library, uint32_t index)
{
    assert(object(photo_library, PHOTOS) && index < 3);
    return new_object(IMAGE, 20 + (int32_t)index);
}

int32_t GameMetadata_PhotoLibraryGetImageById(int32_t photo_library, uint32_t artwork_id)
{
    assert(object(photo_library, PHOTOS));
    return artwork_id >= 20 && artwork_id <= 22 ? new_object(IMAGE, (int32_t)artwork_id) : -1;
}

void GameMetadata_PhotoAlbumDestroy(int32_t album)
{
    destroy(album, ALBUM);
}

void GameMetadata_PhotoAlbumCopyName(int32_t album, char *buffer, uint32_t *inout_bytes)
{
    assert(object(album, ALBUM));
    host_copy("Trip", buffer, inout_bytes);
}

uint32_t GameMetadata_PhotoAlbumGetImageCount(int32_t album)
{
    assert(object(album, ALBUM));
    return 2;
}

int32_t GameMetadata_PhotoAlbumGetImageAt(int32_t album, uint32_t index)
{
    assert(object(album, ALBUM) && index < 2);
    return new_object(IMAGE, 20 + (int32_t)index);
}

static void check_library(void)
{
    char text[EAPP_MEDIA_TEXT_SIZE];

    /* The first query resets what the menus left selected and lists songs,
     * whatever kinds the last menu showed. */
    assert(eapp_media_count(EAPP_MEDIA_TRACKS) == 5 && selected_artist == -1);
    assert(include_kinds == GameMediaKind_Music && !(exclude_kinds & GameMediaKind_Music) &&
           (exclude_kinds & GameMediaKind_Podcast) && (exclude_kinds & GameMediaKind_Audiobook) &&
           (exclude_kinds & GameMediaKind_ITunesU));
    assert(eapp_media_count(EAPP_MEDIA_ARTISTS) == 3);
    unsigned calls = count_calls;
    assert(eapp_media_count(EAPP_MEDIA_ARTISTS) == 3 && count_calls == calls);
    assert(eapp_media_name(EAPP_MEDIA_ARTISTS, 1, text, sizeof text) == 4);
    assert(!strcmp(text, "Beck"));
    assert(eapp_media_name(EAPP_MEDIA_ARTISTS, 3, text, sizeof text) == -1);
    assert(eapp_media_name(EAPP_MEDIA_SEASONS, 0, text, sizeof text) == -1);
    assert(eapp_media_name(EAPP_MEDIA_TRACKS, 4, text, sizeof text) == 8);
    assert(!strcmp(text, "\xc3\x89psilon"));
    assert(eapp_media_name(EAPP_MEDIA_PLAYLISTS, 0, text, sizeof text) == 3);
    assert(eapp_media_season(0) == 2 && eapp_media_season(1) == 0);
    /* A cut name ends before the character the cut went through. */
    char small[2];
    assert(eapp_media_name(EAPP_MEDIA_TRACKS, 4, small, sizeof small) == 8 && !small[0]);
    assert(eapp_media_name(EAPP_MEDIA_TRACKS, 0, small, sizeof small) == 5);
    assert(!strcmp(small, "A"));
    assert(live(FILTER) == 0 && live(TRACK) == 0 && live(PLAYLIST) == 0);

    /* Selecting narrows the tracks and makes every list stale. */
    assert(eapp_media_select(EAPP_MEDIA_TRACKS, 0) == -1);
    assert(eapp_media_select(EAPP_MEDIA_ARTISTS, 3) == -1);
    assert(eapp_media_select(EAPP_MEDIA_ARTISTS, 0) == 0 && selected_artist == 0);
    assert(built_lists == 0 && eapp_media_count(EAPP_MEDIA_TRACKS) == 3);
    assert(eapp_media_select(EAPP_MEDIA_SEASONS, 0) == 0);
    assert(eapp_media_select(EAPP_MEDIA_PLAYLISTS, 0) == 0 && selected_playlist == 0);
    assert(eapp_media_count(EAPP_MEDIA_TRACKS) == 1);
    eapp_media_reset();
    assert(selected_artist == -1 && eapp_media_count(EAPP_MEDIA_TRACKS) == 5);
    assert(live(FILTER) == 0 && live(PLAYLIST) == 0);

    /* Tracks: fields, dates, kinds and writes. */
    assert(!eapp_media_track_open(5));
    struct eapp_media_track *alpha = eapp_media_track_open(0);
    struct eapp_media_track *delta = eapp_media_track_find(103);
    assert(alpha && delta);
    assert(!eapp_media_track_find(999));
    assert(live(TRACK) == 2);
    assert(eapp_media_track_title(alpha, text, sizeof text) == 5 && !strcmp(text, "Alpha"));
    assert(eapp_media_track_artist(delta, text, sizeof text) == 4 && !strcmp(text, "Beck"));
    assert(eapp_media_track_album(alpha, text, sizeof text) == 0 && !text[0]);
    assert(eapp_media_track_path(alpha, text, sizeof text) > 0);
    assert(eapp_media_track_title(NULL, text, sizeof text) == -1);
    assert(eapp_media_track_title(alpha, text, 0) == -1);
    assert(eapp_media_track_id(alpha) == 100 && eapp_media_track_duration_ms(delta) == 4000);
    assert(eapp_media_track_year(alpha) == 1999 && eapp_media_track_year(NULL) == 0);
    assert(eapp_media_track_kind(alpha) == 0);
    assert(eapp_media_track_kind(delta) == EAPP_MEDIA_KIND_AUDIOBOOK);
    assert(eapp_media_track_remembers_position(delta) &&
           !eapp_media_track_remembers_position(alpha));
    struct eapp_date_time when;
    eapp_media_track_added(alpha, &when);
    assert(when.year == 2026 && when.month == 11 && when.day == 3 && when.hour == 3 &&
           when.minute == 4 && when.second == 5);
    eapp_media_track_last_played(alpha, &when);
    assert(when.day == 1);
    assert(eapp_media_track_released(alpha, &when) == 0 && when.day == 9);
    assert(eapp_media_track_released(delta, &when) == -1);
    assert(eapp_media_track_rating(alpha) == 40);
    eapp_media_track_set_rating(alpha, 250);
    assert(eapp_media_track_rating(alpha) == EAPP_MEDIA_RATING_MAX);
    eapp_media_track_set_bookmark_ms(delta, 1234);
    eapp_media_track_count_play(delta);
    eapp_media_track_count_skip(delta);
    assert(eapp_media_track_bookmark_ms(delta) == 1234);
    assert(eapp_media_track_play_count(delta) == 1 && eapp_media_track_skip_count(delta) == 1);

    /* A new list takes tracks by path, each with its own duration; a track
     * without a path is refused. The entries come back as tracks. */
    eapp_music_clear_list();
    assert(playback.clears == 1 && playback.count == 0 && !eapp_music_track_open(0));
    assert(eapp_music_append(NULL) == -1);
    empty_path = 0;
    assert(eapp_music_append(alpha) == -1 && playback.count == 0);
    empty_path = -1;
    assert(eapp_music_append(delta) == 0 && eapp_music_append(alpha) == 0);
    assert(playback.count == 2 && playback.durations[0] == 4000 && playback.durations[1] == 1000);
    assert(!strcmp(playback.paths[1], tracks[0].path));
    struct eapp_media_track *entry = eapp_music_track_open(1);
    assert(entry && eapp_media_track_id(entry) == 0 && !eapp_music_track_open(2));
    assert(eapp_media_track_title(entry, text, sizeof text) == 0);
    eapp_media_track_close(entry);
    eapp_music_remove(2);
    eapp_music_remove(0);
    assert(playback.count == 1 && playback.durations[0] == 1000);
    assert(playback.changed);
    eapp_music_keep_list_on_exit();
    assert(!playback.changed);
    eapp_music_remove(0);
    assert(playback.changed && playback.count == 0);
    /* An object stands until the database moves on or it is closed. */
    assert(eapp_media_track_valid(alpha) && !eapp_media_track_valid(NULL));
    database_moved = 1;
    assert(!eapp_media_track_valid(alpha));
    database_moved = 0;
    eapp_media_track_close(alpha);
    assert(!eapp_media_track_valid(alpha));
    eapp_media_track_close(delta);
    eapp_media_track_close(NULL);
    assert(live(TRACK) == 0);

    /* Playlists. */
    assert(!eapp_media_playlist_open(1));
    struct eapp_media_playlist *list = eapp_media_playlist_open(0);
    struct eapp_media_playlist *found = eapp_media_playlist_find(900);
    assert(list && found && !eapp_media_playlist_find(1));
    assert(eapp_media_playlist_name(list, text, sizeof text) == 3 && !strcmp(text, "Mix"));
    assert(eapp_media_playlist_id(list) == 900);
    assert(eapp_media_playlist_track_count(list) == 2);
    struct eapp_media_track *first = eapp_media_playlist_track_open(list, 0);
    assert(first && eapp_media_track_id(first) == 104);
    assert(!eapp_media_playlist_track_open(list, 2));
    eapp_media_track_close(first);
    eapp_media_select_playlist(found);
    assert(selected_playlist == 0 && eapp_media_count(EAPP_MEDIA_TRACKS) == 2);
    static const int selectors[] = {5, 4, 1, 3, 2};
    for (int order = EAPP_MEDIA_ORDER_LIST; order <= EAPP_MEDIA_ORDER_GENRE; ++order) {
        eapp_media_set_order((enum eapp_media_order)order);
        assert(order_selector == selectors[order]);
    }
    eapp_media_set_order((enum eapp_media_order)5);
    assert(order_selector == 2);
    unsigned before = count_calls;
    eapp_media_set_order(EAPP_MEDIA_ORDER_LIST);
    assert(eapp_media_count(EAPP_MEDIA_TRACKS) == 2 && count_calls == before + 1);
    assert(eapp_media_playlist_remove_track(list, 5) == -1);
    assert(eapp_media_playlist_remove_track(list, 0) == 0);
    assert(eapp_media_playlist_track_count(list) == 1);
    assert(eapp_media_count(EAPP_MEDIA_TRACKS) == 1);
    eapp_media_playlist_refresh(list);
    assert(refreshes == 1);
    assert(eapp_media_playlist_valid(list) && !eapp_media_playlist_valid(NULL));
    eapp_media_playlist_close(list);
    assert(!eapp_media_playlist_valid(list));
    eapp_media_playlist_close(found);
    eapp_media_reset();
    assert(order_selector == 5);
    assert(live(PLAYLIST) == 0 && live(TRACK) == 0 && live(FILTER) == 0);
}

static void check_pictures(void)
{
    struct eapp_media_track *alpha = eapp_media_track_open(0);
    struct eapp_media_track *beta = eapp_media_track_open(1);
    assert(!eapp_media_artwork_find(eapp_media_track_id(beta)));
    struct eapp_media_image *cover = eapp_media_artwork_find(eapp_media_track_id(alpha));
    assert(cover && eapp_media_image_id(cover) == 7);

    /* The largest plain RGB565 thumbnail in the box; the byte-swapped 6 x 6
     * is never drawn. */
    struct eapp_media_picture *picture = eapp_media_image_load(cover, 10, 10);
    const struct eapp_bitmap *bitmap = eapp_media_picture_bitmap(picture);
    assert(bitmap && bitmap->width == 4 && bitmap->height == 4 && bitmap->stride_pixels == 4);
    assert(bitmap->pixels == thumb_pixels && bitmap->transparent == -1 && live(DATA) == 1);
    eapp_media_picture_free(picture);
    assert(live(DATA) == 0 && live_blocks == 0);
    /* None fits: the smallest, with its padded rows. */
    picture = eapp_media_image_load(cover, 1, 1);
    bitmap = eapp_media_picture_bitmap(picture);
    assert(bitmap->width == 2 && bitmap->stride_pixels == 3);
    eapp_media_picture_free(picture);
    eapp_media_picture_free(NULL);
    /* A thumb that fails to load gives no picture and leaks nothing. */
    thumb_missing = 1;
    assert(!eapp_media_image_load(cover, 10, 10) && live(DATA) == 0 && live_blocks == 0);
    thumb_missing = 0;
    assert(!eapp_media_image_load(NULL, 10, 10) && !eapp_media_picture_bitmap(NULL));
    eapp_media_image_close(cover);
    eapp_media_track_close(alpha);
    eapp_media_track_close(beta);

    assert(eapp_media_count(EAPP_MEDIA_ALBUMS) == 1);
    struct eapp_media_image *album = eapp_media_album_artwork_open(0);
    assert(album && !eapp_media_album_artwork_open(1));
    eapp_media_image_close(album);

    /* Photos, by album or across the library. */
    char name[16];
    assert(eapp_media_photo_album_count() == 1);
    assert(eapp_media_photo_album_name(0, name, sizeof name) == 4 && !strcmp(name, "Trip"));
    assert(eapp_media_photo_album_name(1, name, sizeof name) == -1);
    assert(eapp_media_photo_album_index("Trip") == 0 &&
           eapp_media_photo_album_index("x") == -1);
    assert(eapp_media_photo_count(-1) == 3 && eapp_media_photo_count(0) == 2);
    assert(eapp_media_photo_count(1) == 0);
    struct eapp_media_image *photo = eapp_media_photo_open(0, 1);
    assert(photo && eapp_media_image_id(photo) == 21 && !eapp_media_photo_open(0, 2));
    eapp_media_image_close(photo);
    photo = eapp_media_photo_open(-1, 2);
    assert(photo && eapp_media_image_id(photo) == 22 && !eapp_media_photo_open(-1, 3));
    eapp_media_image_close(photo);
    photo = eapp_media_photo_find(20);
    assert(photo && !eapp_media_photo_find(5));
    eapp_media_image_close(photo);
    assert(live(IMAGE) == 0 && live(ALBUM) == 0 && live(TRACK) == 0 && live(DATA) == 0);
}

int main(void)
{
    check_users();
    check_library();
    check_pictures();
    printf("player list, library and pictures passed\n");
    return 0;
}
