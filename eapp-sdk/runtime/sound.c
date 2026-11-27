/* SPDX-License-Identifier: MIT */

/* Sound effects on the host's SoundEffect objects. The host keeps only the
 * pointer to the samples and reads them from a mixer voice while a sound
 * plays, so a block loaded here lives until the sound is freed, and a buffer
 * the app gave stays the app's. The host cannot clear a sound's next, so a
 * freed sound that a living sound still reaches through next links stays
 * with the host, playing when its turn comes, and goes when nothing reaches
 * it any more. Destroying a sound detaches its voices, so a free does not
 * stop first: the host's stop runs along the whole next ring. */

#include "osos-2.0.5.h"
#include "platform.h"

struct eapp_sound {
    int handle;
    uint8_t *owned; /* The file block of a loaded sound. */
    const struct eapp_sound *next;
    uint8_t state; /* 0 free, LIVE, or FREED while a sound still names it. */
};

enum { FREE, LIVE, FREED };

_Static_assert(EAPP_SOUND_VOLUME_MAX == GameSound_VolumeMax, "volume");
_Static_assert(EAPP_SOUND_PAN_MAX == GameSound_PanMax, "pan");
_Static_assert(EAPP_SOUND_SPEED_MIN == GameSound_SpeedMin, "speed");
_Static_assert(EAPP_SOUND_SPEED_MAX == GameSound_SpeedMax, "speed");
_Static_assert(EAPP_SOUND_RATE_MIN == GameSound_SampleRateMin, "rate");
_Static_assert(EAPP_SOUND_RATE_MAX == GameSound_SampleRateMax, "rate");

static struct eapp_sound sounds[32];
#define COUNT (sizeof sounds / sizeof sounds[0])

static int live(const struct eapp_sound *sound)
{
    return sound && sound->state == LIVE;
}

static int format_ok(uint32_t rate, uint32_t channels, uint32_t bits)
{
    return rate >= EAPP_SOUND_RATE_MIN && rate <= EAPP_SOUND_RATE_MAX &&
           (channels == 1 || channels == 2) && (bits == 8 || bits == 16);
}

struct eapp_sound *eapp_sound_create(const void *pcm, size_t bytes, uint32_t rate,
                                     uint32_t channels, uint32_t bits)
{
    if (!pcm || !bytes || !format_ok(rate, channels, bits) || bytes % (channels * bits / 8) ||
        bytes > UINT32_MAX)
        return NULL;
    struct eapp_sound *sound = NULL;
    for (unsigned i = 0; i < COUNT && !sound; ++i)
        if (sounds[i].state == FREE)
            sound = &sounds[i];
    if (!sound)
        return NULL;
    int handle = GameSound_Create();
    if (handle < 0)
        return NULL;
    GameSound_SetData(handle, pcm);
    GameSound_SetByteCount(handle, (uint32_t)bytes);
    GameSound_SetSampleRate(handle, rate);
    GameSound_SetChannels(handle, channels);
    GameSound_SetBitsPerSample(handle, bits);
    sound->handle = handle;
    sound->owned = NULL;
    sound->next = NULL;
    sound->state = LIVE;
    return sound;
}

static uint32_t le16(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static int tag(const uint8_t *p, const char *id)
{
    return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3];
}

/* The PCM of a RIFF WAVE in `file`: the data chunk's bytes, with the format
 * chunk's rate, channels and bits; 0 for anything but plain PCM
 * (WAVE_FORMAT_PCM, 1). 8-bit samples are stored unsigned and flipped to the
 * signed form the host mixes. */
static uint8_t *wav_pcm(uint8_t *file, uint32_t size, uint32_t *bytes, uint32_t *rate,
                        uint32_t *channels, uint32_t *bits)
{
    if (size < 12 || !tag(file, "RIFF") || !tag(file + 8, "WAVE"))
        return NULL;
    uint8_t *pcm = NULL;
    int format = 0;
    *bytes = 0;
    for (uint32_t at = 12; at + 8 <= size;) {
        uint8_t *chunk = file + at + 8;
        uint32_t length = le32(file + at + 4);
        if (length > size - at - 8)
            length = size - at - 8;
        if (tag(file + at, "fmt ") && length >= 16) {
            format = le16(chunk) == 1;
            *channels = le16(chunk + 2);
            *rate = le32(chunk + 4);
            *bits = le16(chunk + 14);
        } else if (tag(file + at, "data")) {
            pcm = chunk;
            *bytes = length;
        }
        at += 8 + length + (length & 1);
    }
    if (!format || !pcm || !*bytes)
        return NULL;
    if (*bits == 8)
        for (uint32_t i = 0; i < *bytes; ++i)
            pcm[i] ^= 0x80;
    return pcm;
}

