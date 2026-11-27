/* SPDX-License-Identifier: MIT */

/* The music library through the host's Metadata interface. Every host object
 * is a handle, an index into one of the host's tables; the app's pointers
 * carry the handle plus one, so NULL stays "none" and the runtime keeps no
 * table of its own. The host's library is a browser over the one the Music,
 * Videos and Podcasts menus use: it starts with whatever those left selected
 * and the kinds the last of them showed, which a reset keeps, so the runtime
 * sets the kinds, songs only, when it opens the library. The Music menus build
 * their own lists when they open, so a selection the app leaves does not reach
 * them. */

#include "osos-2.0.5.h"
#include "platform.h"

#define HANDLE EAPP_MEDIA_HANDLE
#define OBJECT EAPP_MEDIA_OBJECT

enum { LISTS = EAPP_MEDIA_PLAYLISTS + 1 };

#define OTHER_KINDS                                                                            \
    (GameMediaKind_Movie | GameMediaKind_Podcast | GameMediaKind_Audiobook |                   \
     GameMediaKind_MusicVideo | GameMediaKind_TVShow | GameMediaKind_Kind100 |                \
     GameMediaKind_Rental | GameMediaKind_ITunesU)

static int32_t library = -1;
/* A list's entries are readable once the host built it for the current
 * selection; building it is what counting does. */
static uint32_t built, counts[LISTS];

static int32_t open_library(void)
{
    if (library < 0) {
        library = GameMetadata_LibraryCreate();
        if (library >= 0) {
            GameMetadata_LibraryReset(library);
            GameMetadata_LibrarySetKindMasks(library, GameMediaKind_Music, OTHER_KINDS);
        }
        built = 0;
    }
    return library;
}

void eapp_media_reset(void)
{
    if (library >= 0)
        GameMetadata_LibraryReset(library);
    else
        open_library();
    built = 0;
}

uint32_t eapp_media_count(enum eapp_media_list list)
{
    if ((unsigned)list >= LISTS || open_library() < 0)
        return 0;
    if (built & (1u << list))
        return counts[list];
    static uint32_t (*const count[LISTS])(int32_t) = {
        [EAPP_MEDIA_ARTISTS] = GameMetadata_LibraryGetArtistCount,
        [EAPP_MEDIA_ALBUMS] = GameMetadata_LibraryGetAlbumCount,
        [EAPP_MEDIA_GENRES] = GameMetadata_LibraryGetGenreCount,
        [EAPP_MEDIA_COMPOSERS] = GameMetadata_LibraryGetComposerCount,
        [EAPP_MEDIA_TV_SHOWS] = GameMetadata_LibraryGetTVShowCount,
        [EAPP_MEDIA_SEASONS] = GameMetadata_LibraryGetSeasonCount,
        [EAPP_MEDIA_TRACKS] = GameMetadata_LibraryGetTrackCount,
        [EAPP_MEDIA_PLAYLISTS] = GameMetadata_LibraryGetPlaylistCount,
    };
    counts[list] = count[list](library);
    built |= 1u << list;
    return counts[list];
}

static int in_list(enum eapp_media_list list, uint32_t index)
{
    return index < eapp_media_count(list);
}

int32_t eapp_media_library(enum eapp_media_list list, uint32_t index)
{
    return in_list(list, index) ? library : -1;
}

/* The value lists answer a Filter, a name and the value's index. */
static int32_t (*const filter_at[EAPP_MEDIA_SEASONS])(int32_t, uint32_t) = {
    [EAPP_MEDIA_ARTISTS] = GameMetadata_LibraryGetArtistAt,
    [EAPP_MEDIA_ALBUMS] = GameMetadata_LibraryGetAlbumAt,
    [EAPP_MEDIA_GENRES] = GameMetadata_LibraryGetGenreAt,
    [EAPP_MEDIA_COMPOSERS] = GameMetadata_LibraryGetComposerAt,
    [EAPP_MEDIA_TV_SHOWS] = GameMetadata_LibraryGetTVShowAt,
};

