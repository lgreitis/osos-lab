# Audio effects

An audio effect filters the output PCM at `RV_SITE_AudioOutputQueue_Get_call`, after Apple's EQ, for everything the iPod plays. Bind with `RV_HOOK_FILTER_AT` at the effect's stage priority from `rv_audio.h`, which fixes the order of the effects.

| Stage | Priority |
|---|---|
| `RV_AUDIO_STAGE_CROSSFEED` | 100 |
| `RV_AUDIO_STAGE_TONE` | 200 |
| `RV_AUDIO_STAGE_BASS_ENHANCEMENT` | 300 |
| `RV_AUDIO_STAGE_FATIGUE_REDUCTION` | 400 |
| `RV_AUDIO_STAGE_SURROUND` | 500 |
| `RV_AUDIO_STAGE_CHANNELS` | 600 |
| `RV_AUDIO_STAGE_COMPRESSOR` | 700 |
| `RV_AUDIO_STAGE_BALANCE` | 800 |
| `RV_AUDIO_STAGE_DITHER` | 900 |

```c
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_COMPRESSOR),
};
```

The stream is 16-bit stereo at the output sample rate, 8 to 48 kHz. An effect set to Off should pass the buffer through without per-sample work. Build per-sample loops as ARM, where a 32 x 32 to 64-bit multiply is one `smull` instead of a library call; see [Modules](modules.md).

## Shared helpers

`rv_audio.h` holds the shared helpers as inline functions. The limiter (`struct rv_limiter`, `rv_limiter_rest`), the fade behind it, the filter designs and the double-precision maths live in the `dsp` library extension ([Libraries](modules.md#libraries)): an effect lists `requires = ["dsp"]` and includes `rv_dsp.h`.

| Call | |
|---|---|
| `rv_audio_stereo16(context, frames, min_rate, max_rate, &rate)` | 1 when `frames` is not 0 and the output behind the site's third argument is 16-bit stereo at a rate from `min_rate` to `max_rate`, with `rate` that rate |
| `rv_fracmul(a, b)` | a times b, both Q31 |
| `rv_saturate16(sample)` | clamped to S16 |
| `struct rv_handoff`, `rv_handoff_publish(api, &handoff, &next, set, bytes)`, `rv_handoff_pending(&handoff)`, `rv_handoff_take(&handoff, &playing, &next, bytes)` | a set of values passed from the UI task to the pump task. `publish` copies the set into `next` with interrupts masked and bumps the generation; `api->irq_save` and `api->irq_restore` mask them, for a few instructions, in any handoff of an extension's own. When `pending` reports a new generation, the pump task calls `take`, which copies `next` into `playing`, retrying if the generation moved during the copy |
| `struct rv_fade`, `rv_fade_start(&fade, lead)`, `rv_fade_run(&fade, run, &playing, &fading, samples, frames)` | a fade from the old state to the new. `run` processes one state over a buffer in place. `rv_fade_run` runs `playing` and, during a fade, also runs `fading` on a copy and mixes the two |
| `rv_fade_switching(&fade)` | 1 while a fade between a pass-through state and a running one lasts, see below |
| `struct rv_effect`, `rv_effect_publish(&effect, &next, set, bytes)`, `rv_effect_pending(&effect, rate)`, `rv_effect_taken(&effect, rate, lead)` | what an effect keeps beside its two states: the handoff, the fade and the rate that plays, with `.api` set to the core's table. `pending` is 1 when a set waits and no on/off fade lasts, or the rate changed; `taken`, after a take, starts the fade with `lead` frames at the rate that played and ends it on a new rate |

```c
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context, const void *next)
{
    uint32_t rate;
    if (rv_audio_stereo16((const void *)context, frames, 0, UINT32_MAX, &rate))
        effect_process((int16_t *)samples, frames, rate);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}
```

## Changing a set while audio plays

Switching sets at a buffer edge clicks. Keep the old state as a second instance instead: on take, copy `playing` into `fading`, install the new set in `playing` and call `rv_fade_start`. `rv_fade_run` then blends old into new over `RV_FADE_FRAMES` (128, about 3 ms at 44.1 kHz), in chunks of `RV_FADE_CHUNK_FRAMES`.

A new state that must fill first, such as a look-ahead delay line, passes its length as `lead`; for those frames only the old state is heard. On a sample rate change, which comes with a new track, do not fade: clear the state's history and end the fade; `rv_effect_taken` ends the fade, the effect clears its own state. A set published during a fade restarts it from the current output.

## Keeping peaks under full scale

A precut equal to an effect's largest gain does not prevent overs, because phase shifts can line up peaks. An effect that can go over writes int32 output at the S16 scale and passes it to an `rv_limiter` instead of saturating. The limiter ramps the gain down linearly over the 32 frames before an over, holds it until the last over has played, and returns to unity with a time constant of 4,096 frames (about 90 ms at 44.1 kHz). Both channels share the gain. At unity, samples pass unchanged, 32 frames late. Input above +24 dB is clamped first. A zeroed limiter is at rest.

With fades, call `rv_fade_run_limited` instead of `rv_fade_run`. Its `run` reads S16 from `in`, writes at most `RV_LIMITER_CHUNK_FRAMES` wide frames to `out` and returns 1, or returns 0 without writing for a pass-through state such as Off. Two running states blend before the shared limiter. If one passes through, the other's limited output blends with the undelayed buffer. When turning on, rest the limiter and start the fade with a lead of at least `RV_LIMITER_FRAMES`. A pass-through `playing` with no fade costs nothing per sample.

Turning on or off shifts the output by the limiter's 32 frames, inside the blend. The blend is a brief comb filter (notches every 1,378 Hz from 689 Hz at 44.1 kHz). `rv_fade_switching(&fade)` is 1 during such a blend; take a pending set only once it is 0, as `rv_effect_pending` does:

```c
if (rv_effect_pending(&effect, rate))
    take(rate);
```

```c
static int run(void *state, const int16_t *in, int32_t *out, uint32_t frames)
{
    if (((struct state *)state)->mode == MODE_OFF)
        return 0;
    /* ... out[i] = wide result, rounded to the S16 scale ... */
    return 1;
}

rv_fade_run_limited(&effect.fade, &limiter, run, &playing, &fading, samples, frames);
```

`rv_dsp.h` in the `dsp` library also has the biquad designs (`rv_dsp_biquad`, `rv_dsp_bishelf`, `rv_dsp_attenuate`, `rv_dsp_response`, `rv_dsp_peak`), the cascade that runs them with headroom between stages (`struct rv_dsp_stage`, `rv_dsp_stage_set`, `rv_dsp_cascade_run`, `rv_dsp_cascade_rest`) and, for filter designs and editor curves, `rv_exp`, `rv_log`, `rv_sin_cos`, `rv_sqrt` and `rv_decibels` in double precision. Use the double-precision calls on the UI task only.