struct eapp_sound *eapp_sound_load(const char *path)
{
    struct eapp_file *file = eapp_file_open(path);
    if (!file)
        return NULL;
    uint32_t size = eapp_file_size(file);
    uint8_t *block = size ? eapp_alloc(size) : NULL;
    int read = block ? eapp_file_read(file, block, size) : -1;
    eapp_file_close(file);
    struct eapp_sound *sound = NULL;
    if (read == (int)size) {
        uint32_t bytes = 0, rate = 0, channels = 0, bits = 0;
        uint8_t *pcm = wav_pcm(block, size, &bytes, &rate, &channels, &bits);
        if (pcm)
            sound = eapp_sound_create(pcm, bytes, rate, channels, bits);
    }
    if (sound)
        sound->owned = block;
    else
        eapp_free(block);
    return sound;
}

static void destroy(struct eapp_sound *sound)
{
    GameSound_Destroy(sound->handle);
    eapp_free(sound->owned);
    memset(sound, 0, sizeof *sound);
}

/* Destroys the freed sounds no live sound reaches through next links. */
static void collect(void)
{
    uint8_t reached[COUNT] = {0};
    for (unsigned i = 0; i < COUNT; ++i)
        for (const struct eapp_sound *s = sounds[i].state == LIVE ? sounds[i].next : NULL;
             s && !reached[s - sounds]; s = s->next)
            reached[s - sounds] = 1;
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state == FREED && !reached[i])
            destroy(&sounds[i]);
}

void eapp_sound_free(struct eapp_sound *sound)
{
    if (!live(sound))
        return;
    sound->state = FREED;
    collect();
}

void eapp_sounds_free_all(void)
{
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state != FREE)
            GameSound_Stop(sounds[i].handle);
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state != FREE)
            destroy(&sounds[i]);
}

void eapp_sound_play(struct eapp_sound *sound)
{
    if (!live(sound))
        return;
    GameSound_Stop(sound->handle);
    GameSound_Play(sound->handle);
}

void eapp_sound_stop(struct eapp_sound *sound)
{
    if (live(sound))
        GameSound_Stop(sound->handle);
}

void eapp_sound_pause(struct eapp_sound *sound)
{
    if (live(sound))
        GameSound_Pause(sound->handle);
}

void eapp_sound_resume(struct eapp_sound *sound)
{
    if (live(sound))
        GameSound_Resume(sound->handle);
}

int eapp_sound_playing(const struct eapp_sound *sound)
{
    return live(sound) && GameSound_IsPlaying(sound->handle);
}

uint32_t eapp_sound_position(const struct eapp_sound *sound)
{
    return live(sound) ? GameSound_GetVoicePosition(sound->handle) : 0;
}

void eapp_sound_set_volume(struct eapp_sound *sound, uint32_t volume)
{
    if (live(sound))
        GameSound_SetVolume(sound->handle, volume);
}

void eapp_sound_set_pan(struct eapp_sound *sound, int32_t pan)
{
    if (live(sound))
        GameSound_SetPan(sound->handle, pan);
}

void eapp_sound_set_speed(struct eapp_sound *sound, uint32_t per_mille)
{
    if (live(sound))
        GameSound_SetSpeed(sound->handle, per_mille);
}

void eapp_sound_set_play_count(struct eapp_sound *sound, uint32_t count)
{
    if (live(sound))
        GameSound_SetPlayCount(sound->handle, count);
}

void eapp_sound_set_lead_in_ms(struct eapp_sound *sound, uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetLeadInMilliseconds(sound->handle, milliseconds);
}

void eapp_sound_set_tail_ms(struct eapp_sound *sound, uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetTailMilliseconds(sound->handle, milliseconds);
}

void eapp_sound_set_next(struct eapp_sound *sound, struct eapp_sound *next)
{
    if (!live(sound) || !live(next))
        return;
    GameSound_SetNext(sound->handle, next->handle);
    sound->next = next;
    collect();
}

void eapp_sound_set_music_duck(struct eapp_sound *sound, uint32_t level,
                               uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetMusicDuck(sound->handle, level, milliseconds);
}

/* A stream's ring is one looping sound. A looping voice plays the frames up
 * to the last and only interpolates towards that one, so the sound is the
 * ring plus a copy of its first frame. The voice's frame cursor runs from 0
 * to the ring's length in every pass, so the frames since the last look are
 * the cursor's distance travelled, modulo the ring. */
