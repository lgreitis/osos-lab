/* SPDX-License-Identifier: MIT */

/* The iPod's music library, its covers and photos, and the games' player
 * list, read and edited through the host. */

#ifndef EAPP_MEDIA_H
#define EAPP_MEDIA_H

#include "eapp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's one player list for every game: what an app adds, renames or
 * removes, every game sees. The host writes the list to disk when the iPod
 * enters or leaves disk mode and at a few other host events; until then a
 * restart can lose a change. A player is an id and a name; scores and anything
 * else per player stay in the app's own files. Text calls write a terminated
 * UTF-8 string into `out`, cut before a character that does not fit, and
 * return the whole length, so a result of `capacity` or more means the text
 * was cut. */
#define EAPP_USER_NAME_SIZE 64 /* The longest name plus its terminator. */

uint32_t eapp_user_count(void);
/* The id of player `index` (from 0), -1 past the end. Ids stay with a
 * player; indexes move when one is removed. */
int32_t eapp_user_id(uint32_t index);
/* The name's length, -1 for no such player or no buffer. */
int eapp_user_name(int32_t id, char *out, size_t capacity);
/* A new player at the end of the list; its id, or -1 for an empty name or
 * one of EAPP_USER_NAME_SIZE bytes or more. */
int32_t eapp_user_add(const char *name);
/* 0, or -1 for no such player or a name add refuses. */
int eapp_user_rename(int32_t id, const char *name);
/* Takes the player off the list; their files in any app stay. 0, or -1 for
 * no such player. */
int eapp_user_remove(int32_t id);

/* The iPod's music library, songs only (no videos, podcasts, audiobooks or
 * iTunes U), read through the host: lists of artists, albums, genres,
 * composers, TV shows, seasons, tracks and playlists for the current
 * selection, and tracks and playlists as objects. The selection starts as the
 * whole library when an app first asks. Objects (tracks, playlists, images,
 * pictures) are the host's: their data lives in the iPod's own memory, not in
 * the app's heap (a picture also takes a few bytes of it). Close each one when
 * it is no longer shown, and never hold one per track of a large library. The
 * host frees what is left when the app ends. Text calls write a terminated
 * UTF-8 string into `out`, cut before a character that does not fit, and
 * return the whole length, so a result of `capacity` or more means the text
 * was cut; -1 means no such entry or no buffer. */
#define EAPP_MEDIA_TEXT_SIZE 768 /* Fits any track field but a podcast URL. */

enum eapp_media_list {
    EAPP_MEDIA_ARTISTS = 0,
    EAPP_MEDIA_ALBUMS = 1,
    EAPP_MEDIA_GENRES = 2,
    EAPP_MEDIA_COMPOSERS = 3,
    EAPP_MEDIA_TV_SHOWS = 4,
    EAPP_MEDIA_SEASONS = 5,
    EAPP_MEDIA_TRACKS = 6,
    EAPP_MEDIA_PLAYLISTS = 7,
};

struct eapp_media_track;
struct eapp_media_playlist;

/* Back to the whole library with nothing selected. */
void eapp_media_reset(void);
/* Entries of a list for the current selection. The first count of a list
 * after a selection walks every track of it, which takes time on a large
 * library; later counts and reads of that list are quick until the selection
 * or the order changes. Playlists leave out the library itself. An index
 * means an entry only until the next select, reset, order change or playlist
 * edit; going back a level is a reset and the selections again. */
uint32_t eapp_media_count(enum eapp_media_list list);
/* The name of entry `index`: an artist, album, genre, composer, TV show,
 * a track's title or a playlist's name. -1 for seasons, which are numbers. */
int eapp_media_name(enum eapp_media_list list, uint32_t index, char *out, size_t capacity);
/* The number of season `index`, 0 past the end. */
uint32_t eapp_media_season(uint32_t index);
/* Narrows the selection to entry `index` of a list (not tracks): the other
 * lists then hold what goes with it, and selections add up until reset.
 * After a playlist the tracks list holds its tracks in the order
 * eapp_media_set_order chose; EAPP_MEDIA_ORDER_LIST keeps an unshuffled
 * playlist's own. 0, or -1 past the end. */
int eapp_media_select(enum eapp_media_list list, uint32_t index);
void eapp_media_select_playlist(const struct eapp_media_playlist *playlist);

/* How the tracks list is sorted. LIST keeps the order the entries have in
 * the selection's list, a playlist's own order after selecting a playlist
 * that is not shuffled. */
enum eapp_media_order {
    EAPP_MEDIA_ORDER_LIST = 0,
    EAPP_MEDIA_ORDER_TITLE = 1,  /* Title, then artist, then album. */
    EAPP_MEDIA_ORDER_ARTIST = 2, /* Artist, then album. */
    EAPP_MEDIA_ORDER_ALBUM = 3,
    EAPP_MEDIA_ORDER_GENRE = 4, /* Genre, then album. */
};
/* Sorts the tracks list from now on; a reset keeps the order. An app that
 * needs an order sets it before reading the list. */
void eapp_media_set_order(enum eapp_media_order order);

