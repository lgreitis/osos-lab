# SPDX-License-Identifier: GPL-3.0-only
"""Fixed-point EQ accuracy, including tenth-decibel precut."""

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
            fn = ctypes.CDLL(str(library)).cfw_eq_coefficients
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
