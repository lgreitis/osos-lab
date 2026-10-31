# OSOS runtime

Addresses are for Apple 2.0.4 on Classic 7G Rev B.

## Relocation and memory

OSOS has a `0x800`-byte IMG1 header. Startup copies the first `0xaed8` body bytes
into IRAM at `0x22000000`, clears its BSS, then moves main code/data down to
`0x08000000`. Main runtime begins at complete-file offset `0xb6d8`.

| Region | Address / mapping |
| --- | --- |
| Main OSOS runtime | `address = file_offset + 0x07ff4928` |
| IRAM startup/audio | `address = 0x22000000 + file_offset - 0x800`, within the startup region |
| Translation table | `0x2200c000`, 16 KiB |
| Imported SysInfo | `0x08a11cd8`, `0x120` bytes |

Startup maps low vectors into IRAM, identity-maps IRAM and cached DRAM, adds
uncached DRAM aliases and MMIO, then enables the MMU and caches. Main and IRAM
hooks require different file mappings. Zero-filled bytes do not establish safe
scratch space. The CFW reservation is described in [boot sequence](boot.md).

## Tasks, graphics and storage

`0x0805d558` creates and starts 21 task descriptors in two passes. GraphicsManager
enters through `0x08388570`, constructs its object at `0x0815ad68`, and runs the
message loop at `0x0815add8`. UI can execute before the startup task returns.

| Address | Storage behavior |
| --- | --- |
| `0x08278ba4` | Disk initialization path; calls `0x082b1db0(0, 0)`. |
| `0x083619b0` / `0x08361388` / `0x0836140c` | PATA power, reset/init and IDENTIFY. |
| `0x080ccca4` | ATA status polling; returns 3 when BSY and DRQ are clear. Caller `0x080b9c1c` maps this to 59. |
| `0x08058584` | Mounts resource volume 4 from firmware-directory entry `rsrc`. |
| `0x0817ab74` / `0x0826a824` | Font lookup; Pixo table at `0x08a7a254`, fallback under volume 4 `Resources/Fonts`. |

Bootloader and OSOS ATA code have different initialization and wait behavior.
Successful Rockbox reads alone do not validate the Apple storage handoff.

## Audio and volume

Native EQ runs three stereo biquads with signed Q24 coefficients and S16
saturation after each stage. Presets have separate 44.1/48 kHz coefficient banks.
`0x08271bf0` loads coefficients, `0x08271d88` processes one stereo biquad, and
`0x08271f2c` runs all three. Effects/output dispatch is at `0x0818c320`.
Post-effects Q15 gain cannot undo EQ clipping; precut must act before saturation.

Regional policy is tested at `0x0804b150`; ceiling getter: `0x081553c4`.
User volume caps are separate: setter `0x081552f4`, preference load `0x0815537c`,
applied-volume setter `0x08155568`. Ceilings 216 and 256 are internal units,
not percentages or dB.

## Settings language (Apple 2.0.5)

`0x081e21e0` takes a Settings model and returns its saved language index, or `-1`
if no matching preference exists. It reads resource type `prID` (`0x70724944`),
property `0x6066`, from the provider at model offset `0x378`, then searches the
21 twelve-byte entries at `0x089d4a24`. Each entry contains a string resource ID,
a locale code, and a preference ID. Index 0 is English; index 4 is German,
with locale `deDE` (`0x64654445`) and preference ID `0x606a`.

`HandleLanguage` in `0x0821daf4` calls `0x0821e360`, which passes the selected
index to `0x081e226c`. That function applies the locale, saves the preference and
refreshes the root UI. `UI_InitResourceProviders` at `0x081c7e68` registers the
locale provider as global service `0x80`. Its constructor at `0x08122e88` installs
the vtable at `0x0898ba50`; the locale setter at slot `0x74` (`0x08122df0`) calls
`0x08193f34`, which stores the active locale code at provider offset `0x40`.
The CFW resolver reads this code through the global UI service and maps it through
the language table, so no Settings model is required.

Direct disassembly of the pinned image also confirms Settings property `0x8916`:
`0x081e1d84` returns a `VMin` of zero and `VMax` of 21. The indexed-property branch
at `0x081e32c0` calls `0x081e1ea8`, which retrieves the table entry's string ID.
`0x0821e360` obtains the selection index through vtable slot `0x168` and passes it
to `0x081e226c` without a bounds check. Do not pass an external index to this
routine or merely increase the native language count.

