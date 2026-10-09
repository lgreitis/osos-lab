# osos-lab

RepriseOS adds features to Apple's original iPod firmware while retaining its
familiar interface and playback system. This repository contains the patches,
reverse engineering, and tools used to build and package it.

Builds use Apple **2.0.5** for all supported Classic hardware, with each device's
own identity in its companion.

## Compatibility

- iPod Classic 6G, 6.5G and 7G.
- FAT32 storage required.

## Repository layout

- `payload/`: OSOS features, C patch declarations, and UI definitions.
- `game-sdk/`: deferred homebrew runtime; its old bindings are not ported to 2.0.5.
- `doom/`: deferred, incomplete Doom port.
- `patching/`: Python recipe builder, native UI generation, and framework tests.
- `firmware/`: pinned Apple inputs, hardware eligibility and native UI metadata.
- `loader/` and `patches/`: Apple companion loader and Rockbox bootloader patches.
- `usb-helper/`: FAT32 transfer, readback verification, and DFU return.
- `host/`: Rust device library, IPSW/AUPD preparation, bundle assembly, and CLI.
- `tools/`: firmware builds, release bundle export, and Ghidra import/export.
- `ghidra/`: tracked firmware analysis.

Boot selection: **no buttons → RepriseOS**, **Menu → Apple OS**,
**Play/Pause → Rockbox**. RepriseOS loads `/osos-cfw.bin` and `/cfw-loader.bin`.
See [firmware references](docs/README.md), [boot sequence](docs/boot.md)
and [host tools](host/README.md).

## Install

Download RepriseOS Installer from [the website](https://repriseos.com/download/)
or [GitHub releases](https://github.com/lgreitis/repriseos-installer/releases).
Use **`iPod_38.2.0.5.ipsw`** for every supported Classic. The installed Apple
firmware can be older. Follow the app's DFU instructions; it downloads the
RepriseOS package and builds a companion using that iPod's identity.

## Features

- Three-band custom EQ with adjustable filter types, frequency, gain, Q and precut.
- Play Next and Play Last from song context menus.
- EU volume limit removal for European iPods.
- Song Info and an Album Artists browser with Artist fallback.

The game SDK and [Doom port](doom/README.md) are deferred.

## Build

Requires Python 3.11+, Git, Make, Perl, a host C compiler, and Rockbox's
`arm-elf-eabi-` GCC 9.5.0 toolchain. Rust tools require Cargo, libusb, and
pkg-config. Use Xcode Command Line Tools on macOS or libusb development headers
on Linux.

```sh
python3.11 tools/setup.py
cargo build --manifest-path host/Cargo.toml --release -j 8
python3.11 tools/import_inputs.py --ipsw /path/to/iPod_38.2.0.5.ipsw \
  --osos /path/to/osos.bin --aupd /path/to/aupd-body.bin \
  --out /path/to/new-inputs
python3.11 tools/build.py --inputs /path/to/new-inputs --nor /path/to/device-nor.bin \
  --cross-prefix /path/to/bin/arm-elf-eabi-
```

Firmware builds use the inputs in [the firmware manifest](firmware/apple.json).
Outputs go to `build/`, intermediates to `.build/`. Individual targets:
`osos`, `osos-recipe`, `loader`, `loader-recipe`, `bootloader`. `tools/setup.py`
prepares pinned dependencies under `vendor/`; all project tooling lives in this repository.

`osos-recipe` and `loader-recipe` produce JSON recipes and compiled data without
Apple inputs. `osos` and `loader` also assemble images with local inputs through
the shared Rust assembler and require Cargo.

## Distribution

```sh
python3.11 tools/release.py --cross-prefix /path/to/bin/arm-elf-eabi- --jobs 8
```

This builds `build/releases/classic/repriseos.zip` without Apple firmware inputs.
Bundles contain compiled patches, assembly recipes, the NOR installer, and the
USB helper. Assembly combines them with the 2.0.5 IPSW-derived inputs and the
device's NOR identity. Schema 2 bundles require installer **0.2.0 or newer**;
installer 0.1.x does not support this format. Local ZIPs use compatibility
and hash checks; downloaded releases use signed manifests. Never copy a
personalized companion between devices. Resources currently come from the
installed Apple resource partition; dedicated resources are deferred.

## Ghidra

Ghidra tooling requires 12.1.4 and JDK 21+;
set `GHIDRA_INSTALL_DIR` and `JAVA_HOME`, then build `reprise-cli` as above.

```sh
python3.11 tools/ghidra.py import --target firmware-2.0.5 \
  --inputs /path/to/decrypted-inputs --project /path/to/new-project
python3.11 tools/ghidra.py export --project /path/to/new-project
```

Import restores saved annotations by default; `--fresh` creates new analysis.
Alternatively supply `--ipsw FILE --osos DECRYPTED_FILE --aupd DECRYPTED_BODY`
for 2.0.5. Historical 2.0.1/2.0.4 analysis remains importable from preserved
plaintext inputs using the profiles in `ghidra/profiles/`.
Preparation is offline and requires supported plaintext firmware. Import verifies
a temporary project before publishing it and requires a new destination directory.
Save and close Ghidra before exporting. Use `--analysis DIR` for a separate
annotation directory. Logs are in `.build/ghidra-*.log`; all tools expose `--help`.

## Credits

- [Rockbox](https://github.com/Rockbox/rockbox) contributors: the bootloader,
  dual-boot installer, and hardware drivers used by the USB helper.
- [wInd3x](https://github.com/freemyipod/wInd3x) by Serge “q3k” Bazanski:
  the BootROM exploit, DFU protocol, and payload assembler code ported from Go
  to Rust in `reprise-device`, including `payload.rs` and `dfu.rs`.

## Support

♥ [Support on Ko-fi](https://ko-fi.com/lgreitis). Help fund my questionable iPod purchases.
