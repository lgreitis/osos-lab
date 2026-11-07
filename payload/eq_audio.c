/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "eq_coefficients.h"
#include "osos.h"
#include "native/eq-layout.h"
#include "patch.h"

enum {
    FADE_FRAMES = 128,
    FADE_CHUNK_FRAMES = 32,
    CASCADE_CHUNK_FRAMES = 32,
    /* Error feedback rest state: half a step, so the first output rounds. */
    ERROR_RESET = 1 << 23,
    PRECUT_LIMIT = 100,
};

_Static_assert(CFW_EQ_FIELDS == 1 + CFW_EQ_BANDS * 4, "EQ field layout");

static int32_t published[2][CFW_EQ_BANDS][5];
static uint32_t published_precut[2];
static volatile uint32_t generation;
static volatile uint32_t response_rate = 44100;
static void *current_state;
static uint32_t current_generation;

/* The native EQ is embedded before another effect; keep expanded stages here. */
struct eq_bank {
    uint32_t header[OSOS_EQ_STAGE_OFFSET / 4];
    uint32_t stages[CFW_EQ_BANDS][OSOS_EQ_STAGE_STRIDE / 4];
    int32_t error[CFW_EQ_BANDS][2];
};
static struct eq_bank active_bank, old_bank;
static unsigned int fade_remaining;

/* Precut found for the last native preset and rate; presets never change. */
static struct {
    uint32_t preset, rate, precut, valid;
} preset_precut;

/* Working samples for the playback task's cascade. */
static int32_t cascade_samples[CASCADE_CHUNK_FRAMES * 2];
/* Error feedback for native presets; the native stage records have no room. */
static int32_t native_error[OSOS_EQ_NATIVE_STAGES][2] = {
    {ERROR_RESET, ERROR_RESET}, {ERROR_RESET, ERROR_RESET}, {ERROR_RESET, ERROR_RESET}};

static unsigned int precut_for_peak(int peak)
{
    return peak <= 0 ? 0 : peak > PRECUT_LIMIT ? PRECUT_LIMIT : (unsigned int)peak;
}

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    int32_t next[2][CFW_EQ_BANDS][5];
    unsigned int applied[2];
    int automatic = values[CFW_EQ_PRECUT] == CFW_EQ_PRECUT_AUTO;
    for (unsigned int rate = 0; rate < 2; rate++) {
        unsigned int hz = rate ? 48000 : 44100;
        unsigned int precut =
            automatic ? 0 : (unsigned int)-cfw_eq_precut_values[values[CFW_EQ_PRECUT]];
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
            const uint32_t *v = &values[1 + band * 4];
            int gain = cfw_eq_gain[v[3]];
            if (!cfw_eq_coefficients(next[rate][band], cfw_eq_types[v[0]],
                                     cfw_eq_frequencies[v[1]], cfw_eq_q[v[2]], gain,
                                     band == 0 ? precut : 0, hz))
                return 0;
        }
        /* Auto precut removes the cascade's peak boost before playback saturates. */
        if (automatic) {
            precut = precut_for_peak(cfw_eq_peak(next[rate], CFW_EQ_BANDS, hz));
            cfw_eq_attenuate(next[rate][0], precut);
        }
        applied[rate] = precut;
    }
    /* Publish both banks together on the single ARM core. */
    uint32_t flags = cfw_irq_save();
    for (unsigned int rate = 0; rate < 2; rate++) {
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
            for (unsigned int c = 0; c < 5; c++)
                published[rate][band][c] = next[rate][band][c];
        published_precut[rate] = applied[rate];
    }
    generation++;
    cfw_irq_restore(flags);
    return 1;
}

