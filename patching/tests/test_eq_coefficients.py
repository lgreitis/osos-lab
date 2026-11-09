# SPDX-License-Identifier: GPL-3.0-only
"""Fixed-point EQ accuracy, including tenth-decibel precut."""

import cmath
import ctypes
import math
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
class EqCoefficientsTests(unittest.TestCase):
    def test_tenth_db_precut_and_filter_response(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "eq.so"
            subprocess.run(
                [
                    "cc",
                    "-shared",
                    "-fPIC",
                    str(root / "payload/eq_coefficients.c"),
                    "-o",
                    str(library),
                ],
                check=True,
            )
            dll = ctypes.CDLL(str(library))
            fn = dll.cfw_eq_coefficients
            fn.argtypes = [
                ctypes.POINTER(ctypes.c_int32),
                *([ctypes.c_uint] * 3),
                ctypes.c_int,
                ctypes.c_uint,
                ctypes.c_uint,
            ]
            fn.restype = ctypes.c_int
            scale = 1 << 24
            for rate in (44100, 48000):
                previous = scale + 1
                for precut in range(101):
                    out = (ctypes.c_int32 * 5)()
                    self.assertEqual(fn(out, 0, 60, 7, 0, precut, rate), 1)
                    self.assertLess(out[0], previous)
                    self.assertAlmostEqual(
                        out[0], scale * 10 ** (-precut / 200), delta=0.51
                    )
                    self.assertEqual(list(out)[1:], [0, 0, 0, 0])
                    previous = out[0]
                for kind in (1, 2, 3):
                    for hz in (20, 1000, 20000):
                        for gain in (-120, 0, 120):
                            for precut in (0, 1, 37, 100):
                                out = (ctypes.c_int32 * 5)()
                                self.assertEqual(
                                    fn(out, kind, hz, 7, gain, precut, rate), 1
                                )
                                actual = [n / scale for n in out]
                                expected = self.reference(kind, hz, gain, precut, rate)
                                for a, b in zip(actual, expected, strict=True):
                                    self.assertAlmostEqual(a, b, delta=1 / scale)
            out = (ctypes.c_int32 * 5)()
            self.assertEqual(fn(out, 0, 60, 7, 0, 101, 44100), 0)
            response = dll.cfw_eq_response
            response.argtypes = [
                ctypes.POINTER(ctypes.c_int32 * 5),
                ctypes.c_double,
                ctypes.c_uint,
            ]
            response.restype = ctypes.c_int
            for rate in (44100, 48000):
                for q in (3, 7, 40):
                    for gain in (-120, 0, 120):
                        bank = ((ctypes.c_int32 * 5) * 5)()
                        for band, (kind, hz) in enumerate(
                            ((2, 80), (1, 1000), (3, 16000), (1, 250), (1, 4000))
                        ):
                            self.assertEqual(
                                fn(
                                    bank[band],
                                    kind,
                                    hz,
                                    q,
                                    gain,
                                    37 if band == 0 else 0,
                                    rate,
                                ),
                                1,
                            )
                        for i in range(81):
                            hz = 20 * 1000 ** (i / 80)
                            z = cmath.exp(-2j * math.pi * hz / rate)
                            power = 1.0
                            for coefficients in bank:
                                b0, b1, b2, a1, a2 = coefficients
                                power *= (
                                    abs(
                                        (b0 + b1 * z + b2 * z * z)
                                        / (scale + a1 * z + a2 * z * z)
                                    )
                                    ** 2
                                )
                            expected = max(-480, min(480, 100 * math.log10(power)))
                            self.assertAlmostEqual(
                                response(bank, hz, rate), expected, delta=0.51
                            )

            self.check_peak(dll, fn, scale)

    def check_peak(self, dll, fn, scale):
        peak = dll.cfw_eq_peak
        peak.argtypes = [
            ctypes.POINTER(ctypes.c_int32 * 5),
            ctypes.c_uint,
            ctypes.c_uint,
        ]
        peak.restype = ctypes.c_int
        attenuate = dll.cfw_eq_attenuate
        attenuate.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint]
        attenuate.restype = None

        def dense(bank, rate):
            best = 0.0
            for i in range(6001):
                z = cmath.exp(-2j * math.pi * 20 * 1000 ** (i / 6000) / rate)
                power = 1.0
                for b0, b1, b2, a1, a2 in bank:
                    power *= (
                        abs((b0 + b1 * z + b2 * z * z) / (scale + a1 * z + a2 * z * z))
                        ** 2
                    )
                best = max(best, power)
            return 100 * math.log10(best)

        # Narrow bells near Nyquist, overlapping peaks, resonant shelves and cuts.
        cases = [
            [(1, 19000, 40, 120)],
            [(1, 16000, 40, 120), (1, 12500, 30, 60)],
            [(1, 1000, 7, 60), (1, 1250, 40, 40), (2, 80, 7, 30)],
            [(2, 1000, 40, 90), (3, 1000, 26, -60)],
            [(3, 8000, 7, 75), (1, 4000, 12, 50), (2, 60, 7, 75)],
            [(1, 250, 10, -120), (1, 4000, 10, -60)],
        ]
        for rate in (44100, 48000):
            for case in cases:
                bank = ((ctypes.c_int32 * 5) * 5)()
                for i, (kind, hz, q, gain) in enumerate(case):
                    self.assertEqual(fn(bank[i], kind, hz, q, gain, 0, rate), 1)
                expected = dense([list(bank[i]) for i in range(len(case))], rate)
                actual = peak(bank, len(case), rate)
                with self.subTest(rate=rate, case=case):
                    self.assertGreaterEqual(actual, expected - 0.01)
                    self.assertLessEqual(actual, math.ceil(expected) + 1)
                    # Precut rounds up, leaving the peak within 0.1 dB below 0 dB.
                    if actual > 0:
                        attenuate(bank[0], actual)
                        self.assertLessEqual(peak(bank, len(case), rate), 0)
                        self.assertGreater(
                            dense([list(bank[i]) for i in range(len(case))], rate),
                            -1.01,
                        )
        coefficients = (ctypes.c_int32 * 5)(scale, -scale // 2, 1000, 7, 9)
        attenuate(coefficients, 60)
        gain = 10 ** (-60 / 200)
        self.assertEqual(
            list(coefficients),
            [round(scale * gain), round(-scale // 2 * gain), round(1000 * gain), 7, 9],
        )

    @staticmethod
    def reference(kind, hz, gain, precut, rate):
        w = 2 * math.pi * hz / rate
        c, alpha = math.cos(w), math.sin(w) / 1.4
        a = 10 ** (gain / 400)
        beta = 2 * math.sqrt(a) * alpha
        if kind == 1:
            b = [1 + alpha * a, -2 * c, 1 - alpha * a]
            d = [1 + alpha / a, -2 * c, 1 - alpha / a]
        elif kind == 2:
            b = [
                a * (a + 1 - (a - 1) * c + beta),
                2 * a * (a - 1 - (a + 1) * c),
                a * (a + 1 - (a - 1) * c - beta),
            ]
            d = [
                a + 1 + (a - 1) * c + beta,
                -2 * (a - 1 + (a + 1) * c),
                a + 1 + (a - 1) * c - beta,
            ]
        else:
            b = [
                a * (a + 1 + (a - 1) * c + beta),
                -2 * a * (a - 1 + (a + 1) * c),
                a * (a + 1 + (a - 1) * c - beta),
            ]
            d = [
                a + 1 - (a - 1) * c + beta,
                2 * (a - 1 - (a + 1) * c),
                a + 1 - (a - 1) * c - beta,
            ]
        if gain == 0:
            return [10 ** (-precut / 200), 0, 0, 0, 0]
        return [n * 10 ** (-precut / 200) / d[0] for n in b] + [n / d[0] for n in d[1:]]
