/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#include "custom_eq.h"
#include "native/eq-layout.h"

const int32_t cfw_eq_types[4] = {0, 1, 2, 3};
const int32_t cfw_eq_frequencies[32] = {20, 60, 160, 1000, 4000, 8000};
const int32_t cfw_eq_q[38] = {7, 12, 19};
const int32_t cfw_eq_gain[49] = {0, 50, -50, 75, 70, -5};
const int32_t cfw_eq_precut_values[101] = {0, -65};
static unsigned loads, resets, stock_processes, stage_calls[CFW_EQ_BANDS];
static unsigned irq_depth;

uint32_t cfw_irq_save(void)
{
    assert(!irq_depth++);
    return 0x13;
}

void cfw_irq_restore(uint32_t flags)
{
    assert(flags == 0x13 && irq_depth-- == 1);
}

static void osos_eq_reset_stage(void *state, unsigned stage)
{
    unsigned char *base = (unsigned char *)state + stage * OSOS_EQ_STAGE_STRIDE;
    memset(base + 0x20, 0, 32);
}

/* Native Q24 stereo kernel contract, including Q9 histories and S16 stage output. */
static void osos_eq_process_stage(void *state, int16_t *samples, uint32_t frames,
                                  unsigned stage)
{
    assert(!irq_depth && stage < CFW_EQ_BANDS);
    stage_calls[stage]++;
    int32_t *s = (int32_t *)((unsigned char *)state + stage * OSOS_EQ_STAGE_STRIDE);
    for (unsigned i = 0; i < frames; i++) {
        for (unsigned c = 0; c < 2; c++) {
            int32_t input = samples[i * 2 + c] * 512;
            int64_t sum = (int64_t)s[5] * input + (int64_t)s[6] * s[8 + c] +
                          (int64_t)s[7] * s[10 + c] - (int64_t)s[3] * s[12 + c] -
                          (int64_t)s[4] * s[14 + c];
            int32_t output = (int32_t)(sum >> 24);
            s[10 + c] = s[8 + c];
            s[8 + c] = input;
            s[14 + c] = s[12 + c];
            s[12 + c] = output;
            output >>= 9;
            samples[i * 2 + c] = output > 32767    ? 32767
                                 : output < -32768 ? -32768
                                                   : output;
        }
    }
}

static void osos_eq_load(void *state)
{
    assert(osos_eq_preset(state) != CFW_EQ_DSP_PRESET);
    loads++;
}

static void osos_eq_reset(void *state)
{
    resets++;
    for (unsigned i = 0; i < 3; i++)
        osos_eq_reset_stage(state, i);
}

static void osos_eq_process(void *state, int16_t *samples, uint32_t frames,
                            int16_t **output, uint32_t *output_frames)
{
    stock_processes++;
    *output = samples;
    *output_frames = frames;
    for (unsigned i = 0; i < 3; i++) {
        unsigned char *base = (unsigned char *)state + i * OSOS_EQ_STAGE_STRIDE;
        if (!base[OSOS_EQ_BYPASS_OFFSET])
            osos_eq_process_stage(state, samples, frames, i);
    }
}

#include "../../../payload/eq_coefficients.c"
#include "../../../payload/eq_audio.c"

static void process(void *state, int16_t *samples, unsigned frames)
{
    int16_t *output = NULL;
    uint32_t count = 0;
    cfw_eq_process(state, samples, frames, &output, &count);
    assert(output == samples && count == frames);
}

static void signal(int16_t *samples, unsigned frames)
{
    for (unsigned i = 0; i < frames; i++) {
        samples[i * 2] = (int)(i * 71 % 2000) - 1000;
        samples[i * 2 + 1] = (int)(i * 173 % 4000) - 2000;
    }
}

