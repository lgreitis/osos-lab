# OSOS runtime

Addresses are for Apple 2.0.4 on Classic 7G Rev B.

## Relocation and memory

OSOS has a `0x800`-byte IMG1 header. Startup copies the first `0xaed8` body bytes
into IRAM at `0x22000000`, clears its BSS, then moves main code/data down to
`0x08000000`. Main runtime begins at complete-file offset `0xb6d8`.

| Region | Address / mapping |
| --- | --- |
| Main OSOS runtime | `address = file_offset + 0x07ff4928` |
| IRAM startup/audio | `address = 0x22000000 + file_offset - 0x800`, within the startup region |
| Translation table | `0x2200c000`, 16 KiB |
| Imported SysInfo | `0x08a11cd8`, `0x120` bytes |

Startup maps low vectors into IRAM, identity-maps IRAM and cached DRAM, adds
uncached DRAM aliases and MMIO, then enables the MMU and caches. Main and IRAM
hooks require different file mappings. Zero-filled bytes do not establish safe
scratch space. The CFW reservation is described in [boot sequence](boot.md).

## Tasks, graphics and storage

`0x0805d558` creates and starts 21 task descriptors in two passes. GraphicsManager
enters through `0x08388570`, constructs its object at `0x0815ad68`, and runs the
message loop at `0x0815add8`. UI can execute before the startup task returns.

| Address | Storage behavior |
| --- | --- |
| `0x08278ba4` | Disk initialization path; calls `0x082b1db0(0, 0)`. |
| `0x083619b0` / `0x08361388` / `0x0836140c` | PATA power, reset/init and IDENTIFY. |
| `0x080ccca4` | ATA status polling; returns 3 when BSY and DRQ are clear. Caller `0x080b9c1c` maps this to 59. |
| `0x08058584` | Mounts resource volume 4 from firmware-directory entry `rsrc`. |
| `0x0817ab74` / `0x0826a824` | Font lookup; Pixo table at `0x08a7a254`, fallback under volume 4 `Resources/Fonts`. |

Bootloader and OSOS ATA code have different initialization and wait behavior.
Successful Rockbox reads alone do not validate the Apple storage handoff.

## Audio and volume

Native EQ runs three stereo biquads with signed Q24 coefficients and S16
saturation after each stage. Presets have separate 44.1/48 kHz coefficient banks.
`0x08271bf0` loads coefficients, `0x08271d88` processes one stereo biquad, and
`0x08271f2c` runs all three. Effects/output dispatch is at `0x0818c320`.
Post-effects Q15 gain cannot undo EQ clipping; precut must act before saturation.

Regional policy is tested at `0x0804b150`; ceiling getter: `0x081553c4`.
User volume caps are separate: setter `0x081552f4`, preference load `0x0815537c`,
applied-volume setter `0x08155568`. Ceilings 216 and 256 are internal units,
not percentages or dB.
