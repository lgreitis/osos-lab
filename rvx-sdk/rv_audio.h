/* SPDX-License-Identifier: GPL-3.0-only */

/* What an audio effect shares with the others: the output format behind the
 * PCM site, the fixed-point steps of a sample loop, the handoff of a set of
 * values from the task that chose them to the pump task that plays them, and
 * the fade from the state that played to the one that takes over. The
 * look-ahead limiter and the fade behind it are in the dsp library extension
 * (rv_dsp.h).
 *
 * The handoff and the crossfade are adapted from RepriseOS payload/eq_audio.c
 * (dbc0e6f, 93f6a45) by lgreitis. */

#ifndef RV_AUDIO_H
#define RV_AUDIO_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"

/* The priority an effect binds RV_SITE_AudioOutputQueue_Get_call with, so the
 * output stage runs in Rockbox's order whatever order the extensions loaded
 * in. Filters run lower first; an effect that belongs between two stages picks
 * a value between them. */
enum rv_audio_stage {
    RV_AUDIO_STAGE_CROSSFEED = 100,
    RV_AUDIO_STAGE_TONE = 200,
    RV_AUDIO_STAGE_BASS_ENHANCEMENT = 300,
    RV_AUDIO_STAGE_FATIGUE_REDUCTION = 400,
    RV_AUDIO_STAGE_SURROUND = 500,
    RV_AUDIO_STAGE_CHANNELS = 600,
    RV_AUDIO_STAGE_COMPRESSOR = 700,
    RV_AUDIO_STAGE_BALANCE = 800,
    RV_AUDIO_STAGE_DITHER = 900,
};

/* The output of the AudioEffectsContext a filter on
 * RV_SITE_AudioOutputQueue_Get_call receives as its third argument: 1 when
 * `frames` is not 0 and it is 16-bit stereo at a rate from `min_rate` to
 * `max_rate`, with *rate that rate. */
static inline int rv_audio_stereo16(const void *context, uint32_t frames,
                                    uint32_t min_rate, uint32_t max_rate,
                                    uint32_t *rate)
{
    if (!frames)
        return 0;
    *rate = RV_FIELD(context, AudioEffectsContext_output_rate, uint32_t);
    return RV_FIELD(context, AudioEffectsContext_output_bits, uint32_t) == 16 &&
           RV_FIELD(context, AudioEffectsContext_output_channels, uint32_t) == 2 &&
           *rate >= min_rate && *rate <= max_rate;
}

/* a times b, both Q31. One smull in ARM, a library call in Thumb. */
static inline int32_t rv_fracmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 31);
}

static inline int16_t rv_saturate16(int32_t sample)
{
    return sample > 32767 ? 32767 : sample < -32768 ? -32768 : (int16_t)sample;
}

/* A set of values left for the pump task. The task that chose them copies
 * them into `next` with interrupts masked and moves the generation; the pump
 * task, which alone reads `taken`, copies `next` into `playing` once it sees
 * the generation move, and again when it moved under the copy. */
struct rv_handoff {
    volatile uint32_t generation;
    uint32_t taken;
};

static inline void rv_handoff_publish(const struct rv_api *api,
                                      struct rv_handoff *handoff, void *next,
                                      const void *set, uint32_t bytes)
{
    uint32_t state = api->irq_save();
    memcpy(next, set, bytes);
    handoff->generation++;
    api->irq_restore(state);
}

static inline int rv_handoff_pending(const struct rv_handoff *handoff)
{
    return handoff->taken != handoff->generation;
}

static inline void rv_handoff_take(struct rv_handoff *handoff, void *playing,
                                   const void *next, uint32_t bytes)
{
    uint32_t seen;
    do {
        seen = handoff->generation;
        memcpy(playing, next, bytes);
    } while (seen != handoff->generation);
    handoff->taken = seen;
}

/* A state that takes over at a buffer edge steps the output, which clicks.
 * An effect keeps the state that played as a second instance and fades its
 * output into the new one's over RV_FADE_FRAMES frames, in chunks of
 * RV_FADE_CHUNK_FRAMES. `lead` frames before the fade play the old state alone
 * while both run, for a new state that must fill (a delay line) before it is
 * heard. A new fade restarts from what plays; `remaining = 0` cancels one. */
