# Reprise host tools

Rust libraries and CLI for iPod firmware and device operations.
See [current hardware support](../README.md).

- `reprise-device`: DFU checks, NOR backups, AES decryption, storage inspection,
  verified file uploads, and bootloader installation.
- `reprise-bundle`: signed downloads, local ZIP loading, IPSW/NOR preparation,
  and firmware assembly.
- `reprise-cli`: command-line access to device and bundle operations.

Requires Rust, libusb, and pkg-config. Run from `host/`:

```sh
cargo run -- --help
cargo run -- devices
cargo run -- check
cargo run -- nor-dump --output nor.bin
cargo run -- firmware inspect --ipsw /path/to/firmware.ipsw
cargo run -- firmware extract --ipsw /path/to/firmware.ipsw --output osos.img1
cargo run -- decrypt --input osos.img1 --output osos-body.bin
cargo test --workspace -j 8
```

Device commands use BootROM DFU. Select an iPod with `--device BUS:ADDRESS`;
use `--json` for machine-readable results. Decryption outputs the IMG1 body
with AES padding. Raw ciphertext accepts `--skip-bytes`, `--offset`, and `--size`.

Build the upload helper with [`usb-helper/build.py`](../usb-helper/build.py).
`upload` accepts `--helper DIR` or `--bundle DIR --key FILE`; `bundle` provides
`inspect`, `sign`, `fetch`, and `assemble`. Key files contain hex-encoded keys.

Optional firmware replay tests take `REPRISE_ASSEMBLY_ROOT` (this repo) and
`REPRISE_TEST_IPSW` (an explicit file path); assembly replay also takes
`REPRISE_ASSEMBLY_NM`. ZIP replay uses `REPRISE_TEST_PACKAGE` and
`REPRISE_ASSEMBLY_BUILD`. See the ignored tests for saved BootROM/NOR inputs.

GPL-3.0-only; see [COPYING](COPYING). DFU code derives from
[wInd3x](https://github.com/freemyipod/wInd3x) by Serge “q3k” Bazanski;
the helper uses [Rockbox](https://github.com/Rockbox/rockbox) drivers.
