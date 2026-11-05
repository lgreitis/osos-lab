# Theme authoring

Themes are written as a source folder and compiled on a desktop. The
installation location for each compiled folder is `/themes/<id>/`. The compiler
is host-only; it needs the firmware bitmap catalog (`firmware/bitmaps.json`),
not the firmware image.

```sh
python3 -m themes.package themes/examples/modern/theme.json \
  --out build/themes/modern --catalog firmware/bitmaps.json
```

The compiler checks the whole theme and builds every output in memory before
it writes anything. Validation failures leave the output folder untouched. Files
are then replaced individually; a filesystem error can leave a partially updated
folder. Rerun the compiler after resolving the error. Successful runs remove
appearance packs the manifest no longer declares.

## Supported capabilities

RTM4/RTH4 covers:

- **Colors**: the 7 color roles below, for a light and/or dark appearance.
  All colors are opaque; `#RRGGBB` only.
- **Accents**: per-appearance values for the named accent colors chosen on the
  device, and the selection bar style.
- **Bitmaps**: replacing existing firmware `BMap` assets at the same size,
  using the same rules as the Classic dark compiler (`themes/compiler.py`): exact
  native dimensions, native pixel format, and the existing palette size
  (indexed art is reduced to fit).

Unsupported: fonts, layouts, metrics, animations, sounds, strings,
wallpapers, new assets, bitmaps of a different size, translucent colors, and
any appearance other than `light`/`dark`. Named unsupported capabilities are
rejected with an `unsupported capability` error; other unrecognized fields are
rejected as `unknown field`. Invalid dimensions, colors and appearances also
fail validation. Stored color roles and their current consumers are listed below.

The format is internal. New resource types require
verified native hooks and a new binary format version; existing magic values and
role order must not be reinterpreted. Source manifests remain the authoring
interface and must be recompiled for the target firmware catalog. The modern
example demonstrates color data only.

`report.json` lists the capabilities this format supports and gives memory
*estimates* derived from file sizes. They are not measurements of RAM use on
the device.

## Source manifest (schema 1)

```json
{
  "schema": 1,
  "id": "modern",
  "name": "Modern",
  "author": "optional, source only",
  "version": "optional, source only",
  "appearances": {
    "light": {
      "colors": { "accent_fill": "#0071e3" },
      "bitmaps": [
        { "id": "0x0DAD012A", "file": "art/status-bar.png" },
        { "name": "Background_LightBlue_Image", "file": "background.png", "type": "BMap" }
      ]
    },
    "dark": {}
  }
}
```

- `schema`: integer `1`.
- `id`: `[a-z0-9][a-z0-9_-]{0,30}`. `classic`, `default`, and `dark` are reserved.
  `classic` contains the built-in assets; the other names remain reserved for compatibility.
- `name`: at most 47 UTF-8 bytes, no control characters.
- `author` and `version`: optional, at most 64 bytes each. They are copied
  into `report.json` only and are never written to the runtime files.
- `appearances`: must declare at least one of `light` and `dark`. An empty
  entry (`{}`) uses the Classic colors for that appearance and replaces no
  bitmaps. Omitted bitmaps use original stock artwork, including in dark
  appearances; Classic dark artwork is not inherited.
- `colors`: any subset of the roles. Roles you leave out keep their Classic
  default. `icon_tint` is also accepted here; it defaults to `accent_text`.
- `accents`: optional object keyed by accent name (`blue`, `purple`, `pink`,
  `red`, `orange`, `green`, `teal`, `yellow`, `graphite`). Each value is any
  subset of `accent_fill`, `accent_text`, `selection`, `on_accent` and
  `icon_tint`. When the user
  picks that accent, these replace the matching roles. Omitted names and roles
  use built-in values for the appearance; `report.json` lists the names that
  fell back and the contrast of each resolved text/background pair.
- `rendering`: optional. `selection` is `native` (default; the stock gradient
  bar recolored from `selection`) or `flat` (a solid `selection` bar).
  `preview_icons` is `native` (default) or `accent`; it is stored with
  `icon_tint` but has no native drawing consumer.
- `bitmaps`: each entry gives a catalog `id` (hex string), a catalog `name`,
  or both. If both are given they must refer to the same asset. A name that
  appears more than once in the catalog must be referenced by `id`. Each asset
  may be replaced at most once per appearance. `type` is optional, and if
  given must be `BMap`.
- `file`: a relative path inside the manifest folder. Each path component must
  start with `A-Z a-z 0-9 _` and contain only those characters, `.`, `-`
  and spaces. Components are limited to 128 characters, paths to 255.
  Absolute paths, `..`, backslashes, and symlinks that point outside the
  folder are rejected. PNG files are limited to 16 MiB; the manifest itself is
  limited to 256 KiB. Duplicate JSON keys are rejected.

### Color roles and Classic defaults

