/* SPDX-License-Identifier: GPL-3.0-only */

#include "eq_coefficients.h"

#define SCALE 16777216

/* Callers keep |x| below 1.2: decibel gains and the peak grid ratio. */
static __attribute__((noinline)) double exponential(double x)
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

/* 10^(tenths / divisor): divisor 200 turns tenths of a decibel into an amplitude,
 * 400 into the RBJ gain A. */
static double decibels(int tenths, unsigned int divisor)
{
    return exponential(2.302585092994045684 * tenths / divisor);
}

/* b / a[0] and a[1..2] / a[0] with 24 fractional bits; fails when one does not fit. */
static int quantize(int32_t out[5], const double b[3], const double a[3])
{
    for (unsigned int i = 0; i < 5; i++) {
        double scaled = (i < 3 ? b[i] : a[i - 2]) / a[0] * SCALE;
        if (!(scaled > -2147483647.0 && scaled < 2147483647.0))
            return 0;
        out[i] = (int32_t)(scaled + (scaled < 0.0 ? -0.5 : 0.5));
    }
    return 1;
}

int cfw_eq_coefficients(int32_t out[5], unsigned int type, unsigned int hz,
                        unsigned int q_tenths, int gain_tenths_db,
                        unsigned int precut_tenths_db, unsigned int rate)
{
    if (type > CFW_EQ_HIGH_SHELF || hz < 20 || hz > 20000 || q_tenths < 3 ||
        q_tenths > 40 || gain_tenths_db < -120 || gain_tenths_db > 120 ||
        precut_tenths_db > 100 || (rate != 44100 && rate != 48000))
        return 0;

    double b[3] = {1.0, 0.0, 0.0}, d[3] = {1.0, 0.0, 0.0};
    double attenuation = decibels(-(int)precut_tenths_db, 200);
    if (type != CFW_EQ_OFF && gain_tenths_db != 0) {
        double s, c;
        sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
        double a = decibels(gain_tenths_db, 400);
        double alpha = s * 5.0 / q_tenths;

        /* RBJ Audio EQ Cookbook, using Q for both bells and shelves.
         * https://www.w3.org/TR/audio-eq-cookbook/ */
        if (type == CFW_EQ_PEAKING) {
            b[0] = 1.0 + alpha * a;
            b[1] = -2.0 * c;
            b[2] = 1.0 - alpha * a;
            d[0] = 1.0 + alpha / a;
            d[1] = -2.0 * c;
            d[2] = 1.0 - alpha / a;
        } else {
            /* The high shelf is the low shelf at z -> -z: cos(w) and the odd
             * terms change sign, which floating point represents exactly. */
            double sign = type == CFW_EQ_LOW_SHELF ? 1.0 : -1.0, m = sign * c;
            double beta = 2.0 * decibels(gain_tenths_db, 800) * alpha;
            b[0] = a * ((a + 1.0) - (a - 1.0) * m + beta);
            b[1] = sign * 2.0 * a * ((a - 1.0) - (a + 1.0) * m);
            b[2] = a * ((a + 1.0) - (a - 1.0) * m - beta);
            d[0] = (a + 1.0) + (a - 1.0) * m + beta;
            d[1] = sign * -2.0 * ((a - 1.0) + (a + 1.0) * m);
            d[2] = (a + 1.0) + (a - 1.0) * m - beta;
        }
    }
    for (unsigned int i = 0; i < 3; i++)
        b[i] *= attenuation;
    if (!quantize(out, b, d))
        return 0;
    /* Check the denominator after fixed-point quantization (Jury criterion). */
    return out[4] > -SCALE && out[4] < SCALE && (int64_t)SCALE + out[3] + out[4] > 0 &&
           (int64_t)SCALE - out[3] + out[4] > 0;
}

void cfw_eq_attenuate(int32_t coefficients[3], unsigned int precut_tenths_db)
{
    double gain = decibels(-(int)precut_tenths_db, 200);
    for (unsigned int i = 0; i < 3; i++) {
        double scaled = coefficients[i] * gain;
        coefficients[i] = (int32_t)(scaled + (scaled < 0.0 ? -0.5 : 0.5));
    }
}

/* Newton iteration from above; callers pass small positive values. */
static __attribute__((noinline)) double square_root(double value)
{
    if (!(value > 0.0))
        return 0.0;
    double root = value > 1.0 ? value : 1.0;
    for (unsigned int n = 0; n < 48; n++)
        root = 0.5 * (root + value / root);
    return root;
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

/* Power ratio as tenths of a decibel within +/-48 dB, rounded or rounded up. */
static __attribute__((noinline)) int power_tenths(double power, int round_up)
{
    if (!(power > 0.000015848931924611))
        return -480;
    if (!(power < 63095.7344480193))
        return 480;
    double tenths = logarithm(power) * 43.42944819032518;
    int result = (int)tenths;
    if (round_up)
        return result < tenths ? result + 1 : result;
    return (int)(tenths + (tenths < 0.0 ? -0.5 : 0.5));
}

/* |H|^2 of one biquad is a quadratic in cos(w) over another; keep both per stage. */
struct peak_stage {
    double numerator[3], denominator[3];
};

static void peak_stages(struct peak_stage *stages, const int32_t (*coefficients)[5],
                        unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        double b0 = coefficients[i][0] / (double)SCALE,
               b1 = coefficients[i][1] / (double)SCALE,
               b2 = coefficients[i][2] / (double)SCALE,
               a1 = coefficients[i][3] / (double)SCALE,
               a2 = coefficients[i][4] / (double)SCALE;
        stages[i] = (struct peak_stage){
            {b0 * b0 + b1 * b1 + b2 * b2, 2.0 * (b0 * b1 + b1 * b2), 2.0 * b0 * b2},
            {1.0 + a1 * a1 + a2 * a2, 2.0 * (a1 + a1 * a2), 2.0 * a2},
        };
    }
}

