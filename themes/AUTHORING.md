# Theme authoring

Themes are written as a source folder and compiled on a desktop. The
installation location for each compiled folder is `/themes/<id>/`. The compiler
is host-only; it needs the firmware bitmap catalog (`firmware/bitmaps.json`),
not the firmware image.

```sh
python3 -m themes.package themes/examples/flat/theme.json \
  --out build/themes/flat --catalog firmware/bitmaps.json
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
  device.
- **Rendering**: the selection bar style, the Menu Covers default, and an
  optional flat Now Playing cover.
- **Bitmaps**: replacing existing `BMap` assets at the same size, using the
  same rules as the Classic dark compiler (`themes/compiler.py`): exact native
  dimensions, native pixel format, and the existing palette size (indexed art
  is reduced to fit). The catalog lists the firmware's bitmaps followed by the
  RepriseOS Settings preview glyphs (`CFW_Info_Icon`, `CFW_EQ_Icon`,
  `CFW_Theme_Icon`, `CFW_Accent_Icon`, `CFW_DarkMode_Moon`,
  `CFW_MenuCovers_Icon`; 78×121 8-bit masks marked `"source": "payload"`),
  which are replaced and tinted exactly like the stock `Settings_*_Image`
  previews. The payload keeps them run-packed, so mostly black art costs
  little memory: each replacement that differs from the built-in glyph is
  packed into its own allocation while the theme is active.
- **Tint**: bitmaps marked `tint` follow the icon tint, or a named color role,
  of the theme and of the accent the user picks. Masks (formats 4 and 8) are
  colored while drawing; paletted bitmaps (formats 0x64 and 0x65) have their
  palette colors rewritten in memory and ARGB bitmaps (format 0x1888) their
  pixel colors, keeping each entry's alpha.

Unsupported: fonts, layouts, metrics, animations, sounds, strings,
wallpapers, new assets, bitmaps of a different size, translucent colors, and
any appearance other than `light`/`dark`. Named unsupported capabilities are
rejected with an `unsupported capability` error; other unrecognized fields are
rejected as `unknown field`. Invalid dimensions, colors and appearances also
fail validation. Stored color roles and their current consumers are listed below.

The format is internal. New resource types require
verified native hooks and a new binary format version; existing magic values and
role order must not be reinterpreted. Source manifests remain the authoring
interface and must be recompiled for the target firmware catalog. The flat
example replaces most of the stock artwork; its `render.py` draws the art with
ImageMagick from Cupertino Icons and the Inter Medium font, which are not part
of the repository: pass them with `--font` (or `CUPERTINO_ICONS`) and
`--text-font` (or `INTER_FONT`).

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
        { "name": "Background_LightBlue_Image", "file": "background.png", "type": "BMap" },
        { "name": "Settings_Brightness_Image", "file": "art/sun.png", "tint": "icon_tint" },
        { "name": "Settings_EQ_Flat_Image", "tint": "icon_tint" }
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
  `icon_tint`. When the user picks that accent, these replace the matching
  roles. Omitted names and roles use built-in values for the appearance;
  `report.json` lists the names that fell back and the contrast of each
  resolved text/background pair.
- `rendering`: optional. `selection` is `native` (default; the stock gradient
  bar recolored from `selection`) or `flat` (a solid `selection` bar).
  `menu_covers` is `on` (default) or `off`: the Menu Covers setting the theme
  applies until the user chooses a value in Settings. `now_playing_art` is
  `native` (default; the stock tilted cover with its reflection) or `flat` (the
  cover drawn face-on with no reflection).
- `bitmaps`: each entry gives a catalog `id` (hex string), a catalog `name`,
  or both. If both are given they must refer to the same asset. A name that
  appears more than once in the catalog must be referenced by `id`. Each asset
  may be replaced at most once per appearance. `type` is optional, and if
  given must be `BMap`.
- `tint`: optional color the device draws the bitmap in: `icon_tint`,
  `background`, `text`, `accent_fill`, `accent_text`, `on_accent`, `selection`
  or `cover_border`, following the selected accent's palette. The whole
  bitmap takes that one color. Its `file` supplies coverage only: the alpha
  channel if the PNG has one, otherwise the gray level; colored opaque art is
  rejected. Mask assets (formats 4 and 8) may omit `file` to tint the stock
  shape. Paletted assets (formats 0x64 and 0x65) and ARGB assets (format 0x1888)
  require `file`; their palette entries or pixels carry the coverage as alpha
  and are recolored on the device. RGB565 assets cannot be tinted.
- `file`: a relative path inside the manifest folder. Each path component must
  start with `A-Z a-z 0-9 _` and contain only those characters, `.`, `-`
  and spaces. Components are limited to 128 characters, paths to 255.
  Absolute paths, `..`, backslashes, and symlinks that point outside the
  folder are rejected. PNG files are limited to 16 MiB; the manifest itself is
  limited to 256 KiB. Duplicate JSON keys are rejected.

### Color roles and Classic defaults

| Role | Light | Dark | Current use |
| --- | --- | --- | --- |
| background | `#ffffff` | `#181818` | Native surfaces and EQ background; Search strip letters, input text and status bar title on dark well, field or bar art |
| text | `#202020` | `#e8e8e8` | Native text shades and EQ text; Search strip letters, input text and status bar title on light well, field or bar art |
| accent_fill | `#236cc5` | `#00a1ff` | Native accent fills and matching text; EQ field being edited; stock progress, dot, star and playback art the theme leaves stock |
| accent_text | `#3185d0` | `#00a1ff` | Native accent text and the Search input caret; EQ response curve and selected band |
| on_accent | `#ffffff` | `#ffffff` | Selected-row text and tinted icons; EQ text on the field being edited; the highlighted Search key letter |
| selection | `#236cc5` | `#00a1ff` | List selection bar; bitmaps tinted with `selection` (for example a Search key cursor); stock selection and focus art the theme leaves stock |
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

