# Modules

A module is one ELF, linked at 0 with `rvx.lds` and relocated into the Apple heap by the loader. Every module exports `rv_module_entry` (type `rv_module_entry_fn`), which returns a `struct rv_descriptor`. Modules other than `main` may return NULL if they declare no hooks or screens.

## Descriptor

| Field | |
|---|---|
| `size` | `sizeof(struct rv_descriptor)` as built; the core refuses another size |
| `hooks`, `hook_count` | `RV_HOOKS(array)`; bound in order, a conflict fails the load with the site named in Settings > Extensions |
| `settings`, `setting_count` | `RV_SETTINGS(array)` |
| `screens`, `screen_count` | `RV_SCREENS(array)` |
| `entries`, `entry_count` | `RV_ENTRIES(array)` |
| `unload` | called before the hooks are unbound; may be NULL |
| `setting_changed` | see [Settings](settings.md); may be NULL |
| `service` | a table of the module's own functions for the extension's other modules and, for `main`, for other extensions; see Services below; may be NULL |

## Versions

`RV_API_MAJOR` and `RV_API_MINOR` in `rv_api.h` name the SDK. The manifest's `api` is the oldest core the extension runs on: the core leaves an extension asking for a newer minor off, and `rvx-pack` refuses one newer than the SDK it builds with or of another major. From the first release the `rv_api`, `rv_gfx` and `rv_debug_info` tables only grow at the end within a major, and the major stays 1 until then, so a minor before it may change a layout; `RV_HAS(api, entry)` says whether the core's table has an entry, for an extension that runs on older cores and calls a newer one when it is there.

## Triggers

| `trigger` in the manifest | Loaded | Unloaded |
|---|---|---|
| `always` | while the extension is enabled; only the first module, `main` | when the extension is disabled |
| `screen` | when a push looks up one of the ids in its `screens` list | as soon as its last screen closes, unless `preload = "always"` |
| `manual` | `api->module_load` | on `api->module_unload` once its last screen closes, unless `preload = "always"` |

A non-main module's hooks and screens are bound when it loads and dropped when it unloads.

On RepriseOS, `preload = "always"` loads a module with `main` and keeps it resident. An `optional` (default) or `never` module loads on its trigger. Reopening an unloaded screen immediately runs its module entry again with fresh static storage. Keep state that must survive closing the screen in `main` or in an extension setting.

## Services

Modules share no symbols. A module exposes functions by pointing its descriptor's `service` at a table. `api->module_service(self, module)` returns that pointer (module 0 is `main`), or NULL while the module is not loaded. A manual module can fetch `main`'s table in its entry; it stays valid while the extension is loaded. `module_unload` keeps no count, so one module should own each load and unload pair.

Between extensions, `api->extension_service(id)` (API 1.85) returns the `service` of the main module of the extension with manifest id `id` while it runs, or NULL. Call it on the UI task each time the table is needed: the table goes when that extension unloads. It is for a dependency the caller works without. An extension that cannot work without another lists it under `[libraries]` instead. The provider declares the table's struct and its manifest id in a header the caller includes.

On extension unload, `main`'s `unload` callback runs first, while other modules are still loaded and can still be loaded. Then the other modules unload, highest manifest index first, and `main` last.

```c
struct worker_service { int (*run)(const struct job *job); };
/* in the manual module */
static const struct worker_service service = {run};
static const struct rv_descriptor module = {.size = sizeof module, .service = &service};
/* in main, after api->module_load(self, 1) */
const struct worker_service *worker = api->module_service(self, 1);
```

## Libraries

A library is an extension whose functions other extensions call directly. Its manifest says `category = "library"` and lists the functions in `exports`; it has one module, no sites and no content folders, and its entry may return NULL. An extension that calls them lists the library's id under `[libraries]`, as `required` or `optional` (at most four ids in all), and `rv.mk` finds the library's directory under `LIBRARY_PATH` (`..` by default), adds it to the include path, links its exports as 0 and tells `rvx-pack` which words are imports. The core starts the listed libraries before the extension and points every import at the export of the same name. Library code runs on whatever task calls it; a static inside the library is shared by every caller.

```toml
# dsp/manifest.toml
category = "library"
exports = ["rv_dsp_biquad", "rv_limiter_run"]

# equalizer/manifest.toml
[libraries]
required = ["dsp"]
```

Declare the exports in the library's header without `RV_MODULE_CALL` and with the name in parentheses, `int32_t (example_double)(int32_t value);`: a call into a library is a long call through an address the core fills in, and the parentheses keep the guard below out of the declaration. Imports resolve by name alone. An extension whose library is missing shows "Needs dsp" in Settings > Extensions; one whose library lacks a function it calls shows "Needs a newer dsp". A changed export signature gets a new name; the old one stays.

