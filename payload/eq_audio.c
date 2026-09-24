#include "custom_eq.h"
#include "eq_coefficients.h"
#include "osos.h"

static int32_t published[2][3][5];
static volatile uint32_t generation;
static void *current_state;
static uint32_t current_generation;
static uint32_t old_state[0xb8 / 4] __attribute__((aligned(8)));
static unsigned int fade_remaining;

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    int32_t next[2][3][5];
    for (unsigned int rate = 0; rate < 2; rate++) {
        for (unsigned int band = 0; band < 3; band++) {
            const uint32_t *v = &values[1 + band * 4];
            if (!cfw_eq_coefficients(next[rate][band], v[0], cfw_eq_frequencies[v[1]],
                                     v[2] + 3, (int)v[3] - 24,
                                     band == 0 ? values[0] : 0, rate ? 48000 : 44100))
                return 0;
        }
    }
    /* Publish both banks together on the single ARM core. */
    uint32_t flags = cfw_irq_save();
    for (unsigned int rate = 0; rate < 2; rate++)
        for (unsigned int band = 0; band < 3; band++)
            for (unsigned int c = 0; c < 5; c++)
                published[rate][band][c] = next[rate][band][c];
    generation++;
    cfw_irq_restore(flags);
    return 1;
}

static int install_coefficients(void *state, int force)
{
    unsigned char *eq = state;
    unsigned int rate = *(uint32_t *)(eq + 4);
    int32_t coefficients[3][5];
    if (rate != 44100 && rate != 48000) {
        eq[8] = 0;
        return 0;
    }
    uint32_t flags = cfw_irq_save();
    uint32_t version = generation;
    if (!force && current_state == state && current_generation == version) {
        cfw_irq_restore(flags);
        return 0;
    }
    for (unsigned int band = 0; band < 3; band++)
        for (unsigned int c = 0; c < 5; c++)
            coefficients[band][c] =
                version ? published[rate == 48000][band][c] : (c == 0 ? 1 << 24 : 0);
    cfw_irq_restore(flags);

    for (unsigned int band = 0; band < 3; band++) {
        unsigned char *stage = eq + band * 0x38;
        *(int32_t *)(stage + 0x14) = coefficients[band][0];
        *(int32_t *)(stage + 0x18) = coefficients[band][1];
        *(int32_t *)(stage + 0x1c) = coefficients[band][2];
        *(int32_t *)(stage + 0x0c) = coefficients[band][3];
        *(int32_t *)(stage + 0x10) = coefficients[band][4];
        stage[0x40] = coefficients[band][0] == (1 << 24) && !coefficients[band][1] &&
                      !coefficients[band][2] && !coefficients[band][3] &&
                      !coefficients[band][4];
    }
    eq[8] = 1;
    osos_eq_reset(state);
    current_state = state;
    current_generation = version;
    return 1;
}

void cfw_eq_load(void *state)
{
    fade_remaining = 0;
    if (*(uint32_t *)((unsigned char *)state + 0xb4) == CFW_EQ_DSP_PRESET) {
        install_coefficients(state, 1);
    } else {
        current_state = 0;
        osos_eq_load(state);
    }
}

void cfw_eq_process(void *state, int16_t *samples, uint32_t frames, int16_t **output,
                    uint32_t *output_frames)
{
    *output = samples;
    *output_frames = frames;
    if (*(uint32_t *)((unsigned char *)state + 0xb4) != CFW_EQ_DSP_PRESET) {
        osos_eq_process(state, samples, frames, output, output_frames);
        return;
    }
    if (current_state != state || current_generation != generation) {
        for (unsigned int i = 0; i < sizeof(old_state) / 4; i++)
            old_state[i] = ((uint32_t *)state)[i];
        if (install_coefficients(state, 0))
            fade_remaining = 128;
    }
    /* Crossfade old and new filters over the first 128 frames of a live edit. */
    while (frames && fade_remaining) {
        int16_t previous[64];
        unsigned int count = frames < 32 ? frames : 32;
        if (count > fade_remaining)
            count = fade_remaining;
        for (unsigned int i = 0; i < count * 2; i++)
            previous[i] = samples[i];
        int16_t *ignored;
        uint32_t ignored_frames;
        osos_eq_process(old_state, previous, count, &ignored, &ignored_frames);
        osos_eq_process(state, samples, count, &ignored, &ignored_frames);
        for (unsigned int i = 0; i < count; i++) {
            int32_t weight = 129 - fade_remaining;
            for (unsigned int channel = 0; channel < 2; channel++) {
                unsigned int n = i * 2 + channel;
                samples[n] = ((int32_t)previous[n] * (128 - weight) +
                              (int32_t)samples[n] * weight) /
                             128;
            }
            fade_remaining--;
        }
        frames -= count;
        samples += count * 2;
    }
    if (frames) {
        int16_t *ignored;
        uint32_t ignored_frames;
        osos_eq_process(state, samples, frames, &ignored, &ignored_frames);
    }
}
