# Playback log

Settings > Playback Log is the only iPod control and is off by default. When on,
tracks that end, are skipped or replaced are logged in UTF-8 at the disk root.
The default `.scrobbler.log` is compatible with Rockbox/Audioscrobbler importers;
use a desktop uploader to submit it. Keep the iPod clock and time zone correct.

For other reports, create `iPod_Control/Device/playback-log.cfg` and reboot:

```ini
format=both
include_podcasts=0
include_audiobooks=0
columns=start_utc,artist,album,title,listened_ms,length_ms,ended
```

- `format`: `scrobbler` (default), `detailed`, or `both`.
- Podcast and audiobook flags accept `0` or `1`; both default to `0`.
- `columns` selects and orders the detailed fields below. Omit it for all fields
  in table order. It never changes the scrobbler format.
- Blank lines, surrounding whitespace and lines starting with `#` are allowed.
  Unknown or invalid settings are ignored. An empty, duplicate or unknown column
  invalidates that entire list. Files over 1 KiB use defaults; lines over 511
  bytes are ignored. The last valid value for a setting wins.

Full detailed reports use `playback-v3.tsv`. Custom layouts use
`playback-v3-XXXXXXXX.tsv`, with a stable suffix determined by the ordered header.
Changing columns therefore starts a separate file; returning to that layout
appends to its existing file. A mismatched header is left untouched. Existing
v2 `playback.tsv` files are preserved. Tags are limited to 255 UTF-8 bytes without
splitting characters; tabs and newlines in tags become spaces.

Scrobbler rows are `L` after half the track or four minutes of listening,
whichever comes first, provided the track exceeds 30 seconds; otherwise `S`.
Pauses and seek jumps do not count. Enabling mid-track starts counting then.
Detailed reports retain shorter plays too. Rows are saved when a track ends or
is left; an unfinished play is lost on an abrupt reset or power loss.

| Column | Meaning |
|---|---|
| `version` | Row format version, currently `3`. |
| `session` | First logged time after boot, `YYYYMMDD-HHMMSS` (local). Groups rows per boot. |
| `end_utc` | When the track stopped, Unix seconds UTC. |
| `utc_offset` | iPod time zone including DST, `+HH:MM`. |
| `start_local` | Local time when logging first observed this play. |
| `end_local` | When the track stopped, iPod local time `YYYY-MM-DD HH:MM:SS`. |
| `listened_ms` | Accumulated playing time, excluding pauses and seek jumps. |
| `length_ms` | Track duration. |
| `percent` | `listened_ms` as a percentage of `length_ms`; rewinding can make it exceed 100. |
| `ended` | `end` (played to the end), `skip` (next/previous button), `change` (another selection or stop). |
| `apple` | `played` or `skipped` when the stock firmware updated that counter, otherwise `none`. |
| `artist`, `album_artist`, `album`, `title` | Tags from the iTunesDB. |
| `track`, `disc`, `year` | Numbers from the iTunesDB, `0` when missing. |
| `genre`, `composer` | Tags from the iTunesDB. |
| `bitrate` | Average kbps from the iTunesDB. |
| `sample_rate` | Hz from the iTunesDB. |
| `rating` | Stars, 0-5. |
| `play_count`, `skip_count` | The track's counters after this playback. |
| `shuffle` | `on` or `off` for the playing queue. |
| `repeat` | `off`, `one` or `all`. |
| `queue_pos`, `queue_len` | Position in the playing queue (1-based) and its length. |
| `persistent_id` | iTunes Persistent ID (16 hex digits), the same ID iTunes shows for the track. |
| `start_utc` | Unix UTC seconds when logging first observed this play. |
| `position_ms` | Final playback position, separate from time listened. |
