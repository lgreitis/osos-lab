# NES for RepriseOS

InfoNES port for the Apple 2.0.5 base. Supports iNES mappers 0, 1, 2, 3, 4, 7
and 66. No audio, saves or NES 2.0 support.

## Build and install

From `osos-lab`:

```sh
python3 nes/setup.py
make -C nes -j8 CROSS_COMPILE=../toolchains/rockbox-arm/bin/arm-elf-eabi-
```

Copy `iPod_Control/` from `nes/build/nes.zip` into the iPod root. Add your `.nes`
files to `iPod_Control/games_RO/reprise-nes/` or its subfolders. Up to 64 ROMs
are discovered automatically; none are included.

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
