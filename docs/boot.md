# Boot sequence

The patched Rockbox bootloader selects the system at startup:

| Controls | System |
| --- | --- |
| No buttons | RepriseOS |
| Menu or Hold switch | Original Apple OS |
| Play/Pause | Rockbox |

## RepriseOS boot

1. The NOR bootloader reads `/osos-cfw.bin` and `/cfw-loader.bin` into RAM.
2. The companion enters patched Apple PreEfi startup and DXE driver initialization.
3. Its handoff presents the staged OSOS through a RAM-backed file interface to Bds.
4. Bds loads OSOS; the companion places the CFW payload at its runtime address.
5. OSOS starts with Apple's hardware state and boot context. Patched hooks call the payload.

The companion adapts Apple's startup and image-loading path to the decrypted
firmware. Its Apple code patches live in RAM. Compatible companion, OSOS, and
bootloader builds share the file-loading and memory interface.

## Current memory layout

The payload occupies 1 MiB at `0x08b33000`. The OSOS patch moves the heap start
to `0x08c33000`, and the companion copies the appended payload into that reservation.

Source: [bootloader patch](../patches/rockbox/0001-cfw-file-boot.patch),
[Apple companion](../loader/apple/), [OSOS patcher](../patches/osos/patch_osos_payload.py),
[payload](../payload/).
