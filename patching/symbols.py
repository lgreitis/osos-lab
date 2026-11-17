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
            name = parts[7]
            symbols[name] = (
                value if name not in symbols or symbols[name] == value else None
            )
    return symbols


def require(symbols, name):
    try:
        value = symbols[name]
        if value is None:
            raise ValueError(f"Ambiguous linked patch symbol: {name}")
        return value
    except KeyError:
        raise ValueError(f"Missing linked patch symbol: {name}") from None
