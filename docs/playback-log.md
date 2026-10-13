# Playback log

Turn it on in Settings > Playback Log (off by default). RepriseOS then appends
one row to `playback.tsv` (root of the iPod's disk) each time a track stops
playing: it ends, you skip it, you pick something else, or playback
stops. The file is UTF-8, tab-separated, with a header row written when the
file is created. Tabs and line breaks inside tags are replaced with spaces.

Load it with `pandas.read_csv("playback.tsv", sep="\t")`, LibreOffice, or
`cut`/`awk -F'\t'`.

| Column | Meaning |
|---|---|
| `version` | Row format version, currently `2`. New columns are only appended. |
| `session` | First logged time after boot, `YYYYMMDD-HHMMSS` (local). Groups rows per boot. |
| `end_utc` | When the track stopped, Unix seconds UTC. |
| `utc_offset` | iPod time zone including DST, `+HH:MM`. |
| `start_local` | `end_local` minus `elapsed_ms`; later than the real start if playback was paused. |
| `end_local` | When the track stopped, iPod local time `YYYY-MM-DD HH:MM:SS`. |
| `elapsed_ms` | Playback position when the track stopped (the full length for a natural end). |
| `length_ms` | Track duration. |
| `percent` | `elapsed_ms` as a percentage of `length_ms`. |
| `ended` | `end` (played to the end), `skip` (next/previous button), `change` (another selection or stop). |
| `apple` | `played` when the stock firmware counted the play, otherwise `none`. |
| `artist`, `album_artist`, `album`, `title` | Tags from the iTunesDB. |
| `track`, `disc`, `year` | Numbers from the iTunesDB, `0` when missing. |
| `genre`, `composer` | Tags from the iTunesDB. |
| `bitrate` | Average kbps from the iTunesDB. |
| `sample_rate` | Hz from the iTunesDB. |
| `lossless` | `yes` above 400 kbps (ALAC, WAV, AIFF); no MP3 or AAC exceeds it. |
| `rating` | Stars, 0-5. |
| `play_count`, `skip_count` | The track's counters after this playback. |
| `shuffle` | `on` or `off` for the playing queue. |
| `repeat` | `off`, `one` or `all`. |
| `queue_pos`, `queue_len` | Position in the playing queue (1-based) and its length. |
| `persistent_id` | iTunes Persistent ID (16 hex digits), the same ID iTunes shows for the track. |

`apple` follows Apple's rule: a track counts as played when it stops within 10 s
of its end (60 s for tracks of 20 minutes or longer). Apple's skip counter is
updated elsewhere and is not reflected here; use `ended` to see skips, and
`percent` or `elapsed_ms` to apply any other rule (Last.fm, Spotify, your own).
