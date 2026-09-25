# osos game SDK

C11 runtime for **Classic 7G Rev B / FW 2.0.4** with RepriseOS's homebrew patch.
See [Doom](../doom/) for a build and packaging example.

## App contract

Define `game_application` from [`game_api.h`](include/game_api.h): a `package_id`
under `iPod_Control/games_RO/` and optional `init`, `input`, `frame`, `shutdown`
callbacks. The API provides RGB565 frames, buttons, wheel, Hold, heap allocation,
timing, and read-only files. Poll the timer at least once every ~71 minutes.

## Build

Set `.DEFAULT_GOAL`, `CC`, `AR`, and `GAME_SDK`, then include `runtime.mk`.
Link `GAME_STARTUP`, `GAME_RUNTIME`, and `libgcc` with `GAME_LINKER_SCRIPT` and
`GAME_ARCH_FLAGS`. Code and BSS must fit in 1 MiB at `0x18000000`.

For newlib, compile `runtime/newlib_glue.c` and implement the hooks in
[`game_libc.h`](include/game_libc.h). The exit hook must unwind to the app callback.
