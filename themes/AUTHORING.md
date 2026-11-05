# Theme authoring

The compiler and firmware parsing primitives are implemented. The device theme
picker and safe switching coordinator are not connected yet; copying a compiled
folder does not activate it. The existing Dark Mode toggle still uses RTH3 packs.

Themes are written as a source folder and compiled on a desktop. The intended
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

## Current limitations

RTM4/RTH4 covers:

- **Colors**: the 16 color roles below, for a light and/or dark appearance.
  All colors are opaque; `#RRGGBB` only.
- **Bitmaps**: replacing existing firmware `BMap` assets at the same size,
  using the same rules as the RTH3 compiler (`themes/compiler.py`): exact
  native dimensions, native pixel format, and the existing palette size
  (indexed art is reduced to fit).

Not supported yet: fonts, layouts, metrics, animations, sounds, strings,
wallpapers, new assets, bitmaps of a different size, translucent colors, and
any appearance other than `light`/`dark`. Named unsupported capabilities are
rejected with an `unsupported capability` error; other unrecognized fields are
rejected as `unknown field`. Invalid dimensions, colors and appearances also
fail validation. Stored color roles and their current consumers are listed below.

The format is internal and may change before release. New resource types require
verified native hooks and a new binary format version; existing magic values and
role order must not be reinterpreted. Source manifests remain the authoring
interface and must be recompiled for the target firmware catalog. Custom fonts,
new-size icons, per-screen accents and layout changes are future work. The modern
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
- `id`: `[a-z0-9][a-z0-9_-]{0,30}`. `default` and `dark` are reserved
  for the existing RTH3 installation.
- `name`: at most 47 UTF-8 bytes, no control characters.
- `author` and `version`: optional, at most 64 bytes each. They are copied
  into `report.json` only and are never written to the runtime files.
- `appearances`: must declare at least one of `light` and `dark`. An empty
  entry (`{}`) uses the Classic colors for that appearance and replaces no
  bitmaps. Omitted bitmaps use original stock artwork, including in dark
  appearances; Classic dark artwork is not inherited.
- `colors`: any subset of the roles. Roles you leave out keep their Classic
  default.
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
| muted | `#555555` | `#a8a8a8` | EQ secondary text |
| accent_fill | `#236cc5` | `#00a1ff` | Native accent fills and matching text |
| accent_text | `#3185d0` | `#00a1ff` | Native accent text |
| on_accent | `#ffffff` | `#ffffff` | Reserved foreground; no native drawing consumer |
| selection | `#236cc5` | `#00a1ff` | Reserved selection fill; no native drawing consumer |
| track | `#eeeeee` | `#2c2c2c` | EQ control tracks |
| inactive | `#8f959a` | `#7d8c98` | EQ inactive controls |
| grid | `#e7e7e7` | `#303030` | EQ graph grid |
| zero | `#b5b5b5` | `#646464` | EQ zero-gain line |
| border | `#d6d6d6` | `#3c3c3c` | EQ control borders |
| cover_border | `#7e8183` | `#383838` | Cover Flow border |
| eq_accent | `#1479d5` | `#00a1ff` | EQ active controls and response curve |
| eq_selection | `#edf5fc` | `#1a2e3f` | EQ selection background |
| eq_on_accent | `#ffffff` | `#101010` | EQ text on active controls |

Native hooks map stock RGB values rather than individual views. These roles do
not yet control every label, icon or selection. Accent presets override accent
fills and their foregrounds; Theme Default preserves the package colors.

## Compiled folder

| File | Contents |
| --- | --- |
| `theme.bin` | RTM4 metadata, always 120 bytes |
| `light.bin` / `dark.bin` | RTH4 appearance pack, one per declared appearance |
| `report.json` | Host-side report; the device does not need it |

All integers are little-endian. `hash` is the SHA-256 `catalog_hash` of the
bitmap catalog, the same value RTH3 uses.

- `theme.bin` (`<4s32sI32s48s`): `RTM4`, hash, appearance mask (`1` light,
  `2` dark), ASCII id (NUL-terminated and zero-padded), UTF-8 name
  (NUL-terminated and zero-padded).
- Appearance pack header (`<4s32sIIII`, 52 bytes): `RTH4`, hash, appearance
  (`0` light, `1` dark), body size, record count, CRC32 of the body.
- Body: 16 RGBA colors in role order (64 bytes, alpha always 255). These are
  followed by `record count` RTH3-style records: `<III asset_index, offset,
  size>`, then `size` bytes of native bitmap data, zero-padded to a 4-byte
  boundary. Records are sorted by unique, ascending asset index. A pack may
  have no records. The body can be at most `64 + Σ(12 + align4(bytes −
  data_offset))` over every catalog bitmap.

`themes.package` provides `inspect_metadata`, `inspect_appearance`, and
`validate_package` to check compiled files against a catalog. The compiler
runs these checks on its own output before writing.

The RTH3 single-file dark theme (`themes/build.py`, `themes/compiler.py`) is
unchanged.

## Stock rollback data

Generate the complete original-artwork baseline from the pinned, user-provided
firmware during installation:

```sh
python3 -m themes.stock --input /path/to/original/osos.bin \
  --out build/theme-stock.bin
```

The extractor verifies the firmware SHA-256 and every bitmap CRC before writing.
The 599-record baseline is 3,218,168 bytes for the current catalog. The
transaction reader streams it with a 256-byte scratch buffer, retaining only
records required to restore artwork changed by the previous theme. Ordinary
navigation does not read this file. Keep the baseline outside `/themes/<id>/`
to avoid collisions with theme folders. Do not replace the legacy
`themes/default/theme.bin` with it: the existing dark-mode toggle expects its
original matching subset. Installer integration remains pending.
