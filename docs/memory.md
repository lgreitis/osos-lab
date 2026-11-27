# Memory

The runtime adjusts Apple's memory policy at boot, before the extensions start.
`payload/rvx/memory.c` and `payload/rvx/artwork.c` have the reasoning and
numbers; `payload/rvx/storage.c` tells flash adapters from Apple's hard disks by
the ATA IDENTIFY data.

| Rule | What it does |
| --- | --- |
| Cache budget | Sizes the block cache after the library import as max(heap total - import cost - reserve, floor): 14/14 MiB on a hard disk as Apple does, 24/6 MiB on flash. Also releases the four sorted orders the import builds and never reads, 4 bytes per track each |
| Read-ahead floor | Below a 14 MiB cache the media player's read-ahead minimum becomes max(cache - 8 MiB, 2 MiB), so a game's 8 MiB still fits beside it |
| Pool slots | Fixed-pool slots round to 8 bytes instead of 16; ArtworkDB thumbnail records take 40 bytes instead of 48 |
| Game blocks | When the eApp block manager asks for a game's blocks, the cache first grows so the request fits beside what the other clients keep, as far as the heap allows with 1 MiB left; after the release the unassigned blocks above the import size go back to the heap |
| Position scan | List positions come from scanning the order instead of Apple's address index beside it, 8 bytes per entry and as large as the library for a play-all queue |
| Artwork sharing | While the ArtworkDB loads, an image whose thumbnail chain matches one already loaded shares that chain and its thumbnails and image record go back to their pools; tracks that named the dropped image get the first image's id |

Separately, sixty constant words written by the recipe (`payload/trackcount.c`)
widen the media-list node's track count from a halfword at `+0x2e` to a word at
`+0x2c` and move the container count to the halfword at `+0x1e`, so libraries over
65,535 tracks import. The symbols header keeps Apple's layout: code reads the
track count as a word at `MediaEntry_groups`.

## Measurements

Emulator, 64 MB model, 54.3 MB main heap, a synthetic library of 20,000 tracks
with one ArtworkDB image per track as iTunes writes them, read while the first
song plays:

| | Without the rules | Flash | Hard disk |
| --- | --- | --- | --- |
| Free heap | 5.9 MB | 16.8 MB | 6.2 MB |
| Block cache | 22.5 MiB | 17.5 MiB | 27.5 MiB |

The import itself costs about 5 MB less, which the hard-disk run hands to the
cache as Apple's rule does. Apple's firmware stops playing at about 35,000 such
tracks, when the import and the 14 MiB cache floor leave the UI no heap.

A 50,000-track library with artwork plays on flash with the cache at its 6 MiB
floor and 11.0 MB of heap free; starting a homebrew game grows the cache to
10 MiB and quitting it brings the cache back to 6 MiB. A 70,000-track library
without artwork imports and plays with 1.8 MB free; without the track-count
words the import aborts with a data abort.