enum {
    RV_FADE_SHIFT = 7,
    RV_FADE_FRAMES = 1 << RV_FADE_SHIFT,
    RV_FADE_CHUNK_FRAMES = 32
};

struct rv_fade {
    uint32_t remaining; /* lead frames at weight 0, then RV_FADE_FRAMES of blend */
    uint32_t switching; /* rv_fade_run_limited: one state passes the audio through */
};

/* Runs `state` on `frames` interleaved stereo S16 frames in place. */
typedef void (*rv_fade_run_fn)(void *state, int16_t *samples, uint32_t frames);

static inline void rv_fade_start(struct rv_fade *fade, uint32_t lead)
{
    fade->remaining = RV_FADE_FRAMES + lead;
}

/* Runs `playing` over the buffer; while a fade lasts, `fading` too, and mixes
 * the two. The weight runs 1 to RV_FADE_FRAMES so the last faded frame is the
 * new state alone and joins the rest without a step. */
static inline void rv_fade_run(struct rv_fade *fade, rv_fade_run_fn run, void *playing,
                               void *fading, int16_t *samples, uint32_t frames)
{
    while (fade->remaining && frames) {
        int16_t previous[RV_FADE_CHUNK_FRAMES * 2];
        uint32_t chunk = frames < RV_FADE_CHUNK_FRAMES ? frames : RV_FADE_CHUNK_FRAMES;
        if (chunk > fade->remaining)
            chunk = fade->remaining;
        memcpy(previous, samples, chunk * 2 * sizeof *samples);
        run(fading, previous, chunk);
        run(playing, samples, chunk);
        for (uint32_t i = 0; i < chunk * 2; i += 2) {
            int32_t weight = fade->remaining > RV_FADE_FRAMES
                                 ? 0
                                 : (int32_t)(RV_FADE_FRAMES + 1 - fade->remaining);
            for (uint32_t n = i; n < i + 2; n++)
                samples[n] = (int16_t)((previous[n] * (RV_FADE_FRAMES - weight) +
                                        samples[n] * weight) >>
                                       RV_FADE_SHIFT);
            fade->remaining--;
        }
        frames -= chunk;
        samples += chunk * 2;
    }
    run(playing, samples, frames);
}

/* 1 while a fade between a state that passes the audio through and one that
 * runs lasts. Its two outputs are 0.7 ms apart, so a take restarting it then
 * would step the output: an effect leaves a pending set until this clears. */
static inline int rv_fade_switching(const struct rv_fade *fade)
{
    return fade->remaining && fade->switching;
}

/* What an effect keeps beside its two states: the handoff of the next set, the
 * fade between the states and the rate that plays. `api` is the core's table,
 * for the handoff's interrupt masking: `{.api = core}`. */
struct rv_effect {
    struct rv_handoff handoff;
    struct rv_fade fade;
    uint32_t rate;
    const struct rv_api *api;
};

static inline void rv_effect_publish(struct rv_effect *effect, void *next,
                                     const void *set, uint32_t bytes)
{
    rv_handoff_publish(effect->api, &effect->handoff, next, set, bytes);
}

/* 1 when the pump task takes a set before the next buffer: one is pending and
 * no on/off fade lasts, or the rate changed. */
static inline int rv_effect_pending(const struct rv_effect *effect, uint32_t rate)
{
    return (rv_handoff_pending(&effect->handoff) &&
            !rv_fade_switching(&effect->fade)) ||
           rate != effect->rate;
}

/* After a take at `rate`: at the rate that played, the fade from the old state
 * starts with `lead` frames of it alone; a new rate comes with a track, so it
 * ends a fade instead. */
static inline void rv_effect_taken(struct rv_effect *effect, uint32_t rate,
                                   uint32_t lead)
{
    if (rate == effect->rate)
        rv_fade_start(&effect->fade, lead);
    else
        effect->fade.remaining = 0;
    effect->rate = rate;
}

#endif