An optional library (API 1.84) lets the extension run without it. When it is not installed, failed to load, or lacks a function, the core leaves that import NULL and the extension loads anyway. `rv.mk` declares every export of an optional library weak, so the compiler keeps a test against NULL, and turns a bare call to one into a compile error. Call it with `RV_OPTIONAL`, which gives the fallback when the function is missing, or test it and call it with the name in parentheses:

```c
value = RV_OPTIONAL(example_double, 0, 21);
RV_OPTIONAL(example_log, (void)0, "text");

if (example_double)
    value = (example_double)(21);
```

On RepriseOS, extensions and their libraries start at boot, so an optional library installed later is used from the next boot. A library listed as `required` is never NULL, and a test against one does not compile.

A library has no Enabled setting. Its page lists the extensions using it, and Select on it only clears its crash mark after a crash in library code. The `dsp` extension of the extensions repository is a library, and the audio effects there call it.

## Build rules

`rv.mk` builds Thumb with `-mlong-calls -fno-pic -fno-jump-tables` and no C library beyond `rv_libc.h`. It reads the id, the modules in their order, the required libraries and the asset files from `manifest.toml`. `rvx-pack` checks the manifest first, matches each ELF to its module by name, refuses a manifest `api` newer than the SDK or of another major, and refuses any relocation the loader cannot apply: everything but `R_ARM_ABS32` words and branches within the module, and any relocation outside the text and data sections the linker script loads, debug information aside. A branch to a firmware function only holds where the module is linked, so firmware calls are long calls. Memory from `api->alloc` counts against the extension and shows in Settings > Extensions.

| Variable | |
|---|---|
| `<module>_SOURCES` | C and assembly sources, built as Thumb |
| `<module>_ARM_SOURCES` | sources built as ARM with `-marm -fno-optimize-sibling-calls` |
| `EXTRA_CFLAGS` | more compiler flags; set before the include |
| `LIBRARY_PATH` | the directory holding the required libraries' directories, each with its `manifest.toml` and header; `..` by default |
| `ASSETS` | the asset files the package depends on; the manifest's by default |

`make check` runs `tests/run.py` with the SDK when the extension has one.

## ARM

Thumb is the default because it saves RAM. Use ARM only in these cases, with a comment saying why:

| Case | Why |
|---|---|
| DSP and hot loops | the ARMv5TE multiply-accumulate and saturating instructions (SMULxy, SMLAxy, QADD) exist only in ARM state |
| Calls into Apple routines that return with `mov pc, lr` | such a return lands in ARM state, so a Thumb caller crashes; the call must come from an ARM function and must not be a tail call |

Mark single functions `RV_ARM`, or list whole files in `<module>_ARM_SOURCES`.

## Hooks

| Declaration | |
|---|---|
| `RV_HOOK_FILTER(site, fn)` | a handler that may call `api->hook_next` to continue |
| `RV_HOOK_FILTER_AT(site, fn, priority)` | the same at an explicit priority; an audio effect on the output PCM site passes its `RV_AUDIO_STAGE_*` value |
| `RV_HOOK_REPLACE(site, fn)` | an exclusive handler; a second claim on the site is a conflict |
| `RV_HOOK_WORD(site, word)` | an exclusive instruction replacement at a word site |
| `RV_HOOK_POINTER(site, object)` | the address of the module's own `object` at a word site that holds one |

`api->hook_bind(self, &hook)` and `api->hook_unbind(self, &hook)` bind and unbind a hook beyond the descriptor's at run time, for a group of sites an extension switches on and off. A handler the extension already has at the site, from a descriptor or bound before, returns `RV_ALREADY_BOUND`. When a module unloads, the core unbinds the bound hooks whose handler or pointer lies in its image; a word patch stays until the main module unloads. `api->hook_next7` continues a site with seven values, four in registers, two Apple passes on the stack and one the site adds, such as the view a blit handler needs.

`site` is an `RV_SITE_*` id from `rv_sites.h`. A handler receives the Apple arguments followed by `next`, which it passes to `api->hook_next` with the arguments for the rest of the chain. See [Hook sites](https://github.com/paviro/revola/blob/main/docs/core/hook-sites.md) for the calling convention.

`api->string_resource(self, text)` returns a string resource id for `text`, for tables Apple reads by resource id, such as a row added with `RV_HOOK_POINTER`.