/* A host Copy entry writes at most *inout bytes with strncpy, so a full
 * buffer has no terminator, and then stores the text's whole length. */
int eapp_media_copy_text(void (*copy)(int32_t, char *, uint32_t *), int32_t handle, char *out,
                         size_t capacity)
{
    if (handle < 0 || !out || capacity == 0)
        return -1;
    uint32_t length = capacity > 0x7fffffff ? 0x7fffffff : (uint32_t)capacity;
    out[0] = 0;
    copy(handle, out, &length);
    return eapp_text_finish(out, capacity, length < capacity ? length : capacity, length);
}

int eapp_media_name(enum eapp_media_list list, uint32_t index, char *out, size_t capacity)
{
    if ((unsigned)list >= LISTS || list == EAPP_MEDIA_SEASONS || !in_list(list, index))
        return -1;
    int32_t handle;
    void (*destroy)(int32_t);
    void (*copy)(int32_t, char *, uint32_t *);
    if (list == EAPP_MEDIA_TRACKS) {
        handle = GameMetadata_LibraryGetTrackAt(library, index);
        copy = GameMetadata_TrackCopyTitle;
        destroy = GameMetadata_TrackDestroy;
    } else if (list == EAPP_MEDIA_PLAYLISTS) {
        handle = GameMetadata_LibraryGetPlaylistAt(library, index);
        copy = GameMetadata_PlaylistCopyName;
        destroy = GameMetadata_PlaylistDestroy;
    } else {
        handle = filter_at[list](library, index);
        copy = GameMetadata_FilterCopyName;
        destroy = GameMetadata_FilterDestroy;
    }
    if (handle < 0)
        return -1;
    int length = eapp_media_copy_text(copy, handle, out, capacity);
    destroy(handle);
    return length;
}

uint32_t eapp_media_season(uint32_t index)
{
    return in_list(EAPP_MEDIA_SEASONS, index)
               ? GameMetadata_LibraryGetSeasonNumberAt(library, index)
               : 0;
}

int eapp_media_select(enum eapp_media_list list, uint32_t index)
{
    if ((unsigned)list >= LISTS || list == EAPP_MEDIA_TRACKS || !in_list(list, index))
        return -1;
    if (list == EAPP_MEDIA_SEASONS) {
        GameMetadata_LibrarySelectSeasonAt(library, index);
    } else if (list == EAPP_MEDIA_PLAYLISTS) {
        int32_t playlist = GameMetadata_LibraryGetPlaylistAt(library, index);
        if (playlist < 0)
            return -1;
        GameMetadata_LibrarySelectPlaylist(library, playlist);
        GameMetadata_PlaylistDestroy(playlist);
    } else {
        static void (*const select[EAPP_MEDIA_SEASONS])(int32_t, int32_t) = {
            [EAPP_MEDIA_ARTISTS] = GameMetadata_LibrarySelectArtist,
            [EAPP_MEDIA_ALBUMS] = GameMetadata_LibrarySelectAlbum,
            [EAPP_MEDIA_GENRES] = GameMetadata_LibrarySelectGenre,
            [EAPP_MEDIA_COMPOSERS] = GameMetadata_LibrarySelectComposer,
            [EAPP_MEDIA_TV_SHOWS] = GameMetadata_LibrarySelectTVShow,
        };
        int32_t filter = filter_at[list](library, index);
        if (filter < 0)
            return -1;
        select[list](library, filter);
        GameMetadata_FilterDestroy(filter);
    }
    built = 0;
    return 0;
}

void eapp_media_select_playlist(const struct eapp_media_playlist *playlist)
{
    if (playlist && open_library() >= 0) {
        GameMetadata_LibrarySelectPlaylist(library, HANDLE(playlist));
        built = 0;
    }
}

/* Each SetOrderMode entry writes its number to the browser's order selector,
 * which picks the sort fields of the tracks list. */
