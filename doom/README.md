# Doom for RepriseOS

DoomGeneric port on the [eApp SDK](../eapp-sdk/README.md), with sound effects.
No music, saves or config writes.

## Build and install

From the repository root:

```sh
python3 doom/setup.py --cross-prefix toolchains/rockbox-arm/bin/arm-elf-eabi- --shareware
make -C doom -j8 CROSS_COMPILE=../toolchains/rockbox-arm/bin/arm-elf-eabi-
```

`setup.py` fetches DoomGeneric and newlib, builds newlib under `doom/vendor/`,
and with `--shareware` fetches the shareware episode, which the package then
includes. `doom/build/reprise-doom/` is the package. Copy it to
`/Reprise/Games/reprise-doom/` on the iPod, where the Homebrew extension lists
it. Put other Doom and Doom II IWADs into that folder or its subfolders; up to 64
`.wad` files are found. Standalone add-on PWADs are not supported.

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
