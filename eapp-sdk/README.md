# eApp SDK

The SDK for apps and games on Apple's eApp host, the runtime in the iPod Classic 2.0.5 firmware behind the iPod games from iTunes. An app reaches the host through the image's import list by interface name, as Apple's games do. Packages run under RepriseOS, where the Homebrew extension lists them under Extras, and from `iPod_Control/games_RO/<id>/` on any 2.0.5 firmware that skips the game signature check.

A copy of the Revola eApp SDK at `4ca117d678f6` (SDK 2.26), with the package paths under `/Reprise`. [LICENSE-MIT](LICENSE-MIT), [COPYING](COPYING) and [NOTICE](NOTICE) come with it.

## Layout

| Path | Contents |
|---|---|
| the root | The headers an app includes (`eapp.h` and the `eapp_*.h` beside it), `eapp.mk`, and `eapp-pack` with its Python module `eapp_package.py` |
| `runtime/` | The runtime linked into every image: startup, import list, host calls, drawing |
| `include/` | The Khronos OpenGL ES 1.1 headers |
| `firmware/` | The symbols header copied from the Revola reversing repository: host interface ids, types and image limits |
| `examples/` | Example apps |
| `tests/` | Host checks of the runtime against stand-ins for the host and of `eapp-pack`; `tests/device/` holds apps that check what only an iPod shows |
| `docs/` | How to write and package an app |

## Build

Requires GNU make, Python 3, Rockbox's `arm-elf-eabi-` GCC 9.5.0, and for the host checks a C compiler with AddressSanitizer (`HOST_CC`, `cc` by default). An app's Makefile includes `$(SDK)/eapp.mk` from this checkout.

```sh
make -C examples/hello CROSS_COMPILE=/path/to/arm-elf-eabi-
python3 tests/run.py
```

## Versions

`EAPP_SDK_MAJOR` and `EAPP_SDK_MINOR` in `eapp.h` are the SDK version an app builds against.

## Documentation

| Page | Contents |
|---|---|
| [docs/](docs/README.md) | Writing, building and packaging an app |

## License

The headers, `runtime/` and `examples/` are MIT (LICENSE-MIT); an app that links the runtime ships a copy. `eapp.mk`, `eapp-pack`, `tests/` and the symbols header in `firmware/` are GPL-3.0-only (COPYING). NOTICE lists what the repository contains.
