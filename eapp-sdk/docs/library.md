# Library

`eapp_media.h` reads the iPod's music library, songs only. Videos, podcasts, audiobooks and iTunes U are left out.

## Selection

Reads work on a selection that starts as the whole library. `eapp_media_select(list, index)` narrows it by a list entry (an artist, an album, a playlist); selections add up. `eapp_media_reset` returns to everything; to go back one level, reset and select again. An index is valid until the next select, reset, order change or playlist edit. Selections do not affect the Music menus.

The lists are `EAPP_MEDIA_ARTISTS`, `ALBUMS`, `GENRES`, `COMPOSERS`, `TV_SHOWS`, `SEASONS`, `TRACKS` and `PLAYLISTS`. `eapp_media_count(list)` builds the list for the selection, `eapp_media_name(list, index, out, capacity)` reads an entry. The first count after a selection walks every track and is slow on a large library; later counts and reads are quick. Count once, then read the visible rows.

`eapp_media_set_order` sorts the tracks list by title, artist, album or genre. `EAPP_MEDIA_ORDER_LIST` keeps the list's order, a playlist's own after selecting a playlist that is not shuffled. A reset keeps the order. Music > Songs and the Music playlists keep their own. Set it before reading the list.

## Tracks and playlists

| Call | |
|---|---|
| `eapp_media_track_open(index)` | A track of the selection |
| `eapp_media_track_find(id)` | By the iTunes persistent id from `eapp_media_track_id` |
| `eapp_media_playlist_open(index)`, `eapp_media_playlist_find(id)` | The same for playlists |
| `eapp_media_track_close`, `eapp_media_playlist_close` | Close one |
| `eapp_media_track_valid`, `eapp_media_playlist_valid` | Whether the object still stands for its entry after a sync or a library edit |
| `eapp_media_track_set_rating`, `set_bookmark_ms`, `count_play`, `count_skip` | Write to the library. Ratings are 0 to `EAPP_MEDIA_RATING_MAX` |
| `eapp_media_playlist_remove_track` | Removes an entry from a playlist |

Objects live in the iPod's memory, not the app's heap. Close each one when it is no longer shown and never hold one per track of a large library. The host frees the rest when the app ends.

Text fields are copied as UTF-8 and cut before a character that does not fit. The call returns the full length, so a result of `capacity` or more means a cut. `EAPP_MEDIA_TEXT_SIZE` fits every track field but a podcast URL.

## Covers and photos

A `struct eapp_media_image` is a track's cover (`eapp_media_artwork_find`), an album's (`eapp_media_album_artwork_open`), or a photo by album and index (`eapp_media_photo_open`) or by id (`eapp_media_photo_find`). `eapp_media_image_load(image, max_width, max_height)` reads the largest thumbnail within that size, or the smallest when none fits, from disk on the app's task. Only plain RGB565 thumbnails load. `eapp_media_picture_bitmap` returns the pixels as a `struct eapp_bitmap` for [drawing](drawing.md). They take about width x height x 2 bytes of the iPod's memory (80 KB for a 200 x 200 cover) until `eapp_media_picture_free`.

## Players

`eapp_user_count`, `id`, `name`, `add`, `rename` and `remove` edit the host's player list, shared by every game. A player has a fixed id and a name shorter than `EAPP_USER_NAME_SIZE` bytes. Indexes shift when a player is removed. The host writes the list to disk when the iPod enters or leaves disk mode and at a few other events, so a restart before that can lose a change. Keep scores and other per-player data in the app's data files, keyed by the id.
