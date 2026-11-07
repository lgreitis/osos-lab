/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#include "custom_eq.h"
#include "native/eq-layout.h"

enum { BASS_PRESET = 8, FRAMES = 2048 };

const int32_t cfw_eq_types[4] = {0, 1, 2, 3};
const int32_t cfw_eq_frequencies[32] = {20, 60, 160, 1000, 4000, 8000};
const int32_t cfw_eq_q[38] = {7, 12, 19};
const int32_t cfw_eq_gain[49] = {0, 50, -50, 75, 70, -5, 120, -120, 60};
const int32_t cfw_eq_precut_values[102] = {1, 0, -65};
static unsigned loads, resets;
static unsigned irq_depth;
static void *real_state;

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
    memset(base + OSOS_EQ_HISTORY_OFFSET, 0, 32);
}

#include "../../../payload/eq_coefficients.c"

/* Apple's loader contract: coefficients, bypass flags and enable from preset and rate. */
static void osos_eq_load(void *state)
{
    unsigned char *eq = state;
    uint32_t preset = osos_eq_preset(state);
    uint32_t rate = *(uint32_t *)(eq + OSOS_EQ_RATE_OFFSET);
    assert(preset != CFW_EQ_DSP_PRESET && state != real_state);
    loads++;
    static const int32_t identity[5] = {1 << 24};
    for (unsigned stage = 0; stage < OSOS_EQ_NATIVE_STAGES; stage++)
        osos_eq_set_stage(state, stage, identity);
    eq[OSOS_EQ_ENABLED_OFFSET] = preset && (rate == 44100 || rate == 48000);
    if (preset == BASS_PRESET && eq[OSOS_EQ_ENABLED_OFFSET]) {
        int32_t shelf[5];
        assert(cfw_eq_coefficients(shelf, CFW_EQ_LOW_SHELF, 1000, 7, 60, 0, rate));
        osos_eq_set_stage(state, 1, shelf);
    }
}

static void osos_eq_reset(void *state)
{
    resets++;
    for (unsigned i = 0; i < OSOS_EQ_NATIVE_STAGES; i++)
        osos_eq_reset_stage(state, i);
}

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

static void sine(int16_t *samples, unsigned frames, double hz, double amplitude)
{
    for (unsigned i = 0; i < frames; i++)
        samples[i * 2] = samples[i * 2 + 1] =
            (int16_t)lrint(amplitude * sin(2 * M_PI * hz * i / 44100));
}

/* Double-precision cascade from rest; returns the largest unclipped magnitude. */
static double reference(const int32_t (*k)[5], unsigned stages, const int16_t *in,
                        int16_t *out, unsigned frames)
{
    double peak = 0;
    for (unsigned c = 0; c < 2; c++) {
        double h[CFW_EQ_BANDS][4] = {{0}};
        for (unsigned i = 0; i < frames; i++) {
            double x = in[i * 2 + c];
            for (unsigned s = 0; s < stages; s++) {
                double y = (k[s][0] * x + k[s][1] * h[s][0] + k[s][2] * h[s][1] -
                            k[s][3] * h[s][2] - k[s][4] * h[s][3]) /
                           16777216.0;
                h[s][1] = h[s][0];
                h[s][0] = x;
                h[s][3] = h[s][2];
                h[s][2] = y;
                x = y;
            }
            peak = fmax(peak, fabs(x));
            long rounded = lrint(x);
            out[i * 2 + c] = rounded > 32767    ? 32767
                             : rounded < -32768 ? -32768
                                                : rounded;
        }
    }
    return peak;
}

static int max_difference(const int16_t *a, const int16_t *b, unsigned frames)
{
    int worst = 0;
    for (unsigned i = 0; i < frames * 2; i++) {
        int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        worst = d > worst ? d : worst;
    }
    return worst;
}

static void custom(uint32_t values[CFW_EQ_FIELDS], uint32_t precut)
{
    memset(values, 0, CFW_EQ_FIELDS * sizeof(*values));
    values[CFW_EQ_PRECUT] = precut;
    for (unsigned band = 0; band < CFW_EQ_BANDS; band++)
        values[CFW_EQ_BAND1_Q + band * 4] = 0;
}