void eapp_media_set_order(enum eapp_media_order order)
{
    static void (*const set[])(int32_t) = {
        [EAPP_MEDIA_ORDER_LIST] = GameMetadata_LibrarySetOrderMode5,
        [EAPP_MEDIA_ORDER_TITLE] = GameMetadata_LibrarySetOrderMode4,
        [EAPP_MEDIA_ORDER_ARTIST] = GameMetadata_LibrarySetOrderMode1,
        [EAPP_MEDIA_ORDER_ALBUM] = GameMetadata_LibrarySetOrderMode3,
        [EAPP_MEDIA_ORDER_GENRE] = GameMetadata_LibrarySetOrderMode2,
    };
    if ((unsigned)order < sizeof set / sizeof set[0] && open_library() >= 0) {
        set[order](library);
        built = 0;
    }
}

struct eapp_media_track *eapp_media_track_open(uint32_t index)
{
    if (!in_list(EAPP_MEDIA_TRACKS, index))
        return NULL;
    return OBJECT(struct eapp_media_track, GameMetadata_LibraryGetTrackAt(library, index));
}

struct eapp_media_track *eapp_media_track_find(uint64_t id)
{
    if (open_library() < 0)
        return NULL;
    return OBJECT(struct eapp_media_track, GameMetadata_LibraryFindTrack(library, id));
}

void eapp_media_track_close(struct eapp_media_track *track)
{
    if (track)
        GameMetadata_TrackDestroy(HANDLE(track));
}

int eapp_media_track_valid(const struct eapp_media_track *track)
{
    return track && GameMetadata_TrackIsValid(HANDLE(track)) != 0;
}

int eapp_media_playlist_valid(const struct eapp_media_playlist *playlist)
{
    return playlist && GameMetadata_PlaylistIsValid(HANDLE(playlist)) != 0;
}

#define TRACK_TEXT(field, entry)                                                               \
    int eapp_media_track_##field(const struct eapp_media_track *track, char *out,              \
                                 size_t capacity)                                              \
    {                                                                                          \
        return track ? eapp_media_copy_text(entry, HANDLE(track), out, capacity) : -1;         \
    }

TRACK_TEXT(title, GameMetadata_TrackCopyTitle)
TRACK_TEXT(artist, GameMetadata_TrackCopyArtist)
TRACK_TEXT(album, GameMetadata_TrackCopyAlbum)
TRACK_TEXT(genre, GameMetadata_TrackCopyGenre)
TRACK_TEXT(composer, GameMetadata_TrackCopyComposer)
TRACK_TEXT(grouping, GameMetadata_TrackCopyGrouping)
TRACK_TEXT(file_type, GameMetadata_TrackCopyFileType)
TRACK_TEXT(comment, GameMetadata_TrackCopyComment)
TRACK_TEXT(category, GameMetadata_TrackCopyCategory)
TRACK_TEXT(description, GameMetadata_TrackCopyDescription)
TRACK_TEXT(show_name, GameMetadata_TrackCopyShowName)
TRACK_TEXT(podcast_url, GameMetadata_TrackCopyPodcastRssUrl)
TRACK_TEXT(path, GameMetadata_TrackCopyPath)

#define TRACK_NUMBER(field, entry)                                                             \
    uint32_t eapp_media_track_##field(const struct eapp_media_track *track)                    \
    {                                                                                          \
        return track ? (uint32_t)entry(HANDLE(track)) : 0;                                     \
    }

TRACK_NUMBER(duration_ms, GameMetadata_TrackGetDurationMs)
TRACK_NUMBER(bookmark_ms, GameMetadata_TrackGetBookmarkMs)
TRACK_NUMBER(start_ms, GameMetadata_TrackGetStartTimeMs)
TRACK_NUMBER(stop_ms, GameMetadata_TrackGetStopTimeMs)
TRACK_NUMBER(size_bytes, GameMetadata_TrackGetFileSizeBytes)
TRACK_NUMBER(bitrate_kbps, GameMetadata_TrackGetBitrateKbps)
TRACK_NUMBER(year, GameMetadata_TrackGetYear)
TRACK_NUMBER(number, GameMetadata_TrackGetTrackNumber)
TRACK_NUMBER(disc, GameMetadata_TrackGetDiscNumber)
TRACK_NUMBER(play_count, GameMetadata_TrackGetPlayCount)
TRACK_NUMBER(skip_count, GameMetadata_TrackGetSkipCount)
TRACK_NUMBER(season, GameMetadata_TrackGetSeasonNumber)

