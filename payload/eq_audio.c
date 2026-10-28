/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "eq_coefficients.h"
#include "osos.h"
#include "native/eq-layout.h"
#include "patch.h"

enum {
    FADE_FRAMES = 128,
    FADE_CHUNK_FRAMES = 32,
};

_Static_assert(CFW_EQ_FIELDS == 1 + CFW_EQ_BANDS * 4, "EQ field layout");

static int32_t published[2][CFW_EQ_BANDS][5];
static volatile uint32_t generation;
static volatile uint32_t response_rate = 44100;
static void *current_state;
static uint32_t current_generation;

/* The native EQ is embedded before another effect; keep expanded stages here. */
struct eq_bank {
    uint32_t header[OSOS_EQ_STAGE_OFFSET / 4];
    uint32_t stages[CFW_EQ_BANDS][OSOS_EQ_STAGE_STRIDE / 4];
};
static struct eq_bank active_bank, old_bank;
static unsigned int fade_remaining;

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    int32_t next[2][CFW_EQ_BANDS][5];
    int precut = -cfw_eq_precut_values[values[CFW_EQ_PRECUT]];
    for (unsigned int rate = 0; rate < 2; rate++) {
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
            const uint32_t *v = &values[1 + band * 4];
            int gain = cfw_eq_gain[v[3]];
            if (!cfw_eq_coefficients(next[rate][band], cfw_eq_types[v[0]],
                                     cfw_eq_frequencies[v[1]], cfw_eq_q[v[2]], gain,
                                     band == 0 ? precut : 0, rate ? 48000 : 44100))
                return 0;
        }
    }
    /* Publish both banks together on the single ARM core. */
    uint32_t flags = cfw_irq_save();
    for (unsigned int rate = 0; rate < 2; rate++)
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
            for (unsigned int c = 0; c < 5; c++)
                published[rate][band][c] = next[rate][band][c];
    generation++;
    cfw_irq_restore(flags);
    return 1;
}

/* Snapshot the same quantized bank used by playback; never retain the audio state. */
uint32_t cfw_eq_snapshot(int32_t coefficients[CFW_EQ_BANDS][5], uint32_t *rate)
{
    uint32_t flags = cfw_irq_save();
    *rate = response_rate;
    uint32_t version = generation;
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
        for (unsigned int c = 0; c < 5; c++)
            coefficients[band][c] =
                version ? published[*rate == 48000][band][c] : (c == 0 ? 1 << 24 : 0);
    cfw_irq_restore(flags);
    return version;
}

static void reset_bank(struct eq_bank *bank)
{
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
        osos_eq_reset_stage(bank, band);
}

static void process_bank(struct eq_bank *bank, int16_t *samples, uint32_t frames)
{
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
        const unsigned char *stage =
            (const unsigned char *)bank + band * OSOS_EQ_STAGE_STRIDE;
        if (!stage[OSOS_EQ_BYPASS_OFFSET])
            osos_eq_process_stage(bank, samples, frames, band);
    }
}

PATCH_ARM void cfw_eq_reset(void *state)
{
    osos_eq_reset(state);
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

PATCH_ARM void cfw_eq_load(void *state)
{
    fade_remaining = 0;
    if (osos_eq_preset(state) == CFW_EQ_DSP_PRESET) {
        install_coefficients(state, 1);
    } else {
        current_state = 0;
        osos_eq_load(state);
    }
}

PATCH_ARM void cfw_eq_process(void *state, int16_t *samples, uint32_t frames,
                              int16_t **output, uint32_t *output_frames)
{
    *output = samples;
    *output_frames = frames;
    if (osos_eq_preset(state) != CFW_EQ_DSP_PRESET) {
        osos_eq_process(state, samples, frames, output, output_frames);
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
        for (unsigned int band = 0; band < CFW_EQ_BANDS; band++)
            for (unsigned int word = 0; word < OSOS_EQ_STAGE_STRIDE / 4; word++)
                old_bank.stages[band][word] = active_bank.stages[band][word];
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
