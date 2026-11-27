# Sound

The host mixes up to 32 sounds over the music on eight voices and resamples them to the output rate. It takes 8-bit signed or 16-bit little-endian PCM, 1 or 2 channels, `EAPP_SOUND_RATE_MIN` to `EAPP_SOUND_RATE_MAX` Hz (8000 to 48000).

| Call | |
|---|---|
| `eapp_sound_load(path)` | A RIFF/WAVE file of the package with a plain PCM format chunk (format 1, not float or extensible). 8-bit unsigned samples are converted on loading. The sound keeps its own copy |
| `eapp_sound_create(pcm, bytes, rate, channels, bits)` | A buffer of the app. It must stay valid until the sound is freed and until every sound naming it as `next` is freed. Other formats are refused |
| `eapp_sound_play` | Starts from the beginning, restarting a sound still playing |
| `eapp_sound_stop`, `pause`, `resume` | Stop, pause and resume. A paused sound is not `playing` |
| `eapp_sound_playing`, `eapp_sound_position` | Whether a voice renders the sound, and the frames played of the current pass |
| `eapp_sound_free` | Stops the sound unless another names it as `next` |

Sounds left at shutdown are stopped and freed. Ship WAVs through `FILES`. `sox in.wav -r 22050 -c 1 -b 16 out.wav` converts a file.

## Setters

The setters pass values to the host unchecked.

| Setter | Range | Default |
|---|---|---|
| `set_volume` | 0 to `EAPP_SOUND_VOLUME_MAX` | `EAPP_SOUND_VOLUME_MAX` |
| `set_pan` | -`EAPP_SOUND_PAN_MAX` to `EAPP_SOUND_PAN_MAX` | 0 |
| `set_speed` | `EAPP_SOUND_SPEED_MIN` to `EAPP_SOUND_SPEED_MAX` per mille (500 to 2000) | 1000 |
| `set_play_count` | 0 loops until stopped | 1 |
| `set_lead_in_ms`, `set_tail_ms` | Silence before and after | 0 |
| `set_next` | A sound that plays after this one and its tail | none |
| `set_music_duck` | The level the music fades to over a silence before the sound; it comes back after | none |

The host cannot unchain `next`: a freed `next` sound keeps playing in turn until nothing names it.
