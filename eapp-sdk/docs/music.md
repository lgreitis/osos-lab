# Music

`eapp_music_*` drives the iPod's player.

## Commands

`play_pause`, `stop`, `next`, `previous`, `play_track`, `seek`, `scan_forward`, `scan_backward`, `scan_stop` and the setters are queued and applied after the frame returns. `set_volume` applies at once.

| Read | Answers |
|---|---|
| `eapp_music_state`, `track_count`, `repeat_mode`, `shuffle_mode`, `eq`, `speed`, `gain` | The new value at once |
| `position`, `duration`, `track_index`, `scanning` | The old value for one more frame |

Transport commands and the repeat, shuffle, EQ and speed setters do nothing without a playlist (`has_playlist` is 0). `stop` ends playback and drops the playlist; use `play_pause` to pause. After a `stop`, repeat and shuffle read as off until set again. `play_track` takes an index from 0 and ignores one past the end.

## Settings

| Setting | |
|---|---|
| Volume | 0 to `EAPP_MUSIC_VOLUME_MAX`, clamped to the user's limit (`volume_limit`) |
| Speed | `EAPP_MUSIC_SPEED_MIN` to `EAPP_MUSIC_SPEED_MAX` per mille, the audiobook speed. Values out of range are ignored |
| Gain | -`EAPP_MUSIC_GAIN_MAX` to `EAPP_MUSIC_GAIN_MAX` in the host's unit |
| EQ | `set_eq_until_exit` or `set_eq` |

On exit the host restores repeat, shuffle, speed, gain and an EQ set with `set_eq_until_exit`. `set_eq` stays after exit, while Settings > EQ still shows the user's choice. The volume is not restored: an app that changes it restores it in `shutdown`.

## Buttons

Play, Next and Previous releases go to the player unless the app sets `handles_music_buttons`. Such an app passes the buttons it does not use to `eapp_music_pass_button`. The host acts only on releases of those three.

## The app's own list

| Call | |
|---|---|
| `eapp_music_clear_list` | Empties the list |
| `eapp_music_append(track)` | Adds a library track, which can be closed right after |
| `eapp_music_append_file(name, duration_ms)` | Adds a package file |
| `eapp_music_remove(index)` | Removes an entry |
| `eapp_music_play_track(index)` | Starts the list |
| `eapp_music_track_open(index)` | An entry as a track object, from a later frame on, without title, artist, album or id |
| `eapp_music_keep_list_on_exit` | Keeps the list when the app ends, when called after the last clear, append or remove |

The host queues entries by file, so Now Playing shows no title, artist or artwork, and play and skip counts change only if the app counts them. A missing file is left out and later entries move up. When the app ends the host empties the list and Play brings back the user's own.