int main(void)
{
    uint32_t native[OSOS_EQ_STATE_BYTES / 4 + 4] = {0};
    uint32_t untouched[sizeof(native) / 4];
    uint32_t values[CFW_EQ_FIELDS] = {0};
    static int16_t samples[FRAMES * 2], expected[FRAMES * 2], previous[FRAMES * 2];
    int32_t coefficients[CFW_EQ_BANDS][5];
    uint32_t rate, precut;
    real_state = native;
    native[OSOS_EQ_PRESET_OFFSET / 4] = CFW_EQ_DSP_PRESET;
    for (unsigned i = OSOS_EQ_STATE_BYTES / 4; i < sizeof(native) / 4; i++)
        native[i] = 0xdeadbeef;
    for (unsigned sample_rate = 44100; sample_rate <= 48000; sample_rate += 3900) {
        native[OSOS_EQ_RATE_OFFSET / 4] = sample_rate;
        /* Manual 6.5 dB precut and three bells match a double-precision cascade. */
        values[CFW_EQ_PRECUT] = 2;
        for (unsigned band = 0; band < CFW_EQ_BANDS; band++) {
            values[1 + band * 4] = band < 3 ? 1 : 0;
            values[2 + band * 4] = band + 1;
            values[3 + band * 4] = 0;
            values[4 + band * 4] = 1;
        }
        assert(cfw_eq_publish(values));
        cfw_eq_load(native);
        memcpy(untouched, native, sizeof(native));
        assert(cfw_eq_snapshot(coefficients, &rate, &precut) == generation &&
               rate == sample_rate && precut == 65);
        signal(samples, 256);
        reference(coefficients, CFW_EQ_BANDS, samples, expected, 256);
        process(native, samples, 256);
        assert(max_difference(samples, expected, 256) <= 1);
        assert(!memcmp(native, untouched, sizeof(native)));
        for (unsigned band = 3; band < CFW_EQ_BANDS; band++) {
            const int32_t *h =
                (const int32_t *)((const unsigned char *)&active_bank +
                                  band * OSOS_EQ_STAGE_STRIDE + OSOS_EQ_HISTORY_OFFSET);
            for (unsigned i = 0; i < 8; i++)
                assert(!h[i]);
        }

        /* A live edit fades the complete old/new five-stage cascades. */
        struct eq_bank reference_old = active_bank;
        values[CFW_EQ_BAND4_TYPE] = 1;
        values[CFW_EQ_BAND5_TYPE] = 1;
        assert(cfw_eq_publish(values));
        cfw_eq_snapshot(coefficients, &rate, &precut);
        struct eq_bank reference_new = {0};
        for (unsigned band = 0; band < CFW_EQ_BANDS; band++)
            osos_eq_set_stage(&reference_new, band, coefficients[band]);
        reset_bank(&reference_new);
        signal(samples, 256);
        memcpy(expected, samples, 512 * sizeof(*samples));
        memcpy(previous, samples, 512 * sizeof(*samples));
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
        assert(!fade_remaining && !memcmp(samples, expected, 512 * sizeof(*samples)));
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
                                                   band * OSOS_EQ_STAGE_STRIDE +
                                                   OSOS_EQ_HISTORY_OFFSET);
            for (unsigned i = 0; i < 8; i++)
                assert(s[i] == 0);
        }
    }

    /* A +12 dB bell followed by its -12 dB inverse is transparent at 30000 peak;
     * the native kernel saturated the 63000 peak between the two stages. */
    native[OSOS_EQ_RATE_OFFSET / 4] = 44100;
    custom(values, 1);
    values[CFW_EQ_BAND1_TYPE] = values[CFW_EQ_BAND2_TYPE] = 1;
    values[CFW_EQ_BAND1_FREQUENCY] = values[CFW_EQ_BAND2_FREQUENCY] = 3;
    values[CFW_EQ_BAND1_GAIN] = 6;
    values[CFW_EQ_BAND2_GAIN] = 7;
    assert(cfw_eq_publish(values));
    cfw_eq_load(native);
    cfw_eq_snapshot(coefficients, &rate, &precut);
    assert(precut == 0);
    sine(samples, FRAMES, 1000, 30000);
    assert(reference(coefficients, 1, samples, expected, FRAMES) > 60000);
    memcpy(previous, samples, sizeof(samples));
    process(native, samples, FRAMES);
    assert(max_difference(samples, previous, FRAMES) <= 2);

    /* Auto precut removes a +6 dB low shelf's boost; 0 dB would clip. */
    custom(values, 1);
    values[CFW_EQ_BAND1_TYPE] = 2;
    values[CFW_EQ_BAND1_FREQUENCY] = 3;
    values[CFW_EQ_BAND1_GAIN] = 8;
    assert(cfw_eq_publish(values));
    cfw_eq_snapshot(coefficients, &rate, &precut);
    sine(samples, FRAMES, 100, 32767);
    assert(reference(coefficients, CFW_EQ_BANDS, samples, expected, FRAMES) > 60000);
    values[CFW_EQ_PRECUT] = CFW_EQ_PRECUT_AUTO;
    assert(cfw_eq_publish(values));
    cfw_eq_snapshot(coefficients, &rate, &precut);
    assert(precut >= 60 && precut <= 61 && cfw_eq_peak(coefficients, 5, 44100) <= 0);
    cfw_eq_load(native);
    assert(reference(coefficients, CFW_EQ_BANDS, samples, expected, FRAMES) <= 32767.5);
    process(native, samples, FRAMES);
    assert(max_difference(samples, expected, FRAMES) <= 1);

    /* Error feedback keeps low-frequency poles from amplifying rounding noise:
     * a 20 Hz bell and a 60 Hz shelf, both +12 dB, track the double cascade, and reset clears the feedback. */
    custom(values, 1);
    values[CFW_EQ_BAND1_TYPE] = 1;
    values[CFW_EQ_BAND1_Q] = 2;
    values[CFW_EQ_BAND1_GAIN] = 6;
    values[CFW_EQ_BAND2_TYPE] = 2;
    values[CFW_EQ_BAND2_FREQUENCY] = 1;
    values[CFW_EQ_BAND2_GAIN] = 6;
    assert(cfw_eq_publish(values));
    cfw_eq_load(native);
    cfw_eq_snapshot(coefficients, &rate, &precut);
    sine(samples, FRAMES, 1000, 1000);
    reference(coefficients, CFW_EQ_BANDS, samples, expected, FRAMES);
    process(native, samples, FRAMES);
    double error = 0;
    for (unsigned i = 0; i < FRAMES * 2; i++)
        error += (double)(samples[i] - expected[i]) * (samples[i] - expected[i]);
    assert(sqrt(error / (FRAMES * 2)) < 0.2);
    cfw_eq_reset(native);
    for (unsigned band = 0; band < CFW_EQ_BANDS; band++)
        assert(active_bank.error[band][0] == ERROR_RESET &&
               active_bank.error[band][1] == ERROR_RESET);
    for (unsigned stage = 0; stage < OSOS_EQ_NATIVE_STAGES; stage++)
        assert(native_error[stage][0] == ERROR_RESET &&
               native_error[stage][1] == ERROR_RESET);

    /* Native presets load into a copy, gain the cascade's peak as precut and
     * keep the playback histories. */
    native[OSOS_EQ_PRESET_OFFSET / 4] = BASS_PRESET;
    unsigned char *eq = (unsigned char *)native;
    int32_t *history = (int32_t *)(eq + OSOS_EQ_STAGE_STRIDE + OSOS_EQ_HISTORY_OFFSET);
    history[0] = 1234;
    cfw_eq_load(native);
    assert(loads == 1 && !current_state && eq[OSOS_EQ_ENABLED_OFFSET]);
    assert(eq[OSOS_EQ_BYPASS_OFFSET] &&
           !eq[OSOS_EQ_STAGE_STRIDE + OSOS_EQ_BYPASS_OFFSET]);
    assert(history[0] == 1234);
    int32_t shelf[5], loaded[5];
    assert(cfw_eq_coefficients(shelf, CFW_EQ_LOW_SHELF, 1000, 7, 60, 0, 44100));
    memcpy(loaded, eq + OSOS_EQ_STAGE_STRIDE + OSOS_EQ_A1_OFFSET, 2 * sizeof(int32_t));
    assert(loaded[0] == shelf[3] && loaded[1] == shelf[4]);
    int peak = cfw_eq_peak((const int32_t (*)[5])shelf, 1, 44100);
    assert(peak >= 60 && peak <= 61 && preset_precut.precut == (uint32_t)peak);
    int32_t attenuated[5] = {shelf[0], shelf[1], shelf[2], shelf[3], shelf[4]};
    cfw_eq_attenuate(attenuated, peak);
    assert(!memcmp(eq + OSOS_EQ_STAGE_STRIDE + OSOS_EQ_B0_OFFSET, attenuated, 12));
    cfw_eq_load(native);
    assert(loads == 2 &&
           !memcmp(eq + OSOS_EQ_STAGE_STRIDE + OSOS_EQ_B0_OFFSET, attenuated, 12));
    cfw_eq_reset(native);
    sine(samples, FRAMES, 100, 32767);
    assert(reference((const int32_t (*)[5])attenuated, 1, samples, expected, FRAMES) <=
           32767.5);
    process(native, samples, FRAMES);
    assert(max_difference(samples, expected, FRAMES) <= 1);

    /* Unsupported rates disable both paths and leave audio untouched. */
    native[OSOS_EQ_RATE_OFFSET / 4] = 32000;
    cfw_eq_load(native);
    assert(!eq[OSOS_EQ_ENABLED_OFFSET]);
    native[OSOS_EQ_PRESET_OFFSET / 4] = CFW_EQ_DSP_PRESET;
    cfw_eq_load(native);
    assert(!eq[OSOS_EQ_ENABLED_OFFSET]);
    signal(samples, 256);
    memcpy(expected, samples, 512 * sizeof(*samples));
    process(native, samples, 256);
    assert(!memcmp(samples, expected, 512 * sizeof(*samples)) && !current_state);
    native[OSOS_EQ_RATE_OFFSET / 4] = 44100;
    cfw_eq_load(native);
    assert(current_state == native && !fade_remaining);
    unsigned before = resets;
    cfw_eq_reset(native);
    assert(resets == before + 1);
    for (unsigned i = OSOS_EQ_STATE_BYTES / 4; i < sizeof(native) / 4; i++)
        assert(native[i] == 0xdeadbeef);
    puts("EQ audio: headroom cascade, automatic precut, preset precut, crossfade "
         "and reset passed");
    return 0;
}