/* Snapshot the same quantized bank used by playback; never retain the audio state. */
uint32_t cfw_eq_snapshot(int32_t coefficients[CFW_EQ_BANDS][5], uint32_t *rate,
                         uint32_t *precut)
{
    uint32_t flags = cfw_irq_save();
    *rate = response_rate;
    uint32_t version = generation;
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
        for (unsigned int c = 0; c < 5; c++)
            coefficients[band][c] =
                version ? published[*rate == 48000][band][c] : (c == 0 ? 1 << 24 : 0);
    *precut = version ? published_precut[*rate == 48000] : 0;
    cfw_irq_restore(flags);
    return version;
}

static void reset_bank(struct eq_bank *bank)
{
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
        osos_eq_reset_stage(bank, band);
        bank->error[band][0] = bank->error[band][1] = ERROR_RESET;
    }
}

/* Direct form I with Q24 coefficients and rounded 32-bit Q9 samples. The 24-bit
 * remainder of each output feeds into the next (first-order error feedback), so
 * low-frequency poles do not amplify rounding noise. Starting it at half a step
 * makes the first output round to nearest without a start-up impulse. ARM state provides the
 * smull/smlal multiplies that Thumb would replace with calls. */
static PATCH_ARM __attribute__((noinline)) void
process_stage(unsigned char *stage, int32_t error[2], int32_t *samples,
              unsigned int frames)
{
    const int32_t b0 = *(const int32_t *)(stage + OSOS_EQ_B0_OFFSET),
                  b1 = *(const int32_t *)(stage + OSOS_EQ_B1_OFFSET),
                  b2 = *(const int32_t *)(stage + OSOS_EQ_B2_OFFSET),
                  a1 = -*(const int32_t *)(stage + OSOS_EQ_A1_OFFSET),
                  a2 = -*(const int32_t *)(stage + OSOS_EQ_A2_OFFSET);
    int32_t *history = (int32_t *)(stage + OSOS_EQ_HISTORY_OFFSET);
    for (unsigned int channel = 0; channel < 2; channel++) {
        int32_t x1 = history[channel], x2 = history[2 + channel];
        int32_t y1 = history[4 + channel], y2 = history[6 + channel];
        int32_t e = error[channel];
        for (int32_t *sample = samples + channel, *end = samples + frames * 2;
             sample < end; sample += 2) {
            int32_t x = *sample;
            int64_t acc = (int64_t)b0 * x + (int64_t)b1 * x1 + (int64_t)b2 * x2 +
                          (int64_t)a1 * y1 + (int64_t)a2 * y2 + e;
            int64_t sum = acc >> 24;
            int32_t y = (int32_t)sum;
            e = (int32_t)acc & 0xffffff;
            if (sum != y) {
                y = sum < 0 ? INT32_MIN : INT32_MAX;
                e = ERROR_RESET;
            }
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            *sample = y;
        }
        history[channel] = x1;
        history[2 + channel] = x2;
        history[4 + channel] = y1;
        history[6 + channel] = y2;
        error[channel] = e;
    }
}

/* Unlike the native kernel, keep 42 dB of headroom between stages and saturate
 * to S16 once, so a boost followed by a cut does not clip in between. */
static PATCH_ARM void process_cascade(void *bank, unsigned int stages,
                                      int32_t (*error)[2], int16_t *samples,
                                      uint32_t frames)
{
    unsigned char *base = bank;
    int active = 0;
    for (unsigned int stage = 0; stage < stages; stage++)
        active |= !base[stage * OSOS_EQ_STAGE_STRIDE + OSOS_EQ_BYPASS_OFFSET];
    while (active && frames) {
        unsigned int count =
            frames < CASCADE_CHUNK_FRAMES ? frames : CASCADE_CHUNK_FRAMES;
        for (unsigned int i = 0; i < count * 2; i++)
            cascade_samples[i] = (int32_t)samples[i] * 512;
        for (unsigned int stage = 0; stage < stages; stage++) {
            unsigned char *record = base + stage * OSOS_EQ_STAGE_STRIDE;
            if (!record[OSOS_EQ_BYPASS_OFFSET])
                process_stage(record, error[stage], cascade_samples, count);
        }
        for (unsigned int i = 0; i < count * 2; i++) {
            int32_t value = (cascade_samples[i] >> 9) + ((cascade_samples[i] >> 8) & 1);
            samples[i] = value > 32767 ? 32767 : value < -32768 ? -32768 : value;
        }
        frames -= count;
        samples += count * 2;
    }
}

