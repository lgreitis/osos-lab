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
                        unsigned int q_tenths, int gain_half_db,
                        unsigned int precut_half_db, unsigned int rate)
{
    if (type > CFW_EQ_HIGH_SHELF || hz < 20 || hz > 20000 || q_tenths < 3 ||
        q_tenths > 40 || gain_half_db < -24 || gain_half_db > 24 ||
        precut_half_db > 20 || (rate != 44100 && rate != 48000))
        return 0;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    double attenuation = exponential(-2.302585092994045684 * precut_half_db / 40.0);
    if (type != CFW_EQ_OFF && gain_half_db != 0) {
        double s, c;
        sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
        double a = exponential(2.302585092994045684 * gain_half_db / 80.0);
        double alpha = s * 5.0 / q_tenths;
        double beta =
            2.0 * exponential(2.302585092994045684 * gain_half_db / 160.0) * alpha;

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