/* Track `index` of the current selection, NULL past the end. */
struct eapp_media_track *eapp_media_track_open(uint32_t index);
/* The track with the persistent id eapp_media_track_id answers, NULL
 * when none has it. */
struct eapp_media_track *eapp_media_track_find(uint64_t id);
void eapp_media_track_close(struct eapp_media_track *track);
/* 1 while the object still stands for its entry: the host checks its magic
 * word and the database generation, so a sync or a library edit since the
 * open reads as 0, and so does NULL. An app that keeps an object across
 * frames asks before reading it. */
int eapp_media_track_valid(const struct eapp_media_track *track);
int eapp_media_playlist_valid(const struct eapp_media_playlist *playlist);
int eapp_media_track_title(const struct eapp_media_track *track, char *out, size_t capacity);
int eapp_media_track_artist(const struct eapp_media_track *track, char *out, size_t capacity);
int eapp_media_track_album(const struct eapp_media_track *track, char *out, size_t capacity);
int eapp_media_track_genre(const struct eapp_media_track *track, char *out, size_t capacity);
int eapp_media_track_composer(const struct eapp_media_track *track, char *out,
                              size_t capacity);
int eapp_media_track_grouping(const struct eapp_media_track *track, char *out,
                              size_t capacity);
/* What iTunes calls the kind, such as "MPEG audio file". */
int eapp_media_track_file_type(const struct eapp_media_track *track, char *out,
                               size_t capacity);
int eapp_media_track_comment(const struct eapp_media_track *track, char *out,
                             size_t capacity);
int eapp_media_track_category(const struct eapp_media_track *track, char *out,
                              size_t capacity);
int eapp_media_track_description(const struct eapp_media_track *track, char *out,
                                 size_t capacity);
int eapp_media_track_show_name(const struct eapp_media_track *track, char *out,
                               size_t capacity);
/* A podcast's feed URL, which has no length limit. */
int eapp_media_track_podcast_url(const struct eapp_media_track *track, char *out,
                                 size_t capacity);
/* The file on the iPod's disk, as the host writes it. */
int eapp_media_track_path(const struct eapp_media_track *track, char *out, size_t capacity);
/* The iTunes persistent id, the same across launches and syncs. */
uint64_t eapp_media_track_id(const struct eapp_media_track *track);
uint32_t eapp_media_track_duration_ms(const struct eapp_media_track *track);
/* Where playback resumes for a track that remembers its position. */
uint32_t eapp_media_track_bookmark_ms(const struct eapp_media_track *track);
/* The start and stop times iTunes can set; 0 for none. */
uint32_t eapp_media_track_start_ms(const struct eapp_media_track *track);
uint32_t eapp_media_track_stop_ms(const struct eapp_media_track *track);
uint32_t eapp_media_track_size_bytes(const struct eapp_media_track *track);
uint32_t eapp_media_track_bitrate_kbps(const struct eapp_media_track *track);
uint32_t eapp_media_track_year(const struct eapp_media_track *track);
#define EAPP_MEDIA_RATING_MAX 100
uint32_t eapp_media_track_rating(const struct eapp_media_track *track);
uint32_t eapp_media_track_number(const struct eapp_media_track *track);
uint32_t eapp_media_track_disc(const struct eapp_media_track *track);
uint32_t eapp_media_track_play_count(const struct eapp_media_track *track);
uint32_t eapp_media_track_skip_count(const struct eapp_media_track *track);
uint32_t eapp_media_track_season(const struct eapp_media_track *track);

enum eapp_media_kind {
    EAPP_MEDIA_KIND_AUDIOBOOK = 1,
    EAPP_MEDIA_KIND_VIDEO = 2,
    EAPP_MEDIA_KIND_TV_SHOW = 4,
    EAPP_MEDIA_KIND_MUSIC_VIDEO = 8,
};

/* EAPP_MEDIA_KIND_* bits; 0 for none of them, such as a song. */
uint32_t eapp_media_track_kind(const struct eapp_media_track *track);
/* 1 when playback resumes at the bookmark, as for audiobooks, else 0. */
int eapp_media_track_remembers_position(const struct eapp_media_track *track);
/* Dates in local time. released returns 0, or -1 for a track without a
 * release date. */
void eapp_media_track_last_played(const struct eapp_media_track *track,
                                  struct eapp_date_time *out);
void eapp_media_track_last_skipped(const struct eapp_media_track *track,
                                   struct eapp_date_time *out);
void eapp_media_track_added(const struct eapp_media_track *track,
                            struct eapp_date_time *out);
void eapp_media_track_modified(const struct eapp_media_track *track,
                               struct eapp_date_time *out);
int eapp_media_track_released(const struct eapp_media_track *track,
                              struct eapp_date_time *out);
/* Writes to the library's record of the track. Rating is clamped to
 * EAPP_MEDIA_RATING_MAX; counting a play or a skip also stamps its date. */
