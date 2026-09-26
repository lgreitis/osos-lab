# SPDX-License-Identifier: GPL-3.0-only
"""Linked ELF symbols, preserving the ARM/Thumb instruction-set bit."""

import subprocess


def read(elf, prefix, thumb_bits=True):
    listing = subprocess.check_output([prefix + "readelf", "-sW", str(elf)], text=True)
    symbols = {}
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].rstrip(":").isdigit() and parts[6] != "UND":
            value = int(parts[1], 16)
            if parts[3] == "FUNC" and not thumb_bits:
                value &= ~1
            symbols[parts[7]] = value
    return symbols


def require(symbols, name):
    try:
        return symbols[name]
    except KeyError:
        raise ValueError(f"Missing linked patch symbol: {name}") from None
