# RepriseOS game SDK

C11 runtime for the Apple 2.0.5 base. See [Doom](../doom/) for a newlib port and
[NES](../nes/) for a freestanding port.

## App and build

Define `game_application` from [game_api.h](include/game_api.h) with a package ID
under `iPod_Control/games_RO/` and `init`, `input`, `frame`, `shutdown` callbacks.
The API provides RGB565 drawing, buttons, wheel, Hold, heap allocation, timing,
read-only files and directory iteration. Audio and writable files are not implemented.

Set `.DEFAULT_GOAL`, `CC`, `AR`, and `GAME_SDK`, then include `runtime.mk`.
Link `GAME_STARTUP`, `GAME_RUNTIME`, and `libgcc` with `GAME_LINKER_SCRIPT` and
`GAME_ARCH_FLAGS`. Code and BSS must fit in 1 MiB at `0x18000000`.

For newlib, compile `runtime/newlib_glue.c` and implement the hooks in
[game_libc.h](include/game_libc.h). The exit hook must unwind to the app callback.

## Menus and files

[game_ui.h](include/game_ui.h) provides text menus and a file picker:

```c
static struct game_file_picker picker;
game_file_picker_open(&picker, "NES", "", ".nes");
```

Use `""` for the package root or a subfolder such as `"roms"`. The picker searches
subfolders, ignores hidden paths and extension case, and sorts up to 64 matches.
Open returns `0` on success, `-2` for too many files, or `-1` for another error.

Pass input to `game_menu_input`, wheel samples to `game_menu_scroll`, and draw
with `game_menu_draw`. The selected path is `picker.menu.items[picker.menu.selected].value`.
Call `game_file_picker_close` when done; it frees the labels and paths.

For custom browsers, use `game_directory_open/read/close`. Entries contain a
relative filename and directory flag. Up to four iterators may be open; native
iteration reports unavailable folders like empty folders.
