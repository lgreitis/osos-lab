# Assets

`[[asset]]` tables in `manifest.toml` put files into the `.rvx`. A `.png` becomes a bitmap, anything else data.

| Key | Meaning |
|---|---|
| `name` | how the extension refers to it, `[a-z0-9_-]{1,31}` |
| `file` | the source file |
| `preload` | when the asset is read into RAM with the main module: `always`, for what a hook needs without disk access; `optional` (the default), when Settings > Extensions > Optimize is Performance, or Auto on a hard disk; `never` (the default with `compress = false`), read on request, for large or rarely used assets |
| `pane` | the bitmap's role in a menu pane, a pane's `.bitmap` or `.badge`: `"icon"`, a 78 x 121 Settings icon mask the system draws in its own colour, from the PNG's alpha or, if opaque grayscale, its gray level; `"picture"`, a colour picture of 119 x 119, 142 x 87, 130 x 128, 146 x 110 or 132 x 145, which picks the pane's shape (see [Menu placement](menus.md)); `"badge"`, the 20 x 16 badge before the 146 x 110 pane's count line. The packer rejects other sizes |
| `compress = false` | data only: stored raw so `asset_read` can seek in it; `preload` must be `never`. Bitmaps are always compressed |

| Call | |
|---|---|
| `api->asset_load(self, name, &bytes)` | a decoded copy, freed with `api->free` |
| `api->asset_read(self, name, offset, out, bytes, &got)` | part of an asset stored with `compress = false`; `got` is the count copied |
| `api->asset_open(self, name, &file)` | such an asset, with `preload = "never"`, as a handle for `file_read`, `file_seek` and `file_close`, offsets from its start |
| `RV_BITMAP("name")` | a bitmap for `gfx->bitmap` and pane `.bitmap` fields |

The core decodes only the pane picture on screen. A canvas's bitmaps stay decoded while it is open, so `gfx->bitmap` decodes on the first draw only. Decoded bitmaps count against the extension.

## Asset provider

`api->asset_set_provider(self, &provider)` lets a theme replace any extension's bitmap assets, Settings icons and canvas bitmaps alike. The `struct rv_asset_provider` of `rv_theme.h` has three optional functions. The core calls them with the owner extension's id hash and the asset's name hash, both 32-bit FNV-1a; the core's own bitmaps are owned by the hash of `"revola"`.

Before decoding a bitmap, the core asks `replacement` whether the theme has one and how it is stored (`struct rv_asset_stream`: raw or one lz stream in the package format, with stored and decoded sizes). It then reads the stored bytes with `replacement_read` and decodes them into a BMap with its 28-byte header.

Every decoded bitmap, replacement or not, then goes to `adjust`, which may alter it in place. A theme tints a picture there, or strips an icon's reflection for a flat look.

A replacement counts against the holder's memory, not the owner's. One extension holds the provider at a time. Passing NULL releases it, as does unloading the module or freeing the block that holds the table. Setting or releasing it decodes the bitmaps on screen again.

The Appearance extension is the only holder. A theme ships extension bitmaps as `extensions/<id>/<asset>.png`, and Appearance trims the reflection of Settings icons it does not replace when the theme asks for flat icons. Extensions need no changes for this.