void eapp_media_track_set_rating(struct eapp_media_track *track, uint32_t rating);
void eapp_media_track_set_bookmark_ms(struct eapp_media_track *track, uint32_t milliseconds);
void eapp_media_track_count_play(struct eapp_media_track *track);
void eapp_media_track_count_skip(struct eapp_media_track *track);

/* Playlist `index` of the playlists list, NULL past the end. */
struct eapp_media_playlist *eapp_media_playlist_open(uint32_t index);
/* The playlist with the persistent id eapp_media_playlist_id answers,
 * NULL when none has it. */
struct eapp_media_playlist *eapp_media_playlist_find(uint64_t id);
void eapp_media_playlist_close(struct eapp_media_playlist *playlist);
int eapp_media_playlist_name(const struct eapp_media_playlist *playlist, char *out,
                             size_t capacity);
uint64_t eapp_media_playlist_id(const struct eapp_media_playlist *playlist);
uint32_t eapp_media_playlist_track_count(const struct eapp_media_playlist *playlist);
/* Track `index` in the playlist's order (its shuffled order when the
 * playlist is shuffled), NULL past the end. */
struct eapp_media_track *eapp_media_playlist_track_open(
    const struct eapp_media_playlist *playlist, uint32_t index);
/* Takes entry `index` out of the playlist. 0, or -1 when the host
 * refused. */
int eapp_media_playlist_remove_track(struct eapp_media_playlist *playlist, uint32_t index);
/* Re-applies a smart playlist's rules to the library. */
void eapp_media_playlist_refresh(struct eapp_media_playlist *playlist);

/* The playback list, built by the app: clear it, append tracks or files of the
 * package, then eapp_music_play_track starts an entry. The host queues each
 * entry by its file, not as a library entry: the audio plays, but the player
 * knows nothing else about it, so Now Playing shows no title, artist or
 * artwork, and the play and skip counts stay as they are unless the app counts
 * them. A file that does not exist is left out, and the entries after it move
 * up. The host empties the list when the app ends, and Play then brings back
 * the user's own list, unless eapp_music_keep_list_on_exit came after the
 * last change. */
void eapp_music_clear_list(void);
/* Puts the track at the end of the list with its own duration; the app may
 * close it right after. 0, or -1 for a track without a path. */
int eapp_music_append(const struct eapp_media_track *track);
/* Puts file `name` of the app's package at the end of the list; the player
 * shows `duration_ms` as its length. 0, or -1 for a name that leaves the
 * package or a path of EAPP_MEDIA_TEXT_SIZE bytes or more. */
int eapp_music_append_file(const char *name, uint32_t duration_ms);
/* Takes entry `index` out of the list; past the end it does nothing. */
void eapp_music_remove(uint32_t index);
/* Keeps the list as the player's list after the app ends. Only the last
 * change counts: a later clear, append or remove undoes this until it is
 * called again. */
void eapp_music_keep_list_on_exit(void);
/* Entry `index` of the playback list, NULL past the end. An entry the app
 * appended has no title, artist, album or persistent id, and reads back from
 * a later frame, once the host's UI task has queued it. */
struct eapp_media_track *eapp_music_track_open(uint32_t index);

/* Pictures: album covers and the iPod's photos, as RGB565 bitmaps for
 * eapp_draw_bitmap. An image is an entry of the artwork or photo database;
 * loading one reads one of its thumbnails from the disk at once, on the app's
 * task, into the iPod's memory, where it stays until the picture is freed (a
 * 200 x 200 cover holds 80 KB). */
struct eapp_media_image;
struct eapp_media_picture;
struct eapp_bitmap;

/* The cover of the track with that persistent id, or of album `index` of
 * the albums list; NULL for none. */
struct eapp_media_image *eapp_media_artwork_find(uint64_t track_id);
struct eapp_media_image *eapp_media_album_artwork_open(uint32_t index);
void eapp_media_image_close(struct eapp_media_image *image);
/* The largest thumbnail no wider than max_width and no higher than
 * max_height, or the smallest when none fits; NULL when the image has no
 * thumbnail the runtime can draw. */
struct eapp_media_picture *eapp_media_image_load(const struct eapp_media_image *image,
                                                 uint32_t max_width, uint32_t max_height);
/* The picture's pixels, valid until it is freed; `transparent` is -1. */
const struct eapp_bitmap *eapp_media_picture_bitmap(const struct eapp_media_picture *picture);
void eapp_media_picture_free(struct eapp_media_picture *picture);

/* The photo library: albums by index from 0, images of an album, or of the
 * whole library with album -1. */
uint32_t eapp_media_photo_album_count(void);
int eapp_media_photo_album_name(uint32_t album, char *out, size_t capacity);
/* The index of the album with that name, -1 for none. */
int32_t eapp_media_photo_album_index(const char *name);
uint32_t eapp_media_photo_count(int32_t album);
struct eapp_media_image *eapp_media_photo_open(int32_t album, uint32_t index);
/* The photo with the id eapp_media_image_id answers, NULL for none. */
struct eapp_media_image *eapp_media_photo_find(uint32_t id);
uint32_t eapp_media_image_id(const struct eapp_media_image *image);

#ifdef __cplusplus
}
#endif
#endif
