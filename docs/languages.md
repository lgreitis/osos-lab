# Languages and language IDs

## External language packs: implementation status

The host compiler and catalog export are implemented. Device-side loading,
English-only payload generation, persistence and additional entries in Apple's
Language menu are **not enabled yet**. The current firmware still embeds its
existing translations and follows the original 21-language selector. Copying the
new packs to an iPod does not activate them. Release bundles do not include them
yet.

The native selection routine indexes Apple's fixed table without a bounds check.
The lifetime of text pointers retained by native views has not been established.
The runtime integration must resolve both issues before it can safely switch
external languages and release the old pack. Package limits below are host-format
limits, not a verified device heap budget.

### Build Outputs

A normal `tools/build.py osos` build also produces:

```text
build/languages/catalog.json
build/languages/index.bin
build/languages/<tag>/strings.bin
```

The catalog is derived from the exact generated UI resource IDs and build
identity. It includes semantic CFW keys, English defaults, composition recipes,
CFW format signatures and the 1,143 `Str ` entries in the pinned Apple OSOS bank.
This does not claim coverage of strings from other providers, dynamically
composed Apple text, music metadata or text inside bitmaps.

Existing paired catalogs remain authoritative until the runtime migration.
They are exported to 20 non-English packs automatically. English requires no
pack. Recipe-only/release builds emit an intermediate CFW catalog at
`.build/payload/language-catalog.json` but do not require or parse an original
image to produce device packs.

To obtain editable tag-based copies of the existing translations:

```bash
.venv/bin/python tools/languages.py export \
  --input .build/test-inputs/osos.bin \
  --out build/language-preview \
  --source-out build/language-sources
```

The standalone export's catalog uses its `--revision` and `--version` arguments;
its defaults are a development preview, not necessarily the installed firmware's
identity. Use the normal firmware build's catalog when compiling packs for that
build. Exporting sources overwrites matching files in `--source-out`; use a
separate directory for edited translations.

### Tag-Based Sources

Each source is `<tag>/strings.lo`, encoded as UTF-8. For example, a new ASCII
test language can use `build/language-sources/x-test/strings.lo`:

```xml
<language tag="x-test" name="Test Language" english-name="Test Language">
  <string key="cfw:cfw_info/CFW_Info_Title" text="Test Info"/>
</language>
```

Use the keys in the exported catalog. CFW keys are scoped to their UI source;
equal English text does not merge different declared string identities.
Composition parts with an empty key are literals; named parts can be translated
independently. Native keys use `apple:` followed by the eight-digit hexadecimal
resource ID. Native strings containing `%` cannot currently be changed, because
their formatting dialect has not been verified. CFW formats must retain their
argument types; numbered `%s`/`%u` arguments may be reordered.

Compile the complete source tree against an existing catalog, without invoking
the firmware compiler or reading the original image:

```bash
.venv/bin/python tools/languages.py build \
  --catalog build/languages/catalog.json \
  --sources build/language-sources \
  --out build/languages
```

The resulting index describes only this source tree; unlisted old pack files are
not indexed. Tags must be ASCII alphanumeric subtags separated by hyphens, with
an alphabetic first character and at most 31 bytes. Canonical native tags are
`en`, `ja`, `cs`, `da`, `de`, `es`, `fr`, `el`, `it`, `hu`, `nl`, `nb`, `pt`,
`ru`, `pl`, `fi`, `sv`, `tr`, `ko`, `zh-Hans` and `zh-Hant`. Other tags have no
native index. An external `en` source is rejected. UTF-8 acceptance does not
establish font, right-to-left or shaping support on the device.

### Binary Contract

All integer fields are little-endian. `RLP1` version 1 packs contain native
firmware and catalog SHA-256 fingerprints, a 32-byte zero-padded tag, entry count,
body size and body CRC32. The body contains sorted `{id, offset, length}` u32
entries followed by contiguous NUL-terminated UTF-8 strings. Offsets are relative
to the text area; lengths include the NUL. Duplicate IDs, gaps, overlaps, trailing
data, embedded NULs and invalid UTF-8 are rejected.

`RLI1` version 1 indexes contain both fingerprints, count, body size and CRC32.
Each entry has a 32-byte tag, 96-byte display name, 96-byte English name and
signed native index (`-1` for custom languages). Text fields are zero-padded.
Entries sort by tag; duplicate tags, including case-only differences, and
duplicate nonnegative native indices are rejected.

Current host limits are 64 languages, 4,096 strings per pack, 262,144 bytes per
pack and 4,096 bytes per terminated string. Names are limited to 95 UTF-8 bytes.
CRC32 detects corruption; it does not authenticate a pack.

## Current Native Language IDs

The CFW follows the iPod's selected system language. For Apple firmware 2.0.5,
translations use the following language IDs:

| ID | Language |
|---:|---|
| 0 | English |
| 1 | Japanese |
| 2 | Czech |
| 3 | Danish |
| 4 | German |
| 5 | Spanish |
| 6 | French |
| 7 | Greek |
| 8 | Italian |
| 9 | Hungarian |
| 10 | Dutch |
| 11 | Norwegian |
| 12 | Portuguese |
| 13 | Russian |
| 14 | Polish |
| 15 | Finnish |
| 16 | Swedish |
| 17 | Turkish |
| 18 | Korean |
| 19 | Simplified Chinese |
| 20 | Traditional Chinese |

## Translation catalogs

A `.lo` catalog sits alongside its corresponding `.ui` file and shares the same
base name, for example [cfw_info.lo](../payload/cfw_info.lo) for
[cfw_info.ui](../payload/cfw_info.ui). The catalog is loaded automatically during
the build. The `lang` attribute selects the language of a translation:

```xml
<locale>
  <string id="CFW_Info_Title" text="CFW Info"/>
  <string id="CFW_Info_Title" lang="4" text="CFW-Informationen"/>
  <string id="CFW_Info_Title" lang="6" text="Informations sur le CFW"/>
</locale>
```

The UI references the text using `@CFW_Info_Title`. IDs are scoped to each UI
document, including its catalog.

## Default text and fallback

Each text ID requires exactly one declaration **without `lang`**. This provides
the default text, usually in English. `lang="0"` explicitly selects English but
does not replace the required default declaration.

If a translation is missing for the selected language, the default text is used.
This also applies if the system language is unknown or the locale service has not
been initialized. Each language ID may appear only once per text ID; declaration
order does not matter.

## Editing and building

Catalogs are saved as UTF-8. Accented letters and other characters may appear
directly in the text; XML character references are also supported. A language ID
does not imply that all CFW text has already been translated. Changes take effect
in the next firmware build.

The CFW Info page content remains in English; its menu entry and page title can
be translated. Dynamic text uses `<format>` templates. For details on text
references, placeholders, and the C API, see
[UI string references](osos.md#ui-string-references).