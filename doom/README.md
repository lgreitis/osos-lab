# Doom for RepriseOS

DoomGeneric port for the Apple 2.0.5 base. No audio, saves or config writes.

## Build and install

From `osos-lab`:

```sh
python3 doom/setup.py --cross-prefix toolchains/rockbox-arm/bin/arm-elf-eabi- --shareware
make -C doom -j8 CROSS_COMPILE=../toolchains/rockbox-arm/bin/arm-elf-eabi-
```

Copy `iPod_Control/` from `doom/build/doom.zip` into the iPod root. The package
includes the shareware episode. Add other Doom/Doom II IWADs to
`iPod_Control/games_RO/reprise-doom/` or its subfolders; up to 64 `.wad` files
are discovered automatically. Standalone add-on PWADs are not supported.

Open **Extras → Games → Doom** and choose a WAD.

## Controls

| Input | Gameplay | Menu |
| --- | --- | --- |
| Menu | Forward + use | Up |
| Play/Pause | Fire | Down |
| Previous / Next | Turn left / right | Left / right |
| Center | Next weapon | Select / confirm |
| Wheel | Strafe | — |
| Hold on | Toggle menu; unlock to resume input | Toggle menu |
| Previous + Next, 1.5 seconds | Exit | Exit |
