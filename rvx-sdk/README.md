# RVX extension SDK

Build RepriseOS extensions for iPod Classic OSOS 2.0.5 with the Revola SDK 1.81.
The SDK was imported from Revola revision `5c96ca75d138`. The upstream
[license](COPYING), [notices](NOTICE) and [SDK reference](docs/README.md) are included.

## Build the Hello example

From the root of this checkout, with Python 3.11+, GNU Make and the Rockbox ARM toolchain:

```sh
make -C rvx-sdk/examples/hello all SDK="$PWD/rvx-sdk" \
  CROSS_COMPILE="$PWD/toolchains/rockbox-arm/bin/arm-elf-eabi-" PYTHON=python3.11
```

Copy `rvx-sdk/examples/hello/build/hello.rvx` to `/Reprise/Extensions/` on the iPod.
Reboot into RepriseOS and open **Settings → Hello**.