| Role | Light | Dark | Current use |
| --- | --- | --- | --- |
| background | `#ffffff` | `#181818` | Native surfaces and EQ background |
| text | `#202020` | `#e8e8e8` | Native text shades and EQ text |
| accent_fill | `#236cc5` | `#00a1ff` | Native accent fills and matching text; EQ field being edited |
| accent_text | `#3185d0` | `#00a1ff` | Native accent text; EQ response curve and selected band |
| on_accent | `#ffffff` | `#ffffff` | Selected-row text; EQ text on the field being edited |
| selection | `#236cc5` | `#00a1ff` | List selection bar |
| cover_border | `#7e8183` | `#383838` | Cover Flow border |

The Graphic EQ has no roles of its own. Its grays are mixed from `background`
toward `text` when the screen is drawn, so they follow whatever colors you pick:

| EQ element | Mix toward `text` |
| --- | --- |
| Control tracks | 22/255 |
| Graph grid | 28/255 |
| Control borders | 46/255 |
| Zero-gain line | 89/255 |
| Inactive controls | 130/255 |
| Secondary text | 185/255 |

The selected band gets a light tint: `background` mixed toward `accent_text`
by 20/255, or 43/255 when the background is dark. Labels drawn on that tint use
`accent_fill` or `accent_text`, whichever stands out more against it. The field
being edited is filled with `accent_fill` and its text uses `on_accent`, so keep
`on_accent` readable on both `accent_fill` and `selection`. `report.json`
reports the contrast of both pairs.

Native hooks map stock RGB values rather than individual views. These roles do
not control every label or icon. Accent presets override accent
fills and their foregrounds; Theme Default preserves the package colors.

The list selection bar is drawn once into a shared cached bitmap and redrawn
when its style changes. Classic with Theme Default or Blue keeps the stock bar
unchanged. Otherwise the gradient ends at `selection`, starts at `selection`
lightened 25% toward white, and has a top line lightened 55%; `flat` uses
`selection` for all three.

## Compiled folder

| File | Contents |
| --- | --- |
| `theme.bin` | RTM4 metadata, always 120 bytes |
| `light.bin` / `dark.bin` | RTH4 appearance pack, one per declared appearance |
| `report.json` | Host-side report; the device does not need it |

All integers are little-endian. `hash` is the SHA-256 `catalog_hash` of the
bitmap catalog.

- `theme.bin` (`<4s32sI32s48s`): `RTM4`, hash, appearance mask (`1` light,
  `2` dark), ASCII id (NUL-terminated and zero-padded), UTF-8 name
  (NUL-terminated and zero-padded).
- Appearance pack header (`<4s32sIIII`, 52 bytes): `RTH4`, hash, appearance
  (`0` light, `1` dark), body size, record count, CRC32 of the body.
- Body: a 216-byte style prefix followed by bitmap records. The prefix holds
  7 RGBA colors in role order (28 bytes), then a little-endian flags word
  (`1` flat selection, `2` accent preview icons), the RGBA icon tint, and
  9 accents × 5 roles of RGBA in the name and role order listed above. Every
  alpha byte is 255.
- Records: `record count` entries of `<III asset_index, offset, size>`, then
  `size` bytes of native bitmap data, zero-padded to a 4-byte boundary.
  Records are sorted by unique, ascending asset index. A pack may have no
  records. The body can be at most `216 + Σ(12 + align4(bytes − data_offset))`
  over every catalog bitmap.

`themes.package` provides `inspect_metadata`, `inspect_appearance`, and
`validate_package` to check compiled files against a catalog. The compiler
runs these checks on its own output before writing.

Classic packs use the same RTH4 layout with the Classic colors in the prefix:
`themes/classic/dark.bin` (`themes/build.py`, `themes/compiler.py`) and the
stock baseline below. The firmware validates their prefix but applies its
built-in Classic style.

## Stock restoration data

Generate the complete original-artwork baseline from the pinned, user-provided
firmware during installation:

```sh
python3 -m themes.stock --input /path/to/original/osos.bin \
  --out build/themes/classic/default.bin
```

The extractor verifies the firmware SHA-256 and every bitmap CRC before writing.
The 599-record baseline is 3,218,412 bytes for the current catalog. The
transaction reader streams it with a 256-byte scratch buffer, retaining only
records required to restore artwork changed by the previous theme. Boot validates this file before enabling theming.
Install it at `themes/classic/default.bin`.
The `classic` folder is reserved and does not appear in directory discovery. Its
complete baseline can restore artwork changed by any custom theme. Install the
Classic dark pack at `themes/classic/dark.bin`. Both files are required to enable
theming at boot. Installer 0.2.9 or newer generates and installs these files from verified Apple
firmware; the extracted stock artwork is not distributed with releases.

See [Theme switching](../docs/theme-switch.md) for runtime
ownership, synchronous application, and preference handling.