The setter at `0x08193f34` updates locale/provider fields; this alone does not
prove that every view has released borrowed text pointers. External runtime
selection remains gated on that lifetime analysis. The host-side export and pack
compiler are documented in [languages.md](languages.md#external-language-packs-implementation-status).

The existing resource-service hook resolves translated `Str ` IDs before the
original lookup. Untranslated IDs and other resource types are passed through.
Existing dynamic Settings sources remain unchanged. Dark Mode and CFW Info
provide English/German XML examples; CFW Info keeps its page content in English.

UI string translations share one ID and use the native language index:

```xml
<string id="CFW_Info_Title" text="CFW Info"/>
<string id="CFW_Info_Title" lang="4" text="CFW Informationen"/>
```

Exactly one declaration without `lang` is required as the fallback.
Indices range from 0 to 20 for Apple 2.0.5; each index may appear once per ID.
Declaration order does not matter. The generator emits the language table and
the shared resource resolver selects a variant on each lookup, without
string-specific C code. Unknown locales or missing translations use the default.
The same fallback applies before the locale provider is initialized. Strings
are encoded as UTF-8; XML character references can also express non-ASCII text.

## UI string references

All UI documents can reference their declared strings by prefixing an ID with
`@`. Existing literal text remains valid; `@@` escapes a literal leading `@`.

```xml
<ui>
	<values id="states"><value number="0" label="@OFF_TEXT"/></values>
	<menu id="EQ" title="@EQ_TITLE">
		<selector id="EQ_STATE" title="@STATE_TITLE" values="states" slot="0" default="0"/>
	</menu>
	<string id="EQ_TITLE" text="Custom EQ"/>
	<string id="EQ_TITLE" lang="4" text="Eigener EQ"/>
	<string id="STATE_TITLE" text="State"/>
	<string id="STATE_TITLE" lang="4" text="Status"/>
	<string id="OFF_TEXT" text="Off"/>
	<string id="OFF_TEXT" lang="4" text="Aus"/>
</ui>
```

References work in menu, selector, action, info, toggle, text-page and music-entry
titles, value labels, and range suffixes. Page headers, Settings previews,
selected-value summaries and selection markers inherit the same translations.
Composed labels fall back independently for each referenced string.

An info row can set `unknown="@UNKNOWN_TEXT"` to translate its initial unknown
value. A text-file paragraph consisting of `@BODY_TEXT` uses that string's
variants, including line breaks. Text-file paths are not string references.
Use `&#10;` for line breaks inside XML attributes. Text-page strings also expand
`{build_version}` and `{build_revision}`, just like literal text-file paragraphs.
Strings themselves contain literal text, not recursive references. Undefined
references fail compilation. IDs are scoped to their UI document.

Feature code that assembles dynamic text in C must use translated string resources explicitly;
changing an initial info-row label does not translate later C-generated values.

A same-stem `.lo` catalog is loaded automatically alongside a `.ui` document.
Its `<locale>` root accepts the same `<string>` declarations and explicit
`<format>` declarations, sharing the document's ID namespace:

```xml
<locale>
	<format id="TRACK" text="Track %1$u of %2$u"/>
	<format id="TRACK" lang="4" text="Titel %1$u von %2$u"/>
</locale>
```

`cfw_string(ID)` returns the current language's raw UTF-8 text, with the default
as fallback. `cfw_format(buffer, capacity, ID, ...)` formats a catalog template
and returns the number of bytes written, excluding its terminator. Supported
placeholders are `%s` (`const char *`), `%u` (`uint32_t`), and `%%`; numbered
arguments such as `%2$s` allow reordering and repetition. Use at most eight
arguments and do not mix numbered and sequential placeholders. Translation
argument types must match the default, which is checked during compilation.
Ordinary `<string>` declarations treat percent signs literally.

Formatting is bounded, NUL-terminated when capacity is nonzero, and does not
split UTF-8 characters on truncation. Song Info uses this API for its dynamic
rows; its templates and English/German wording live in `payload/song_info.lo`.
Keep translations on format IDs, not on the final dynamically updated row IDs,
so resource lookup does not replace completed rows with static placeholders.
