# NES for RepriseOS

InfoNES port on the [eApp SDK](../eapp-sdk/README.md). Supports iNES mappers 0, 1,
2, 3, 4, 7 and 66. No audio, saves or NES 2.0 support.

## Build and install

From the repository root:

```sh
python3 nes/setup.py
make -C nes -j8 CROSS_COMPILE=../toolchains/rockbox-arm/bin/arm-elf-eabi-
```

`nes/build/reprise-nes/` is the package. Copy it to `/Reprise/Games/reprise-nes/`
on the iPod, where the Homebrew extension lists it. Put your `.nes` files into
that folder or its subfolders; up to 64 ROMs are found, none are included.

Open **Extras → Games → NES**, choose a ROM, then select PAL or NTSC to match it.

## Controls

| Input | Action |
| --- | --- |
| Previous / Next click | Left / Right |
| Play click | Down |
| Center click | A |
| Menu click | B, switchable to Up in the pause menu |
| Hold switch | Open pause menu; unlock to use it |
| Start Button in pause menu | Send Start and resume |

The pause menu also offers Resume, Game List and Quit. Wheel scrolling navigates
menus. Select is unmapped, following the [InfoNES Rockbox layout](https://github.com/pcwizzy37/infones-rockbox/blob/master/keymaps.h).

## Tests

`python3 nes/tests/run.py` runs the core and the port on the host against a
synthetic ROM, after `nes/setup.py`.