Stock blue artwork that marks selection, focus or progress follows the theme
when your pack does not replace it. This covers the OptionBar thumbs (the Search
keyboard cursor and the Clock's Add/Edit bar), the active button, picker and
calendar highlights, the Settings checkmark, the progress and Genius sliders,
new-item dots, rating stars and playback glyphs. The device recolors them from
the stock pixels with `selection` or `accent_fill`, keeping their shading; the
full list is `ACCENT_ART` in `patching/assets.py`. To give one of them other art,
replace or tint it as usual; replaced assets are never recolored.

The Search letter strip draws its unhighlighted letters in `text` or
`background`, whichever differs more in brightness from the center pixel of the
well art (`OptionBar_*_Well_Center_Image`), so the well may be light or dark.
The highlighted key letter uses `on_accent`; keep it readable on whatever tints
the thumb slices.

The text typed into the Search box follows the same rule against the center
pixel of the field art (`System_Input_Field_Middle_Image`), so the field may be
light or dark; its caret uses `accent_text`. Classic keeps the stock light field
with native black text.

The title of the Stopwatch and Charging header follows the same rule against
the center pixel of the bar art (`NowPlaying_StatusBar_Image`), so the bar may
be light or dark. Classic keeps the stock dark bar with its native light title.

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
- Body: an 856-byte style prefix followed by bitmap records. The prefix holds
  7 RGBA colors in role order (28 bytes), then a little-endian flags word
  (`1` flat selection, `2` Menu Covers off, `4` flat Now Playing artwork), the
  RGBA icon tint,
  9 accents × 5 roles of RGBA in the name and role order listed above, and
  640 tint role bytes, one per catalog asset index: `0` untinted, `1`
  `icon_tint`, then `2` to `8` for the 7 base roles in role order. Every
  alpha byte is 255. A tint role on a paletted or ARGB asset requires a record
  for that asset in the same pack; roles on RGB565 assets, beyond the catalog,
  or above `8` are invalid.
- Records: `record count` entries of `<III asset_index, offset, size>`, then
  `size` bytes of native bitmap data, zero-padded to a 4-byte boundary.
  Records are sorted by unique, ascending asset index. A pack may have no
  records. The body can be at most `856 + Σ(12 + align4(bytes − data_offset))`
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
The 605-record baseline is 3,277,180 bytes for the current catalog. Switching reads
it straight into resident artwork, only up to the last bitmap that needs restoring,
and checks each restored bitmap against its original checksum. Boot checks its
header before enabling theming.
Install it at `themes/classic/default.bin`.
The `classic` folder is reserved and does not appear in directory discovery. Its
complete baseline can restore artwork changed by any custom theme. Install the
Classic dark pack at `themes/classic/dark.bin`. Both files are required to enable
theming at boot. Installer 0.2.9 or newer generates and installs these files from verified Apple
firmware; the extracted stock artwork is not distributed with releases.

See [Theme switching](../docs/theme-switch.md) for runtime
ownership, synchronous application, and preference handling.
