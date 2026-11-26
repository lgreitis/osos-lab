# RVX extension SDK

Build RepriseOS extensions for iPod Classic OSOS 2.0.5 with RVX API 1.85.
Based on the Revola SDK, revision `f13b88cd194a` (API 1.85), with its `rv_api.h`
unchanged. The upstream [license](COPYING), [notices](NOTICE) and
[SDK reference](docs/README.md) are included.

## Build the Hello example

From the SDK directory, with Python 3.11+, GNU Make and the Rockbox ARM GCC 9.5.0 toolchain on your PATH:

```sh
make -C examples/hello all SDK="$PWD" PYTHON=python3.11 CROSS_COMPILE=arm-elf-eabi-
```

Copy `examples/hello/build/hello.rvx` to `/Reprise/Extensions/` on the iPod. Reboot into RepriseOS and open **Settings → Hello**.