uint64_t eapp_media_track_id(const struct eapp_media_track *track)
{
    return track ? GameMetadata_TrackGetPersistentId(HANDLE(track)) : 0;
}

uint32_t eapp_media_track_rating(const struct eapp_media_track *track)
{
    int32_t rating = track ? GameMetadata_TrackGetRating(HANDLE(track)) : 0;
    return rating < 0 ? 0 : (uint32_t)rating;
}

uint32_t eapp_media_track_kind(const struct eapp_media_track *track)
{
    if (!track)
        return 0;
    int32_t handle = HANDLE(track);
    uint32_t kind = 0;
    if (GameMetadata_TrackIsAudiobook(handle))
        kind |= EAPP_MEDIA_KIND_AUDIOBOOK;
    if (GameMetadata_TrackIsVideo(handle))
        kind |= EAPP_MEDIA_KIND_VIDEO;
    if (GameMetadata_TrackIsTVShow(handle))
        kind |= EAPP_MEDIA_KIND_TV_SHOW;
    if (GameMetadata_TrackIsMusicVideo(handle))
        kind |= EAPP_MEDIA_KIND_MUSIC_VIDEO;
    return kind;
}

int eapp_media_track_remembers_position(const struct eapp_media_track *track)
{
    return track && GameMetadata_TrackRemembersPosition(HANDLE(track));
}

/* The host's DateTime record into the app's struct. */
static void date_time(const uint8_t *record, struct eapp_date_time *out)
{
    out->second = record[DateTime_second];
    out->minute = record[DateTime_minute];
    out->hour = record[DateTime_hour];
    out->day = record[DateTime_day];
    out->month = record[DateTime_month];
    out->year = (uint16_t)(record[DateTime_year] | record[DateTime_year + 1] << 8);
}

#define TRACK_DATE(field, entry)                                                               \
    void eapp_media_track_##field(const struct eapp_media_track *track,                        \
                                  struct eapp_date_time *out)                                  \
    {                                                                                          \
        _Alignas(4) uint8_t record[DateTime_bytes] = {0};                                      \
        if (!out)                                                                              \
            return;                                                                            \
        if (track)                                                                             \
            entry((struct DateTime *)record, HANDLE(track));                                   \
        date_time(record, out);                                                                \
    }

TRACK_DATE(last_played, GameMetadata_TrackGetLastPlayed)
TRACK_DATE(last_skipped, GameMetadata_TrackGetLastSkipped)
TRACK_DATE(added, GameMetadata_TrackGetDateAdded)
TRACK_DATE(modified, GameMetadata_TrackGetDateModified)

int eapp_media_track_released(const struct eapp_media_track *track,
                              struct eapp_date_time *out)
{
    _Alignas(4) uint8_t record[DateTime_bytes] = {0};
    if (!track || !out || !GameMetadata_TrackGetReleaseDate(HANDLE(track),
                                                            (struct DateTime *)record))
        return -1;
    date_time(record, out);
    return 0;
}

void eapp_media_track_set_rating(struct eapp_media_track *track, uint32_t rating)
{
    if (track)
        GameMetadata_TrackSetRating(HANDLE(track), rating > EAPP_MEDIA_RATING_MAX
                                                       ? EAPP_MEDIA_RATING_MAX
                                                       : (int32_t)rating);
}

void eapp_media_track_set_bookmark_ms(struct eapp_media_track *track, uint32_t milliseconds)
{
    if (track)
        GameMetadata_TrackSetBookmarkMs(HANDLE(track), milliseconds);
}

void eapp_media_track_count_play(struct eapp_media_track *track)
{
    if (track)
        GameMetadata_TrackIncrementPlayCount(HANDLE(track));
}