int main(void)
{
    uint32_t native[OSOS_EQ_STATE_BYTES / 4 + 4] = {0};
    uint32_t untouched[sizeof(native) / 4];
    uint32_t values[CFW_EQ_FIELDS] = {0};
    int16_t samples[512], expected[512], previous[512];
    int32_t coefficients[CFW_EQ_BANDS][5];
    uint32_t rate;
    native[OSOS_EQ_PRESET_OFFSET / 4] = CFW_EQ_DSP_PRESET;
    for (unsigned i = OSOS_EQ_STATE_BYTES / 4; i < sizeof(native) / 4; i++)
        native[i] = 0xdeadbeef;
    for (unsigned sample_rate = 44100; sample_rate <= 48000; sample_rate += 3900) {
        native[OSOS_EQ_RATE_OFFSET / 4] = sample_rate;
        values[0] = 1;
        for (unsigned band = 0; band < CFW_EQ_BANDS; band++) {
            values[1 + band * 4] = band < 3 ? 1 : 0;
            values[2 + band * 4] = band + 1;
            values[3 + band * 4] = 0;
            values[4 + band * 4] = 1;
        }
        assert(cfw_eq_publish(values));
        cfw_eq_load(native);
        memcpy(untouched, native, sizeof(native));
        assert(cfw_eq_snapshot(coefficients, &rate) == generation &&
               rate == sample_rate);
        uint32_t legacy[OSOS_EQ_STATE_BYTES / 4] = {0};
        for (unsigned band = 0; band < 3; band++)
            osos_eq_set_stage(legacy, band, coefficients[band]);
        signal(samples, 256);
        memcpy(expected, samples, sizeof(samples));
        int16_t *out;
        uint32_t frames;
        osos_eq_process(legacy, expected, 256, &out, &frames);
        process(native, samples, 256);
        assert(!memcmp(samples, expected, sizeof(samples)));
        assert(!memcmp(native, untouched, sizeof(native)));
        assert(!stage_calls[3] && !stage_calls[4]);

        /* A live edit fades the complete old/new five-stage cascades. */
        struct eq_bank reference_old = active_bank;
        values[CFW_EQ_BAND4_TYPE] = 1;
        values[CFW_EQ_BAND5_TYPE] = 1;
        assert(cfw_eq_publish(values));
        cfw_eq_snapshot(coefficients, &rate);
        struct eq_bank reference_new = {0};
        for (unsigned band = 0; band < CFW_EQ_BANDS; band++)
            osos_eq_set_stage(&reference_new, band, coefficients[band]);
        signal(samples, 256);
        memcpy(expected, samples, sizeof(samples));
        memcpy(previous, samples, sizeof(samples));
        process_bank(&reference_old, previous, 128);
        process_bank(&reference_new, expected, 256);
        for (unsigned i = 0; i < 128; i++)
            for (unsigned c = 0; c < 2; c++) {
                unsigned n = i * 2 + c;
                expected[n] = ((int32_t)previous[n] * (127 - (int)i) +
                               (int32_t)expected[n] * ((int)i + 1)) /
                              128;
            }
        process(native, samples, 17);
        assert(fade_remaining == 111);
        process(native, samples + 34, 239);
        assert(!fade_remaining && !memcmp(samples, expected, sizeof(samples)));
        assert(stage_calls[3] && stage_calls[4]);
        assert(!memcmp(native, untouched, sizeof(native)));

        /* Native reset must clear all five histories and cancel a pending fade. */
        values[CFW_EQ_BAND5_GAIN] = 2;
        assert(cfw_eq_publish(values));
        process(native, samples, 1);
        assert(fade_remaining == 127);
        cfw_eq_reset(native);
        assert(!fade_remaining);
        for (unsigned band = 0; band < CFW_EQ_BANDS; band++) {
            const uint32_t *s = (const uint32_t *)((const unsigned char *)&active_bank +
                                                   band * OSOS_EQ_STAGE_STRIDE + 0x20);
            for (unsigned i = 0; i < 8; i++)
                assert(s[i] == 0);
        }
        memset(stage_calls, 0, sizeof(stage_calls));
    }
    native[OSOS_EQ_RATE_OFFSET / 4] = 32000;
    cfw_eq_load(native);
    assert(!((unsigned char *)native)[OSOS_EQ_ENABLED_OFFSET]);
    signal(samples, 256);
    memcpy(expected, samples, sizeof(samples));
    process(native, samples, 256);
    assert(!memcmp(samples, expected, sizeof(samples)) && !current_state);
    native[OSOS_EQ_RATE_OFFSET / 4] = 44100;
    cfw_eq_load(native);
    assert(current_state == native && !fade_remaining);
    native[OSOS_EQ_PRESET_OFFSET / 4] = 8;
    cfw_eq_load(native);
    assert(loads == 1 && !current_state);
    unsigned before = stock_processes;
    process(native, samples, 1);
    assert(stock_processes == before + 1);
    cfw_eq_reset(native);
    assert(resets == 3);
    for (unsigned i = OSOS_EQ_STATE_BYTES / 4; i < sizeof(native) / 4; i++)
        assert(native[i] == 0xdeadbeef);
    puts("EQ audio: five-stage processing, migration parity, crossfade and reset "
         "passed");
    return 0;
}