static void process_bank(struct eq_bank *bank, int16_t *samples, uint32_t frames)
{
    process_cascade(bank, CFW_EQ_BANDS, bank->error, samples, frames);
}

PATCH_ARM void cfw_eq_reset(void *state)
{
    osos_eq_reset(state);
    for (unsigned int stage = 0; stage < OSOS_EQ_NATIVE_STAGES; stage++)
        native_error[stage][0] = native_error[stage][1] = ERROR_RESET;
    if (current_state == state) {
        reset_bank(&active_bank);
        fade_remaining = 0;
    }
}

static int install_coefficients(void *state, int force)
{
    unsigned char *eq = state;
    unsigned int rate = *(uint32_t *)(eq + OSOS_EQ_RATE_OFFSET);
    int32_t coefficients[CFW_EQ_BANDS][5];
    if (rate != 44100 && rate != 48000) {
        eq[OSOS_EQ_ENABLED_OFFSET] = 0;
        current_state = 0;
        fade_remaining = 0;
        return 0;
    }
    uint32_t flags = cfw_irq_save();
    response_rate = rate;
    uint32_t version = generation;
    if (!force && current_state == state && current_generation == version) {
        cfw_irq_restore(flags);
        return 0;
    }
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
        for (unsigned int c = 0; c < 5; c++)
            coefficients[band][c] =
                version ? published[rate == 48000][band][c] : (c == 0 ? 1 << 24 : 0);
    cfw_irq_restore(flags);

    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
        osos_eq_set_stage(&active_bank, band, coefficients[band]);
    eq[OSOS_EQ_ENABLED_OFFSET] = 1;
    reset_bank(&active_bank);
    current_state = state;
    current_generation = version;
    return 1;
}

/* Apple's presets boost up to 7.5 dB without precut. Load them into a copy,
 * attenuate the first active stage by the cascade's peak, then install. */
static void load_native_preset(void *state)
{
    uint32_t copy[OSOS_EQ_STATE_BYTES / 4];
    unsigned char *eq = state, *loaded = (unsigned char *)copy;
    for (unsigned int i = 0; i < OSOS_EQ_STATE_BYTES / 4; i++)
        copy[i] = ((const uint32_t *)state)[i];
    osos_eq_load(copy);

    uint32_t preset = osos_eq_preset(copy);
    uint32_t rate = *(const uint32_t *)(loaded + OSOS_EQ_RATE_OFFSET);
    int32_t active[OSOS_EQ_NATIVE_STAGES][5];
    unsigned int count = 0, first = OSOS_EQ_NATIVE_STAGES;
    for (unsigned int stage = 0; stage < OSOS_EQ_NATIVE_STAGES; stage++) {
        const unsigned char *record = loaded + stage * OSOS_EQ_STAGE_STRIDE;
        if (record[OSOS_EQ_BYPASS_OFFSET])
            continue;
        if (first == OSOS_EQ_NATIVE_STAGES)
            first = stage;
        active[count][0] = *(const int32_t *)(record + OSOS_EQ_B0_OFFSET);
        active[count][1] = *(const int32_t *)(record + OSOS_EQ_B1_OFFSET);
        active[count][2] = *(const int32_t *)(record + OSOS_EQ_B2_OFFSET);
        active[count][3] = *(const int32_t *)(record + OSOS_EQ_A1_OFFSET);
        active[count][4] = *(const int32_t *)(record + OSOS_EQ_A2_OFFSET);
        count++;
    }
    if (loaded[OSOS_EQ_ENABLED_OFFSET] && count) {
        if (!preset_precut.valid || preset_precut.preset != preset ||
            preset_precut.rate != rate) {
            preset_precut.precut = precut_for_peak(cfw_eq_peak(active, count, rate));
            preset_precut.preset = preset;
            preset_precut.rate = rate;
            preset_precut.valid = 1;
        }
        int32_t *b =
            (int32_t *)(loaded + first * OSOS_EQ_STAGE_STRIDE + OSOS_EQ_B0_OFFSET);
        cfw_eq_attenuate(b, preset_precut.precut);
    }

    /* Install coefficients, bypass flags and enable together; keep histories. */
    uint32_t flags = cfw_irq_save();
    eq[OSOS_EQ_ENABLED_OFFSET] = loaded[OSOS_EQ_ENABLED_OFFSET];
    for (unsigned int stage = 0; stage < OSOS_EQ_NATIVE_STAGES; stage++) {
        unsigned int base = stage * OSOS_EQ_STAGE_STRIDE;
        for (unsigned int offset = OSOS_EQ_A1_OFFSET; offset < OSOS_EQ_HISTORY_OFFSET;
             offset += 4)
            *(uint32_t *)(eq + base + offset) =
                *(const uint32_t *)(loaded + base + offset);
        eq[base + OSOS_EQ_BYPASS_OFFSET] = loaded[base + OSOS_EQ_BYPASS_OFFSET];
    }
    cfw_irq_restore(flags);
}

