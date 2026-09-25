# Doom for osos

For Classic 7G Rev B / FW 2.0.4 with RepriseOS's homebrew patch.
Uses [game-sdk](../game-sdk/) and DoomGeneric. No audio, saves, or config writes.

## Build and install

```sh
python3 doom/setup.py --cross-prefix /path/to/bin/arm-elf-eabi- --shareware
make -C doom -j4 CROSS_COMPILE=/path/to/bin/arm-elf-eabi-
```

From `osos-lab`; output: `doom/build/doom.zip` (override with `OUTPUT=...`).
Copy its `iPod_Control/` into the iPod root, keeping the `reprise-doom` directory.
Restart RepriseOS and open **Extras → Games → Doom**.

## Controls

| Input | Gameplay | Menu |
| --- | --- | --- |
| Menu | Forward + use | Up |
| Play/Pause | Fire | Down |
| Previous / Next | Turn left / right | Left / right |
| Center | Next weapon | Select / confirm |
| Wheel | Strafe | — |
| Hold on | Toggle menu; unlock to resume input | Toggle menu |
| Previous + Next, 1.5 seconds | Exit to osos | Exit to osos |
