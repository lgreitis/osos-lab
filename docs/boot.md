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
        Companion->>Companion: Load OSOS and copy the 960 KiB payload reservation into place
        Companion->>OS: Enter OSOS with Apple's boot context
        OS->>OS: Native code calls CFW through patched hooks
    end
```

The companion adapts Apple's startup and image-loading path to the decrypted
firmware. Its Apple code patches live in RAM. Compatible companion, OSOS, and
bootloader builds share the file-loading and memory interface.

## Current memory layout

The payload is linked at `CFW_PAYLOAD_BASE` (`0x08b3c000`, above Apple's BSS) and
may use at most `CFW_PAYLOAD_BYTES` (960 KiB). Each build appends only its linked
size, rounded to 32 bytes, to the OSOS image; the companion copies those bytes into
place. The OSOS patch moves the native heap start from `0x08b3b1b0` to the end of
the linked payload, so every byte the payload links is taken from the heap.
`CFW_PAYLOAD_BUDGET` (132 KiB) caps the linked size well below that maximum; the
link fails when the payload exceeds it, so raising it is an explicit change.

Native driver initialization matters: Rockbox disk reads do not establish
that Apple's ATA and OSOS state are initialized. The companion resumes DXE
dispatch, then intercepts the transition to Bds selection. See [NOR drivers](nor.md)
and [OSOS relocation](osos.md).
