# Custom Settings icons

Settings previews should match Apple's shaded grayscale symbols. Use
`payload/rvx/assets/settings-info.png` as a finished reference; an
extension's icon is built the same way with the shared `settings_icon.py` in
the extensions repository.

- Use a **78 × 121 px** canvas. Center the symbol horizontally, with its main
  silhouette approximately between y=12 and y=64. Leave room below for reflection.
- Keep the shape simple and readable at actual size. Use a dark beveled rim,
  a brighter upper face and a darker lower face; avoid flat white line art.
- Add a vertically mirrored, fading reflection beginning around y=85. The
  reflection should be noticeably dimmer than the symbol.
- Export opaque grayscale on black, without a baked-in page background or
  transparent alpha. The UI uses it as a native grayscale mask.
- Inspect at 1× beside the existing icons; an enlarged preview is useful for
  checking the rim and antialiasing, but does not replace the actual-size check.

## Render an icon

Keep an editable SVG beside the PNG. Draw only the white silhouette on black
in a `viewBox="0 0 78 121"`; omit shading and reflection from the SVG.
With ImageMagick installed, run from the repository root:

```sh
python3 tools/settings_icon.py icon.svg icon.png
```

The renderer applies the native bevel, gradient and reflection at 8×
resolution, then downsamples to the native size. Commit both the SVG and PNG.
The info icon was originally rendered from a font and has no SVG source.
A PNG in `payload/rvx/assets/` goes into the asset trailer after the runtime
in `osos-cfw.bin`; the runtime's own row names it by its file stem in a
`struct bitmap`, as an extension names an asset of its package. An extension
ships its icons in its own package.