struct eapp_sound_stream {
    struct eapp_sound *sound;
    uint8_t *ring;
    uint32_t frames, frame_bytes, rate;
    uint32_t cursor; /* The voice's frame at the last look. */
    uint32_t looked; /* eapp_ticks_ms() at the last look. */
    uint32_t write;  /* The frame the next write fills. */
    uint32_t queued; /* Frames written from the cursor on. */
    uint8_t started;
};

struct eapp_sound_stream *eapp_sound_stream_create(uint32_t rate, uint32_t channels,
                                                   uint32_t bits, uint32_t frames)
{
    if (!format_ok(rate, channels, bits) || frames < rate / 20)
        return NULL;
    uint32_t frame_bytes = channels * bits / 8;
    if (frames > (UINT32_MAX - sizeof(struct eapp_sound_stream)) / frame_bytes - 1)
        return NULL;
    size_t bytes = ((size_t)frames + 1) * frame_bytes;
    struct eapp_sound_stream *stream = eapp_alloc(sizeof *stream + bytes);
    if (!stream)
        return NULL;
    memset(stream, 0, sizeof *stream + bytes);
    stream->ring = (uint8_t *)(stream + 1);
    stream->frames = frames;
    stream->frame_bytes = frame_bytes;
    stream->rate = rate;
    stream->sound = eapp_sound_create(stream->ring, bytes, rate, channels, bits);
    if (!stream->sound) {
        eapp_free(stream);
        return NULL;
    }
    GameSound_SetPlayCount(stream->sound->handle, 0);
    return stream;
}

void eapp_sound_stream_free(struct eapp_sound_stream *stream)
{
    if (!stream)
        return;
    eapp_sound_free(stream->sound);
    eapp_free(stream);
}

/* Copies the first frame behind the ring's end. */
static void guard(struct eapp_sound_stream *stream)
{
    memcpy(stream->ring + stream->frames * stream->frame_bytes, stream->ring,
           stream->frame_bytes);
}

/* Silences `count` frames from `from` on, round the ring. */
static void silence(struct eapp_sound_stream *stream, uint32_t from, uint32_t count)
{
    uint32_t first = stream->frames - from < count ? stream->frames - from : count;
    memset(stream->ring + from * stream->frame_bytes, 0, first * stream->frame_bytes);
    memset(stream->ring, 0, (count - first) * stream->frame_bytes);
    guard(stream);
}

/* Moves the cursor to the voice's and silences what it played. A voice taken
 * by another sound starts the stream over. The cursor runs up to a mixing
 * block ahead of the clock, so after three quarters of the ring's length of
 * time its distance can hide a pass, and the whole ring counts as played. */
static void advance(struct eapp_sound_stream *stream)
{
    if (!stream->started)
        return;
    if (!eapp_sound_playing(stream->sound)) {
        silence(stream, 0, stream->frames);
        stream->cursor = stream->write = stream->queued = stream->started = 0;
        return;
    }
    uint32_t now = eapp_sound_position(stream->sound) % stream->frames;
    uint32_t played = (now + stream->frames - stream->cursor) % stream->frames;
    uint32_t ms = eapp_ticks_ms();
    if ((uint64_t)(ms - stream->looked) * stream->rate * 4 >= (uint64_t)stream->frames * 3000)
        played = stream->frames;
    stream->looked = ms;
    silence(stream, stream->cursor, played);
    if (played > stream->queued) {
        stream->write = now;
        stream->queued = 0;
    } else {
        stream->queued -= played;
    }
    stream->cursor = now;
}

uint32_t eapp_sound_stream_space(struct eapp_sound_stream *stream)
{
    if (!stream)
        return 0;
    advance(stream);
    return stream->frames - stream->queued;
}

uint32_t eapp_sound_stream_write(struct eapp_sound_stream *stream, const void *pcm,
                                 uint32_t frames)
{
    if (!stream || !pcm)
        return 0;
    advance(stream);
    uint32_t space = stream->frames - stream->queued;
    if (frames > space)
        frames = space;
    uint32_t first =
        stream->frames - stream->write < frames ? stream->frames - stream->write : frames;
    memcpy(stream->ring + stream->write * stream->frame_bytes, pcm,
           first * stream->frame_bytes);
    memcpy(stream->ring, (const uint8_t *)pcm + first * stream->frame_bytes,
           (frames - first) * stream->frame_bytes);
    guard(stream);
    stream->write = (stream->write + frames) % stream->frames;
    stream->queued += frames;
    if (!stream->started && stream->queued >= stream->frames / 2) {
        eapp_sound_play(stream->sound);
        stream->looked = eapp_ticks_ms();
        stream->started = 1;
    }
    return frames;
}

void eapp_sound_stream_set_volume(struct eapp_sound_stream *stream, uint32_t volume)
{
    if (stream)
        eapp_sound_set_volume(stream->sound, volume);
}
