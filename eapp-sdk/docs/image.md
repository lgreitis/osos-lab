# Image

`runtime/eapp.lds` links the image at the host's mapping, 1 MiB for code, data and bss, with the eApp header (`runtime/header.c`) first.

`runtime/imports.def` lists the host's import interfaces and their functions in the host's order. `runtime/imports.S` turns it into the import list: one jump stub per function, filled in by the host at load. The image holds no firmware address, so an unresolved symbol at link time is a call that bypasses the import list.

## Picture files

A picture (`.eimg`) is a 20-byte header, then the rows without padding.

| Word | |
|---|---|
| `EIMG` | |
| width, height | Pixels |
| format | 0 RGB565, 1 RGBA4444, 2 RGBA5551, 3 RGBA8888, 4 A8 |
| colour key | -1 for none |

Words are little-endian.

## Pack files

A pack (`.epak`) is `EPAK`, the entry count and an offset per entry, then the pictures with their headers, each 4-byte aligned.

## Module files

A module (`.emod`) is a 36-byte header, the module's text and data linked at 0 with the `EAPP_MODULE` table first, then the relocations.

| Word | |
|---|---|
| `EMOD` | |
| version | 1 |
| build id | 16 bytes: the image's GNU build id |
| text and data | Bytes, a multiple of 4 |
| bss | Bytes, cleared at load |
| relocations | The count of offsets that follow |

Each relocation is the offset of a word that holds an address inside the module, in ascending order; the loader adds the block's address to it. Words that hold the image's addresses stay as linked. `runtime/eapp.lds` keeps the image's build id note right after the eApp header, and the loader refuses a module whose build id differs.
