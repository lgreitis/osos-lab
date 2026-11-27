#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write the example's beep: an 880 Hz sine of 150 ms with a 10 ms fade at
both ends, 16-bit mono 22050 Hz PCM WAV. python3 tone.py beep.wav"""

import math
import struct
import sys
import wave

RATE, HERTZ, MILLISECONDS, FADE_MS = 22050, 880, 150, 10

frames = RATE * MILLISECONDS // 1000
fade = RATE * FADE_MS // 1000
samples = bytearray()
for i in range(frames):
    gain = min(1.0, i / fade, (frames - 1 - i) / fade)
    samples += struct.pack(
        "<h", int(20000 * gain * math.sin(2 * math.pi * HERTZ * i / RATE))
    )
with wave.open(sys.argv[1], "wb") as out:
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(RATE)
    out.writeframes(bytes(samples))
