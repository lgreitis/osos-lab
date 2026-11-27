# Build

An app's `Makefile` sets the variables below and includes `$(SDK)/eapp.mk`. `make SDK=/path/to/eapp-sdk` writes the package to `build/<ID>/`.

| Variable | |
|---|---|
| `ID` | The package's folder name. Ids share one namespace with extensions |
| `NAME` | What Extras lists |
| `KIND` | `game` or `app`, the root `eapp-pack --zip` uses. The folder a package is copied to decides where it is listed |
| `SOURCES` | C sources, built freestanding without a C library |
| `ICON` | A 55 x 55 8-bit RGB or RGBA PNG |
| `HEAP_KIB` | The app's heap, 5120 by default and at most; the host treats larger values as 5120 |
| `VERSION` | The manifest's version, 1.0 by default |
| `FILES` | Data files copied into the package under their relative paths, which the app reads them by |
| `IMAGES` | PNGs packed as RGB565 pictures: `art/tiles.png` becomes `art/tiles.eimg`, transparent pixels the colour key. `images.h` gets `EAPP_IMAGE_TILES`, `EAPP_IMAGE_TILES_WIDTH` and `_HEIGHT` |
| `IMAGES_ALPHA` | PNGs packed as RGBA4444 textures for [OpenGL ES](opengl.md) |
| `IMAGE_PACKS` | Pack names. `<name>_IMAGES` and `<name>_IMAGES_ALPHA` list each pack's PNGs, written together as `<name>.epak`. `images.h` gets `EAPP_PACK_<NAME>`, an `EAPP_PACK_<NAME>_<STEM>` index per entry and the entry's `EAPP_IMAGE_<STEM>_WIDTH` and `_HEIGHT` |
| `MODULES` | Module names. `<name>_SOURCES` lists each module's sources, packed as `<name>.emod`; see [Modules](modules.md) |
| `NAMES` | A TOML file with the name per language, `de_DE = "Hallo"`, keyed by the locales under [Device](device.md). Other languages show `NAME`. Needs Python 3.11 |
| `EXTRA_CFLAGS`, `EXTRA_LDFLAGS` | Added to the compile and link flags |
| `LDLIBS` | Libraries linked between the app's objects and the runtime |
| `THUMB` | Non-empty: the app's and its modules' sources as Thumb code; see [Thumb](#thumb) |
| `ARM_SOURCES` | Sources that stay ARM under `THUMB`; `<name>_ARM_SOURCES` for a module |
| `EAPP_WERROR` | `-Werror` unless set to empty |

A generated source goes into `SOURCES` with its path, `gen/tiles.c`, and gets its own rule in the Makefile. The object rule takes any relative path.

## The package

`eapp-pack` checks the image's header, entries and import list, converts the icon and the pictures, and writes `Manifest.plist` and the package folder. The manifest lists the classic (PlatformID 3) and the nano 3G (2) with the same image. The nano is untested. `--zip` adds a ZIP with the folder under `Reprise/Games/<id>/` or `Reprise/Apps/<id>/`. A packaging script of the app's own can import the Python module `eapp_package.py`.

## Limits

| Limit | |
|---|---|
| Image | 1 MiB for code, data and bss; [modules](modules.md) hold more code |
| Heap | `HEAP_KIB`, at most 5120 KiB |
| Open files | 12, data files and newlib descriptors included |
| Open folders | 4. A recursive listing goes 7 levels below the folder opened and 500 entries per folder |
| Sounds | 32 at once on 8 voices |
| Save store | 4 slots, 100 KiB together |
| File picker | 64 files, labels of 42 characters |
| Text line | 63 glyphs in `eapp_draw_text_wrapped` |

## Thumb

`THUMB := yes` compiles the app's and its modules' C sources as Thumb code, which is smaller than ARM. Files in `ARM_SOURCES` or `<name>_ARM_SOURCES` stay ARM. The runtime is always ARM.

A few of the host's functions return in ARM state, which a Thumb caller does not survive, so Thumb code calls the host only through the SDK's functions; files in `ARM_SOURCES` may call it directly. The OpenGL ES functions return correctly and can be called from Thumb code too.

An app with its own link step adds `-mthumb` to its own compile rules; `THUMB` still builds the runtime the way Thumb code needs.

## With a C library

`eapp_libc.h` declares `memcpy`, `memmove`, `memset` and `memcmp`, which the runtime supplies because GCC emits calls to them.

An app that needs newlib compiles `runtime/newlib_glue.c` with its sources and supplies `eapp_libc_write` and `eapp_libc_exit`. The glue maps `malloc` to the heap, `open` (read only) to package files, `write` on the standard streams to `eapp_libc_write` and `exit` to `eapp_libc_exit`, which must unwind to the current callback. `atexit` handlers and `.init_array` do not run, so neither do C++ static constructors and GCC constructor attributes.

Such an app leaves `SOURCES` empty and writes its own rule for `$(OUT)/$(ID).elf` with `EAPP_ARCH_FLAGS`, `EAPP_CFLAGS`, `EAPP_LDFLAGS`, `$(EAPP_STARTUP)` and `$(EAPP_RUNTIME)`, linking `-lgcc` and its libraries; `eapp.mk` turns that ELF into the image and the package as for any app. `make clean` removes `$(OUT)`, so a library built once, such as newlib, lives outside it. The app ships newlib's object code and notices.

With modules, that rule also links `$(OUT)/modules.ld`, and `EAPP_LDFLAGS` carries `--build-id=md5` once `MODULES` is set; `eapp.mk` links the modules against the image's ELF and packs them as for any app.
