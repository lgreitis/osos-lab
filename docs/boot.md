# Boot sequence

The patched Rockbox bootloader selects the system at startup:

| Controls | System |
| --- | --- |
| No buttons | RepriseOS |
| Menu or Hold switch | Original Apple OS |
| Play/Pause | Rockbox |

## RepriseOS boot

```mermaid
sequenceDiagram
    actor User
    participant RB as Rockbox bootloader (NOR)
    participant Disk as iPod disk
    participant Companion as cfw-loader.bin (RAM)
    participant Apple as Apple startup and drivers
    participant OS as OSOS + CFW payload

    Note over User,RB: Power-on/reset reaches the installed bootloader
    alt Menu/Hold switch
        RB->>Apple: Enter original Apple boot path
        Note over Apple,OS: Boot original Apple OS
    else Play/Pause
        RB->>Disk: Load Rockbox
        Note over RB,Disk: Start Rockbox
    else No buttons
        RB->>Disk: Read /osos-cfw.bin and /cfw-loader.bin
        Disk-->>RB: Files staged in RAM
        RB->>Companion: Copy bootstrap to internal RAM and enter it
        Companion->>Apple: Run patched PreEfi startup
        Apple->>Apple: Initialize hardware and dispatch DXE drivers
        Apple->>Companion: Intercept handoff before normal boot selection
        Companion->>Companion: Present staged OSOS as a RAM-backed file to Bds
        Companion->>Companion: Load OSOS and copy the 1 MiB payload into place
        Companion->>OS: Enter OSOS with Apple's boot context
        OS->>OS: Native code calls CFW through patched hooks
    end
```

The companion adapts Apple's startup and image-loading path to the decrypted
firmware. Its Apple code patches live in RAM. Compatible companion, OSOS, and
bootloader builds share the file-loading and memory interface.

## Current memory layout

The payload occupies 1 MiB at `0x08b33000`. The OSOS patch moves the heap start
to `0x08c33000`, and the companion copies the appended payload into that reservation.

Source: [bootloader patch](../patches/rockbox/0001-cfw-file-boot.patch),
[Apple companion](../loader/apple/), [OSOS patcher](../patches/osos/patch_osos_payload.py),
[payload](../payload/).
