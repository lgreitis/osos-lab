# RepriseOS RVX SDK

An extension is a set of modules built for a firmware target and packed into one `.rvx` file. The SDK holds the headers, linker script, Make include and packer it needs. Apps and games run on Apple's eApp host and build against the [eApp SDK](https://github.com/paviro/revola-eapp-sdk).

| Page | Contents |
|---|---|
| [Manifest](manifest.md) | Every key of `manifest.toml` |
| [Modules](modules.md) | Entry point, descriptor, versions, module triggers, build rules, when to use ARM |
| [User interface](ui.md) | Declaring screens, rows, settings and entries; canvases; resource limits |
| [Menu placement](menus.md) | Putting a row into a stock menu |
| [Settings](settings.md) | Reading, writing and reacting to settings |
| [Files](files.md) | The extension's data folder and the content folders it owns |
| [Assets](assets.md) | Bitmaps and data packed into the `.rvx` |
| [Raw sites](raw-sites.md) | Hooking code no core site covers |
| [Audio effects](audio.md) | The output PCM site and the effect stages |
| [Canvas kit](canvas.md) | Palette, plots, value cells and Select/Menu input for an editor canvas |
| [Media items](media.md) | Reading the items the context menus hand out and the player's |

## Files of the SDK

| File | Contents |
|---|---|
| `rv_api.h` | The `rv_api` table the core passes to a module's entry, the descriptor it returns, hook, setting, screen, row and entry structs, `RV_API_MAJOR` and `RV_API_MINOR` |
| `rv_debug.h`, `rv_theme.h` | Debugger structures (`api->debug_info()` returns `NULL` on RepriseOS) and the asset provider a theme sets with `api->asset_set_provider` |
| `rv_ui.h` | One-line declarations over those structs: `RV_ROW_PUSH`, `RV_SCREEN_LIST`, `RV_MENU_ENTRY`, `RV_SETTING_CHOICE` and the rest |
| `rv_libc.h`, `rv_libc.c` | The C library `rv.mk` links into every module; a module carries only what it calls |
| `rv_canvas.h`, `rv_canvas.c` | The [canvas kit](canvas.md), linked like `rv_libc` |
| `rv_audio.h` | Shared [audio effect](audio.md) helpers: format check, `rv_fracmul`, `rv_saturate16`, handoff, fade and `struct rv_effect`; the limiter is in the `dsp` library |
| `rv_media.h` | [Media items](media.md): the item, the player and a song model's rows |
| `rv_sites.h` | Named hook IDs. RepriseOS supports all listed IDs except `RV_SITE_IRQ_Dispatch_entry` |
| `rv_menus.h` | The stock rows of the Main, Music, Extras and Settings menus that an entry anchors on, generated from the symbols header |
| `rv_firmware.h`, `rv_firmware.mk` | `RV_FIRMWARE_ID` of the Apple firmware the symbols describe; the loader refuses a module built for another |
| `osos-2.0.5.h`, `osos-2.0.5.ld` | Apple functions, data, constants and struct offsets by name; modules call Apple directly |
| `rvx.lds` | Module layout: linked at 0, text, rodata, data and bss in one segment |
| `rv.mk` | Make include, `include $(SDK)/rv.mk` with `SDK` the root of the SDK |
| `rvx-pack`, `rvx.py`, `manifest.py`, `bitmap.py` | Pack `manifest.toml`, the module ELFs and the assets into the `.rvx`; Python 3.11+ and the `arm-elf-eabi-` binutils |
| `assets.py`, `assets.json` | A built extension's bitmap assets as JSON records (id, asset, format, size) for themes to match; `assets.json` holds the core's own |
| `docs/` | These pages, at the root of the SDK |

`rv_libc.h` provides `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `rv_format` and `rv_vformat` (printf into a buffer), `rv_format_duration` ("3:07"), `rv_format_int` ("100 %"), `rv_format_tenths` ("-0.5 dB"), `rv_format_hz` ("1.5k Hz"), `rv_copy_utf8` (a copy that never cuts inside a UTF-8 sequence) and `rv_clamp`. `api->format_number` and `api->format_size` write "12,345" and "6.0 KB".

## A first extension

`manifest.toml` names the extension and its modules; [Manifest](manifest.md) lists every key.

```toml
id = "hello"
name = "Hello"
description = "Adds a Hello page to Settings."
version = "0.1.0"
author = "You"
license = "GPL-3.0-only"
api = "1.78"                 # the oldest core API major.minor the extension runs on
category = "other"           # where Settings > Extensions lists it

[[module]]
name = "main"
trigger = "always"
```

An installed extension is off until the listener turns it on in Settings > Extensions. Only the first-party extensions in `core/loader/bundled.def` start on.

`Makefile` names each module's sources and includes `rv.mk`, which takes the rest from the manifest:

```make
main_SOURCES := main.c
include $(or $(SDK),$(error set SDK to the unpacked RepriseOS RVX SDK))/rv.mk
```

`main.c` exports `rv_module_entry`, keeps the API table and its handle, and returns a descriptor:

```c
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

static const char *const about[] = {"Hello from an extension."};

static const struct rv_screen screens[] = {
    RV_SCREEN_TEXT("about", "Hello", about),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("Hello", "about"),
                  .pane = RV_PANE(.caption = "Says hello")),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension, RV_SCREENS(screens), RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
```

```sh
make SDK=/path/to/rvx-sdk
```

Copy `build/hello.rvx` to `/Reprise/Extensions/` on the iPod, reboot, and open **Settings → Hello**. The [extensions repository](https://github.com/RepriseOS/repriseos-extensions) has larger examples, including Balance’s screen module and Crossfeed’s DSP library dependency.
