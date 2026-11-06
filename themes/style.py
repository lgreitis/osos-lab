# SPDX-License-Identifier: GPL-3.0-only
"""RTH4 style prefix: base color roles, then options (render flags, icon tint,
one palette per named accent and one tint role byte per catalog asset)."""

import struct

from patching.theme_format import MAX_ASSETS, TINT_BYTES

# On-disk RTH4 base role order. Append only with a new package version.
COLOR_ROLES = (
    "background",
    "text",
    "accent_fill",
    "accent_text",
    "on_accent",
    "selection",
    "cover_border",
)
COLORS = len(COLOR_ROLES) * 4
# Classic light and dark, matching payload/theme_style.c.
COLOR_DEFAULTS = {
    key: dict(zip(COLOR_ROLES, (f"#{value}" for value in values.split())))
    for key, values in {
        "light": "ffffff 202020 236cc5 3185d0 ffffff 236cc5 7e8183",
        "dark": "181818 e8e8e8 00a1ff 00a1ff ffffff 00a1ff 383838",
    }.items()
}

# Stable numeric identifiers (enum cfw_theme_accent - 1): append; never reorder.
ACCENT_NAMES = (
    "blue",
    "purple",
    "pink",
    "red",
    "orange",
    "green",
    "teal",
    "yellow",
    "graphite",
)
# Palette slot order (enum cfw_theme_palette_role).
ACCENT_ROLES = (
    "accent_fill",
    "accent_text",
    "selection",
    "on_accent",
    "icon_tint",
)
FLAT_SELECTION = 1
# Colors a tinted bitmap may follow; the on-disk byte is 1 + index, 0 for untinted.
TINT_ROLES = ("icon_tint", *COLOR_ROLES)
PALETTE_BYTES = len(ACCENT_NAMES) * len(ACCENT_ROLES) * 4
OPTIONS_BYTES = 8 + PALETTE_BYTES + TINT_BYTES
STYLE_BYTES = COLORS + OPTIONS_BYTES
# Light fill/text and dark fill/text; Blue retains Classic's values.
ACCENT_PRESETS = (
    ("236cc5", "3185d0", "00a1ff", "00a1ff"),
    ("8944ab", "8944ab", "964ebc", "da8fff"),
    ("c41e56", "c41e56", "ce2660", "ff6491"),
    ("d02028", "d02028", "d42830", "ff6961"),
    ("b84800", "b84800", "be4c00", "ff9f0a"),
    ("1e803a", "1e803a", "20863e", "30d158"),
    ("007378", "007378", "00787d", "40d2d7"),
    ("826500", "826500", "846700", "ffd60a"),
    ("5c6066", "5c6066", "64686e", "bec2c8"),
)


def rgb(color):
    return bytes.fromhex(color.removeprefix("#"))


def accent_defaults(name, appearance):
    """Built-in palette for an accent."""
    preset = ACCENT_PRESETS[ACCENT_NAMES.index(name)]
    fill, text = (
        "#" + value for value in (preset[2:] if appearance == "dark" else preset[:2])
    )
    return dict(zip(ACCENT_ROLES, (fill, text, fill, "#ffffff", text)))


def contrast(first, second):
    """WCAG contrast ratio of two #RRGGBB colors."""

    def luminance(color):
        channels = [v / 255 for v in rgb(color)]
        linear = [
            v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
            for v in channels
        ]
        return sum(v * w for v, w in zip(linear, (0.2126, 0.7152, 0.0722)))

    low, high = sorted((luminance(first), luminance(second)))
    return (high + 0.05) / (low + 0.05)


CONTRAST_PAIRS = (
    ("text", "background"),
    ("accent_text", "background"),
    ("on_accent", "selection"),
    ("on_accent", "accent_fill"),
)


def contrast_report(colors, palettes):
    """Contrast of each text/background pair for Theme Default and every accent."""
    report = []
    for name, palette in [("default", {}), *palettes.items()]:
        resolved = colors | palette
        for foreground, background in CONTRAST_PAIRS:
            ratio = contrast(resolved[foreground], resolved[background])
            report.append(
                {
                    "accent": name,
                    "foreground": foreground,
                    "background": background,
                    "ratio": round(ratio, 3),
                    "passes": ratio >= 4.5,
                }
            )
    return report


def encode_colors(colors):
    return b"".join(rgb(colors[role]) + b"\xff" for role in COLOR_ROLES)


def encode_tinted(roles):
    """One role byte per catalog asset index from a mapping of index to tint role."""
    data = bytearray(TINT_BYTES)
    for index, role in roles.items():
        if not 0 <= index < MAX_ASSETS:
            raise ValueError(f"tinted asset index {index} exceeds {MAX_ASSETS}")
        data[index] = 1 + TINT_ROLES.index(role)
    return bytes(data)


def encode_options(flags, icon, palettes, tinted=None):
    palette = b"".join(
        rgb(palettes[name][role]) + b"\xff"
        for name in ACCENT_NAMES
        for role in ACCENT_ROLES
    )
    return (
        struct.pack("<I", flags)
        + rgb(icon)
        + b"\xff"
        + palette
        + encode_tinted(tinted or {})
    )


def decode_options(data):
    if len(data) != OPTIONS_BYTES:
        raise ValueError("invalid RTH4 options length")
    flags = struct.unpack_from("<I", data)[0]
    if flags & ~FLAT_SELECTION:
        raise ValueError("unknown RTH4 rendering flags")
    if any(data[i] != 255 for i in range(7, 8 + PALETTE_BYTES, 4)):
        raise ValueError("RTH4 colors must be opaque")
    palettes, at = {}, 8
    for name in ACCENT_NAMES:
        palettes[name] = {}
        for role in ACCENT_ROLES:
            palettes[name][role] = "#" + data[at : at + 3].hex()
            at += 4
    tinted = {}
    for index, value in enumerate(data[at : at + TINT_BYTES]):
        if value > len(TINT_ROLES):
            raise ValueError(f"unknown RTH4 tint role {value} for asset {index}")
        if value:
            tinted[index] = TINT_ROLES[value - 1]
    return {
        "flags": flags,
        "icon_tint": "#" + data[4:7].hex(),
        "accents": palettes,
        "tinted": tinted,
    }


def classic_style(appearance):
    """Style prefix for Classic packs; firmware uses its built-in Classic style."""
    colors = COLOR_DEFAULTS[appearance]
    palettes = {name: accent_defaults(name, appearance) for name in ACCENT_NAMES}
    return encode_colors(colors) + encode_options(0, colors["accent_text"], palettes)
