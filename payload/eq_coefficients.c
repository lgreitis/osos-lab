/* SPDX-License-Identifier: GPL-3.0-only */

#include "eq_coefficients.h"

#define SCALE 16777216

/* The supported gains keep |x| below 1.2. */
static double exponential(double x)
{
    double sum = 1.0, term = 1.0;
    for (unsigned int n = 1; n <= 18; n++) {
        term *= x / n;
        sum += term;
    }
    return sum;
}

/* Frequencies stay below Nyquist, so 0 <= x < pi. */
static void sine_cosine(double x, double *sine, double *cosine)
{
    double s = x, c = 1.0, st = x, ct = 1.0;
    for (unsigned int n = 1; n <= 11; n++) {
        st *= -x * x / ((2 * n) * (2 * n + 1));
        ct *= -x * x / ((2 * n - 1) * (2 * n));
        s += st;
        c += ct;
    }
    *sine = s;
    *cosine = c;
}

int cfw_eq_coefficients(int32_t out[5], unsigned int type, unsigned int hz,
                        unsigned int q_tenths, int gain_tenths_db,
                        unsigned int precut_tenths_db, unsigned int rate)
{
    if (type > CFW_EQ_HIGH_SHELF || hz < 20 || hz > 20000 || q_tenths < 3 ||
        q_tenths > 40 || gain_tenths_db < -120 || gain_tenths_db > 120 ||
        precut_tenths_db > 100 || (rate != 44100 && rate != 48000))
        return 0;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    double attenuation = exponential(-2.302585092994045684 * precut_tenths_db / 200.0);
    if (type != CFW_EQ_OFF && gain_tenths_db != 0) {
        double s, c;
        sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
        double a = exponential(2.302585092994045684 * gain_tenths_db / 400.0);
        double alpha = s * 5.0 / q_tenths;
        double beta =
            2.0 * exponential(2.302585092994045684 * gain_tenths_db / 800.0) * alpha;

        /* RBJ Audio EQ Cookbook, using Q for both bells and shelves.
         * https://www.w3.org/TR/audio-eq-cookbook/ */
        if (type == CFW_EQ_PEAKING) {
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * c;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha / a;
        } else if (type == CFW_EQ_LOW_SHELF) {
            b0 = a * ((a + 1.0) - (a - 1.0) * c + beta);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * c);
            b2 = a * ((a + 1.0) - (a - 1.0) * c - beta);
            a0 = (a + 1.0) + (a - 1.0) * c + beta;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * c);
            a2 = (a + 1.0) + (a - 1.0) * c - beta;
        } else {
            b0 = a * ((a + 1.0) + (a - 1.0) * c + beta);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * c);
            b2 = a * ((a + 1.0) + (a - 1.0) * c - beta);
            a0 = (a + 1.0) - (a - 1.0) * c + beta;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * c);
            a2 = (a + 1.0) - (a - 1.0) * c - beta;
        }
    }
    double values[5] = {b0 * attenuation / a0, b1 * attenuation / a0,
                        b2 * attenuation / a0, a1 / a0, a2 / a0};
    for (unsigned int i = 0; i < 5; i++) {
        double scaled = values[i] * SCALE;
        if (!(scaled > -2147483647.0 && scaled < 2147483647.0))
            return 0;
        out[i] = (int32_t)(scaled + (scaled < 0.0 ? -0.5 : 0.5));
    }
    /* Check the denominator after fixed-point quantization (Jury criterion). */
    return out[4] > -SCALE && out[4] < SCALE && (int64_t)SCALE + out[3] + out[4] > 0 &&
           (int64_t)SCALE - out[3] + out[4] > 0;
}

/* Natural log via range reduction and the rapidly converging atanh series. */
static double logarithm(double value)
{
    int power = 0;
    while (value >= 2.0) {
        value *= 0.5;
        power++;
    }
    while (value < 1.0) {
        value *= 2.0;
        power--;
    }
    double z = (value - 1.0) / (value + 1.0), term = z, sum = z;
    for (unsigned int n = 3; n <= 23; n += 2) {
        term *= z * z;
        sum += term / n;
    }
    return 2.0 * sum + power * 0.693147180559945309;
}

int cfw_eq_response(const int32_t coefficients[CFW_EQ_BANDS][5], double hz,
                    unsigned int rate)
{
    if (!(hz >= 20.0 && hz <= 20000.01) || (rate != 44100 && rate != 48000))
        return 0;
    double s, c;
    sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
    double c2 = c * c - s * s, s2 = 2.0 * s * c, power = 1.0;
    for (unsigned int band = 0; band < CFW_EQ_BANDS; band++) {
        const int32_t *k = coefficients[band];
        double nr = k[0] + k[1] * c + k[2] * c2;
        double ni = k[1] * s + k[2] * s2;
        double dr = SCALE + k[3] * c + k[4] * c2;
        double di = k[3] * s + k[4] * s2;
        double denominator = dr * dr + di * di;
        if (!(denominator > 0.0))
            return 480;
        power *= (nr * nr + ni * ni) / denominator;
    }
    /* Bound the plot, including singular or underflowed responses. */
    if (!(power > 0.000015848931924611))
        return -480;
    if (!(power < 63095.7344480193))
        return 480;
    double tenths = logarithm(power) * 43.42944819032518;
    return (int)(tenths + (tenths < 0.0 ? -0.5 : 0.5));
}
