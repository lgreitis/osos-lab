# osos-lab

RepriseOS adds features to Apple's original iPod firmware while retaining its
familiar interface and playback system. This repository contains the patches,
reverse engineering, and tools used to build and package it.

> Current support: iPod Classic 7G Rev B, Apple 2.0.4, FAT32. Support for more models is planned.

- `payload/`: OSOS features, C patch declarations, and UI definitions.
- `game-sdk/`: C runtime and APIs for native homebrew games.
- `doom/`: Doom port, build tooling, and game packaging example.
- `patching/`: Python recipe builder, native UI generation, and framework tests.
- `targets/`: supported firmware fingerprints and UI structure metadata.
- `loader/` and `patches/`: Apple companion loader and Rockbox bootloader patches.
- `usb-helper/`: FAT32 transfer, readback verification, and DFU return.
- `host/`: Rust device library, IPSW/NOR preparation, bundle assembly, and CLI.
- `tools/`: firmware builds, release bundle export, and Ghidra import/export.
- `ghidra/`: tracked firmware analysis.

Boot selection: **no buttons → RepriseOS**, **Menu → Apple OS**,
**Play/Pause → Rockbox**. RepriseOS loads `/osos-cfw.bin` and `/cfw-loader.bin`.
See [firmware references](docs/README.md), [boot sequence](docs/boot.md)
and [host tools](host/README.md).

## Features

- Three-band custom EQ with adjustable filter types, frequency, gain, Q and precut.
- Play Next and Play Last from song context menus.
- EU volume limit removal for European iPods.
- An experimental [Doom port](doom/README.md).

The Doom port currently has no audio or saving.

## Build

Requires Python 3.11+, Git, Make, Perl, a host C compiler, and Rockbox's
`arm-elf-eabi-` GCC 9.5.0 toolchain. Rust tools require Cargo, libusb, and
pkg-config. Use Xcode Command Line Tools on macOS or libusb development headers
on Linux.

```sh
python3.11 tools/setup.py
cargo build --manifest-path host/Cargo.toml --release -j 8
python3.11 tools/import_inputs.py --osos /path/to/osos.bin \
  --apple-loader /path/to/apple-loader.bin --nor /path/to/nor.bin \
  --modules /path/to/modules
python3.11 tools/build.py --cross-prefix /path/to/bin/arm-elf-eabi-
```

Firmware builds use the inputs in [the target manifest](targets/classic7g-2.0.4.json).
Outputs go to `build/`, intermediates to `.build/`. Individual targets:
`osos`, `osos-recipe`, `loader`, `loader-recipe`, `bootloader`. `tools/setup.py`
prepares pinned dependencies under `vendor/`; all project tooling lives in this repository.

`osos-recipe` and `loader-recipe` produce JSON recipes and compiled data without
Apple inputs. `osos` and `loader` also assemble images with local inputs through
the shared Rust assembler and require Cargo.

## Distribution

```sh
python3 usb-helper/build.py --rockbox vendor/rockbox \
  --toolchain /path/to/bin --out build/helper
python3 tools/export_bundle.py --helper build/helper --version 0.1.0-dev.1 \
  --out build/package
python3 tools/bundle.py --pack build/package --out build/package.zip
```

Bundles contain compiled patches, assembly recipes, the NOR installer, and the
USB helper. The desktop installer combines them with the user's IPSW and NOR
backup. Local ZIPs use compatibility and hash checks; downloaded releases use
signed manifests. Compatible firmware components are interchangeable.

## Ghidra

Ghidra tooling requires 12.1.4 and JDK 21+;
set `GHIDRA_INSTALL_DIR` and `JAVA_HOME`, then build `reprise-cli` as above.

```sh
python3.11 tools/ghidra.py import --target classic7g-2.0.4 \
  --inputs /path/to/decrypted-inputs --project /path/to/new-project
python3.11 tools/ghidra.py export --project /path/to/new-project
```

Import restores saved annotations by default; `--fresh` creates new analysis.
Alternatively supply `--ipsw FILE --nor FILE --osos DECRYPTED_FILE` and, for
an encrypted NOR, `--apple-loader DECRYPTED_FILE`; the IPSW selects the target.
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