/* Cascade power at c = cos(w); singular responses report a huge peak. */
static double stage_power(const struct peak_stage *stages, unsigned int count, double c)
{
    double c2 = 2.0 * c * c - 1.0, numerator = 1.0, denominator = 1.0;
    for (unsigned int i = 0; i < count; i++) {
        const double *n = stages[i].numerator, *d = stages[i].denominator;
        numerator *= n[0] + n[1] * c + n[2] * c2;
        denominator *= d[0] + d[1] * c + d[2] * c2;
    }
    return denominator > 0.0 ? numerator / denominator : 1e30;
}

int cfw_eq_response(const int32_t coefficients[CFW_EQ_BANDS][5], double hz,
                    unsigned int rate)
{
    if (!(hz >= 20.0 && hz <= 20000.01) || (rate != 44100 && rate != 48000))
        return 0;
    struct peak_stage stages[CFW_EQ_BANDS];
    double s, c;
    peak_stages(stages, coefficients, CFW_EQ_BANDS);
    sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
    /* Bound the plot, including singular or underflowed responses. */
    return power_tenths(stage_power(stages, CFW_EQ_BANDS, c), 0);
}

/* Evaluate at u = tan(w / 2), where cos(w) = (1 - u^2) / (1 + u^2). */
static double peak_power(const struct peak_stage *stages, unsigned int count, double u)
{
    double u2 = u * u;
    return stage_power(stages, count, (1.0 - u2) / (1.0 + u2));
}

static double tan_half(double hz, unsigned int rate)
{
    double s, c;
    sine_cosine(6.283185307179586477 * hz / rate, &s, &c);
    return s / (1.0 + c);
}

/* Golden-section search for a local maximum of a unimodal interval. */
static double peak_refine(const struct peak_stage *stages, unsigned int count, double a,
                          double b)
{
    const double g = 0.6180339887498949;
    double x1 = b - g * (b - a), x2 = a + g * (b - a);
    double f1 = peak_power(stages, count, x1), f2 = peak_power(stages, count, x2);
    for (unsigned int n = 0; n < 10; n++) {
        if (f1 < f2) {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = a + g * (b - a);
            f2 = peak_power(stages, count, x2);
        } else {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = b - g * (b - a);
            f1 = peak_power(stages, count, x1);
        }
    }
    return f1 > f2 ? f1 : f2;
}

int cfw_eq_peak(const int32_t (*coefficients)[5], unsigned int count, unsigned int rate)
{
    enum { POINTS = 48 };
    struct peak_stage stages[CFW_EQ_BANDS];
    double grid[POINTS], starts[POINTS + CFW_EQ_BANDS];
    unsigned int start_count = 0;
    if (count > CFW_EQ_BANDS || (rate != 44100 && rate != 48000))
        return 0;
    peak_stages(stages, coefficients, count);

    /* RBJ filters keep their shape on a log tan(w / 2) axis, so a uniform grid
     * there resolves narrow bells near Nyquist as well as at low frequencies. */
    double low = tan_half(20.0, rate), high = tan_half(20000.0, rate);
    double ratio = exponential(logarithm(high / low) / (POINTS - 1)), u = low;
    double best = 0.0;
    for (unsigned int k = 0; k < POINTS; k++) {
        grid[k] = peak_power(stages, count, u);
        if (grid[k] > best)
            best = grid[k];
        u *= ratio;
    }
    u = low;
    for (unsigned int k = 0; k < POINTS; k++) {
        double left = k ? grid[k - 1] : -1.0,
               right = k + 1 < POINTS ? grid[k + 1] : -1.0;
        if (grid[k] > left && grid[k] >= right)
            starts[start_count++] = u;
        u *= ratio;
    }
    /* Resonant pole angles locate peaks that sit on another band's slope. */
    for (unsigned int i = 0; i < count; i++) {
        double a1 = coefficients[i][3] / (double)SCALE,
               a2 = coefficients[i][4] / (double)SCALE;
        if (!(a2 > 0.0) || a1 * a1 >= 4.0 * a2)
            continue;
        double c = -a1 / (2.0 * square_root(a2));
        if (!(c > -1.0 && c < 1.0))
            continue;
        u = square_root((1.0 - c) / (1.0 + c));
        if (u >= low && u <= high)
            starts[start_count++] = u;
    }
    for (unsigned int i = 0; i < start_count; i++) {
        double x = starts[i], a = x / ratio, b = x * ratio;
        double value = peak_power(stages, count, x);
        if (value > best)
            best = value;
        value = peak_refine(stages, count, a < low ? low : a, b > high ? high : b);
        if (value > best)
            best = value;
    }
    return power_tenths(best, 1);
}