void eapp_media_track_count_skip(struct eapp_media_track *track)
{
    if (track)
        GameMetadata_TrackIncrementSkipCount(HANDLE(track));
}

struct eapp_media_playlist *eapp_media_playlist_open(uint32_t index)
{
    if (!in_list(EAPP_MEDIA_PLAYLISTS, index))
        return NULL;
    return OBJECT(struct eapp_media_playlist,
                  GameMetadata_LibraryGetPlaylistAt(library, index));
}

struct eapp_media_playlist *eapp_media_playlist_find(uint64_t id)
{
    if (open_library() < 0)
        return NULL;
    return OBJECT(struct eapp_media_playlist, GameMetadata_LibraryFindPlaylist(library, id));
}

void eapp_media_playlist_close(struct eapp_media_playlist *playlist)
{
    if (playlist)
        GameMetadata_PlaylistDestroy(HANDLE(playlist));
}

int eapp_media_playlist_name(const struct eapp_media_playlist *playlist, char *out,
                             size_t capacity)
{
    if (!playlist)
        return -1;
    return eapp_media_copy_text(GameMetadata_PlaylistCopyName, HANDLE(playlist), out, capacity);
}

uint64_t eapp_media_playlist_id(const struct eapp_media_playlist *playlist)
{
    return playlist ? GameMetadata_PlaylistGetPersistentId(HANDLE(playlist)) : 0;
}

uint32_t eapp_media_playlist_track_count(const struct eapp_media_playlist *playlist)
{
    return playlist ? GameMetadata_PlaylistGetCount(HANDLE(playlist)) : 0;
}

/* Order mode 0: the playlist's shuffled order when it is shuffled, else its
 * own. */
struct eapp_media_track *eapp_media_playlist_track_open(
    const struct eapp_media_playlist *playlist, uint32_t index)
{
    if (!playlist || index >= GameMetadata_PlaylistGetCount(HANDLE(playlist)))
        return NULL;
    return OBJECT(struct eapp_media_track,
                  GameMetadata_PlaylistGetTrackAt(HANDLE(playlist), index, 0));
}

int eapp_media_playlist_remove_track(struct eapp_media_playlist *playlist, uint32_t index)
{
    if (!playlist || index >= GameMetadata_PlaylistGetCount(HANDLE(playlist)))
        return -1;
    if (!GameMetadata_PlaylistRemoveEntryAt(HANDLE(playlist), index, 0, 0))
        return -1;
    built = 0;
    return 0;
}

void eapp_media_playlist_refresh(struct eapp_media_playlist *playlist)
{
    if (playlist) {
        GameMetadata_PlaylistRefresh(HANDLE(playlist));
        built = 0;
    }
}

/* The playback list is the Audio interface's, but its entries are files: the
 * host makes a file-only record for each queued path on the UI task, with
 * the duration it is given, so a track's duration must be its own. */
void eapp_music_clear_list(void)
{
    GameAudio_ClearPlaybackList();
}

void eapp_music_remove(uint32_t index)
{
    if (index < GameAudio_GetEntryCount())
        GameAudio_RemoveEntryAtIndex(index);
}

int eapp_music_append(const struct eapp_media_track *track)
{
    char path[EAPP_MEDIA_TEXT_SIZE];
    int length = eapp_media_track_path(track, path, sizeof path);
    if (length <= 0 || (size_t)length >= sizeof path)
        return -1;
    GameAudio_AppendMediaSelection(path, NULL, 0, eapp_media_track_duration_ms(track));
    return 0;
}

/* The host marks the list changed with every clear, append and remove, and
 * replaces a changed list with the user's when the app ends. */
void eapp_music_keep_list_on_exit(void)
{
    GameAudio_SetPlaybackChanged(0);
}

struct eapp_media_track *eapp_music_track_open(uint32_t index)
{
    if (index >= GameAudio_GetEntryCount())
        return NULL;
    return OBJECT(struct eapp_media_track, GameAudio_GetItemHandleAtIndex(index));
}
