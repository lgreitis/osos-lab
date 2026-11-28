/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define OSOS_RESOURCE_BLOCK_APPEND_CALL 0x0817a88c

#define OSOS_DISK_MODE_RELOAD_CALL 0x081aec20
#define OSOS_PREFERENCES_LOAD_CALL 0x08058808
/* Native save-all; it runs only around USB disk mode. */
#define OSOS_PREFERENCES_FLUSH_CALL 0x0806392c
/* The only two calls to native light sleep, both dispatched from 0x0819aaf4; the
 * first is idle sleep. */
#define OSOS_SLEEP_CALL 0x0819b32c
#define OSOS_SLEEP_REQUEST_CALL 0x0819bba8
/* The Reset All Settings confirmation; the other caller, 0x080f3cb0, keeps the
 * native reset only. */
#define OSOS_RESET_ALL_SETTINGS_CALL 0x08212844
#define OSOS_SETTINGS_ACTION_SLOT 0x089a3bf4
#define OSOS_SETTINGS_SECONDARY_ACTION_SLOT 0x089a7c3c
#define OSOS_HEAP_REGION_BASE_LITERAL 0x0804b478
#define OSOS_RUNTIME_HEAP_BASE_LITERAL 0x0807c048
#define OSOS_PANIC_ENTRY 0x0803930c
#define OSOS_ABORT_ENTRY 0x0802606c
#define OSOS_RESOURCE_INIT_CALL 0x081c7ea8
#define OSOS_UI_LOAD_EVENTS_CALL 0x081d63f8
#define OSOS_NATIVE_HEAP_BASE 0x08b3b1b0

#define OSOS_SETTINGS_PROPERTY_SLOT 0x08997b60
#define OSOS_SETTINGS_INDEXED_PROPERTY_SLOT 0x08997b7c
#define OSOS_MAIN_MENU_INDEX_CALL 0x081d2214
#define OSOS_MUSIC_MENU_INDEX_CALL 0x081dd6dc
#define OSOS_MAIN_MENU_TOGGLE_CALL 0x081d225c
#define OSOS_MUSIC_MENU_TOGGLE_CALL 0x081dd724

/* Movie and TV episode lists: primary and secondary contextual-menu vtable slots. */
#define OSOS_MOVIE_MENU_SLOT 0x089a3ac8
#define OSOS_MOVIE_SECONDARY_MENU_SLOT 0x089a7b14
#define OSOS_TV_EPISODE_MENU_SLOT 0x089a5c94
#define OSOS_TV_EPISODE_SECONDARY_MENU_SLOT 0x089aa080