PATCH_ARM void cfw_eq_load(void *state)
{
    fade_remaining = 0;
    if (osos_eq_preset(state) == CFW_EQ_DSP_PRESET) {
        install_coefficients(state, 1);
    } else {
        current_state = 0;
        load_native_preset(state);
    }
}

PATCH_ARM void cfw_eq_process(void *state, int16_t *samples, uint32_t frames,
                              int16_t **output, uint32_t *output_frames)
{
    *output = samples;
    *output_frames = frames;
    if (osos_eq_preset(state) != CFW_EQ_DSP_PRESET) {
        process_cascade(state, OSOS_EQ_NATIVE_STAGES, native_error, samples, frames);
        return;
    }
    unsigned int rate = *(uint32_t *)((unsigned char *)state + OSOS_EQ_RATE_OFFSET);
    if (rate != 44100 && rate != 48000) {
        current_state = 0;
        fade_remaining = 0;
        return;
    }
    if (current_state != state) {
        fade_remaining = 0;
        install_coefficients(state, 1);
    } else if (current_generation != generation) {
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
            for (unsigned int word = 0; word < OSOS_EQ_STAGE_STRIDE / 4; word++)
                old_bank.stages[band][word] = active_bank.stages[band][word];
            old_bank.error[band][0] = active_bank.error[band][0];
            old_bank.error[band][1] = active_bank.error[band][1];
        }
        if (install_coefficients(state, 0))
            fade_remaining = FADE_FRAMES;
    }
    /* Crossfade complete cascades, including both added stages. */
    while (frames && fade_remaining) {
        int16_t previous[FADE_CHUNK_FRAMES * 2];
        unsigned int count = frames < FADE_CHUNK_FRAMES ? frames : FADE_CHUNK_FRAMES;
        if (count > fade_remaining)
            count = fade_remaining;
        for (unsigned int i = 0; i < count * 2; i++)
            previous[i] = samples[i];
        process_bank(&old_bank, previous, count);
        process_bank(&active_bank, samples, count);
        for (unsigned int i = 0; i < count; i++) {
            int32_t weight = FADE_FRAMES + 1 - fade_remaining;
            for (unsigned int channel = 0; channel < 2; channel++) {
                unsigned int n = i * 2 + channel;
                samples[n] = ((int32_t)previous[n] * (FADE_FRAMES - weight) +
                              (int32_t)samples[n] * weight) /
                             FADE_FRAMES;
            }
            fade_remaining--;
        }
        frames -= count;
        samples += count * 2;
    }
    if (frames)
        process_bank(&active_bank, samples, frames);
}
