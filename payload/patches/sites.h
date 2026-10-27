/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define OSOS_PREFERENCES_LOAD_CALL 0x08058808
#define OSOS_SETTINGS_ACTION_SLOT 0x089a3bf4
#define OSOS_SETTINGS_SECONDARY_ACTION_SLOT 0x089a7c3c
#define OSOS_GRAPHICS_FILL_RECT_CALL 0x0825ae9c
#define OSOS_BITMAP_VIEW_LOAD_ENTRY 0x08100020
#define OSOS_TEXT_DRAW_COLOR_CALL 0x082872a8
#define OSOS_SEARCH_INPUT_COLOR_WORD 0x08833294
#define OSOS_MARQUEE_DRAW_COLOR_CALL 0x081a8f0c
#define OSOS_COVER_FLOW_CLEAR_COLOR_CALL 0x08138304
#define OSOS_ALBUM_ART_CLEAR_COLOR_CALL 0x08164230
#define OSOS_ARTWORK_REFLECTION_BLEND_CALL 0x0826a890
#define OSOS_LYRICS_BACKGROUND_COLOR_WORD 0x0882ab6c
#define OSOS_EQ_MAP_ENTRY 0x080b28a0
#define OSOS_EQ_TRACK_GUARD_WORD 0x08173b74
#define OSOS_EQ_MENU_IDS_LITERAL 0x0807bfb0
#define OSOS_EQ_SETTINGS_IDS_LITERAL 0x080bb4ac
#define OSOS_EQ_PREVIEW_TABLE_LITERAL 0x081e0fa8
#define OSOS_EQ_NAME_TABLE_LITERAL 0x081e0ff0
#define OSOS_EQ_RESOURCE_TABLE_LITERAL 0x082955dc
#define OSOS_EQ_SETTINGS_COUNT_WORD 0x080bb498
#define OSOS_EQ_PREVIEW_COUNT_WORD 0x081e0f74
#define OSOS_EQ_NAME_COUNT_WORD 0x081e0fc0
#define OSOS_EQ_PROVIDER_COUNT_WORD 0x081e1ae8
#define OSOS_EQ_RESOURCE_COUNT_WORD 0x08295420
#define OSOS_EQ_SET_PRESET_LOAD_CALL 0x082735dc
#define OSOS_EQ_SET_RATE_LOAD_CALL 0x08273610
#define OSOS_EQ_PROCESS_SLOT 0x089ae0e0
#define OSOS_EQ_DSP_COUNT_WORD 0x082735c8
#define OSOS_GAME_MANIFEST_INIT_CALL 0x080f9418
#define OSOS_HEAP_REGION_BASE_LITERAL 0x0804b478
#define OSOS_RUNTIME_HEAP_BASE_LITERAL 0x0807c048
#define OSOS_PANIC_ENTRY 0x0803930c
#define OSOS_ABORT_ENTRY 0x0802606c
#define OSOS_PLAYLIST_CANCEL_APPEND_CALL 0x0822ffb8
#define OSOS_SONG_CANCEL_APPEND_CALL 0x082195ec
#define OSOS_GENIUS_CANCEL_APPEND_CALL 0x0821d26c
#define OSOS_PLAYLIST_ACTION_ENTRY 0x0822ea1c
#define OSOS_SONG_ACTION_ENTRY 0x08218b24
#define OSOS_GENIUS_ACTION_ENTRY 0x0821c0e4
#define OSOS_RESOURCE_INIT_CALL 0x081c7ea8
#define OSOS_RESOURCE_NEXT_TEMPLATE_CALL 0x081af75c
#define OSOS_REGIONAL_VOLUME_RESULT_WORD 0x0804b188
#define OSOS_NATIVE_HEAP_BASE 0x08b3b1b0
#define OSOS_ITUNES_IMPORT_TRACKS_CALL 0x08058f1c
#define OSOS_ITUNES_ALBUM_ARTIST_SLOT 0x0808adcc
#define OSOS_ITUNES_ALBUM_ARTIST_DISCARD_WORD 0xea0000ec
#define OSOS_TRACKDATA_DESTROY_STORAGE_CALL 0x0803e650
#define OSOS_NOW_PLAYING_CANCEL_APPEND_CALL 0x0821526c
#define OSOS_NOW_PLAYING_ACTION_SLOT 0x0899be50
#define OSOS_NOW_PLAYING_SECONDARY_ACTION_SLOT 0x089a2e00

#define OSOS_MUSIC_ACTION_SLOT 0x089a940c
#define OSOS_MUSIC_SECONDARY_ACTION_SLOT 0x089a507c
#define OSOS_ARTISTS_BUILD_ENTRY 0x08048fe8
#define OSOS_ARTIST_NAME_ENTRY 0x08047b54
#define OSOS_ARTIST_LETTER_ENTRY 0x08047bc8
#define OSOS_ARTIST_FILTER_COMPARE_CALL 0x080d7bd8
#define OSOS_ALBUM_GROUP_LITERAL 0x08048fe4
#define OSOS_ALBUM_SUBTITLE_SLOT 0x0898cbf4
#define OSOS_SELECT_ARTWORK_SOURCE_CALL 0x08169e30
#define OSOS_ALBUM_ARTWORK_CREATE_CALL 0x08138c34
#define OSOS_MODEL_DESTROY_ENTRY 0x08133c30
#define OSOS_MAIN_ACTION_SLOT 0x089a4c9c
#define OSOS_MAIN_SECONDARY_ACTION_SLOT 0x089a8fa4
#define OSOS_SETTINGS_PROPERTY_SLOT 0x08997b60
#define OSOS_SETTINGS_INDEXED_PROPERTY_SLOT 0x08997b7c
#define OSOS_MAIN_MENU_INDEX_CALL 0x081d2214
#define OSOS_MUSIC_MENU_INDEX_CALL 0x081dd6dc
#define OSOS_MAIN_MENU_TOGGLE_CALL 0x081d225c
#define OSOS_MUSIC_MENU_TOGGLE_CALL 0x081dd724
#define OSOS_MUSIC_MENU_APPLY_CALL 0x081e3f48
#define OSOS_MAIN_MENU_RESET_ENTRY 0x081e1024
#define OSOS_MUSIC_MENU_RESET_ENTRY 0x081e1134

/* Play Next for albums and artists. */
#define OSOS_MEDIA_LIST_ACTION_ENTRY 0x0820f5a4
#define OSOS_MEDIA_LIST_OTG_ADD_CALL 0x0820f9cc
#define OSOS_ALBUMS_CANCEL_APPEND_CALL 0x0821c004
#define OSOS_ARTISTS_CANCEL_APPEND_CALL 0x0821fee8

/* iAP accessory authentication: device-signature verifier entries. */
#define OSOS_IAP_AUTH_VERIFY_A 0x0812dca8
#define OSOS_IAP_AUTH_VERIFY_B 0x081592fc

/* Rename Playlist: Playlists hold menu (TSilverMediaListCntlr_Playlists). */
#define OSOS_PLAYLISTS_BUILD_MENU 0x08229618
#define OSOS_PLAYLISTS_MENU_SLOT 0x089a96e4
/* Same builder via TSilverCntlrTransitionAddon<..._Playlists>. */
#define OSOS_PLAYLISTS_TRANSITION_MENU_SLOT 0x089a535c
#define OSOS_PLAYLISTS_CANCEL_APPEND_CALL 0x08229698
/* Playlists library: row -> playlist reference {vtable, list, ...}. */
#define OSOS_PLAYLISTS_GET 0x08169178
#define OSOS_PLAYLISTS_ROW_ITEM 0x081691a8
/* Set a playlist's name from a length-prefixed UTF-16 string. */
#define OSOS_PLAYLIST_SET_NAME 0x0805c3d8
/* Get a playlist's name (length-prefixed UTF-16); entry hooked for saved renames. */
#define OSOS_PLAYLIST_GET_NAME 0x08049624
#define OSOS_PLAYLIST_GET_NAME_RESUME 0x0804962c
/* 64-bit iTunesDB persistent ID (mhyp +0x1c) stored by the playlist importer. */
#define OSOS_PLAYLIST_PERSISTENT_ID 0x30
/* Search keyboard text: 'Str ' 0x7684 in the search model (lookup 0x7680). */
#define OSOS_SEARCH_MODEL_LOOKUP 0x081c7d44
#define OSOS_SEARCH_MODEL_ID 0x7680
#define OSOS_SEARCH_QUERY_ID 0x7684
/* Search model byte set while the keyboard should be restored on reopen. */
#define OSOS_SEARCH_MODEL_KEYBOARD_FLAG 0x18
#define OSOS_MODEL_RESOURCE_GET 0x08268b64
#define OSOS_MODEL_RESOURCE_SET 0x08268c28
#define OSOS_SEARCH_SET_QUERY_CALL 0x0812614c
/* TSearchCntlr construction and HandleMenuOnKeyboard (vtable +0x150). */
#define OSOS_SEARCH_CONSTRUCT 0x08126964
#define OSOS_SEARCH_CONSTRUCT_CALL 0x081261c0
#define OSOS_SEARCH_MENU_ON_KEYBOARD 0x08125dc8
#define OSOS_SEARCH_MENU_ON_KEYBOARD_SLOT 0x0898c034
#define OSOS_SEARCH_TRANSITION_MENU_ON_KEYBOARD_SLOT 0x0899d3e8
/* Controller navigation event (vtable +0x11c) and its CppString argument. */
#define OSOS_CONTROLLER_EMIT_EVENT 0x11c
#define OSOS_CPP_STRING_INIT 0x083cf82c
#define OSOS_CPP_STRING_DESTROY 0x083cf7d4

/* Add to Playlist. Library (re)load: FUN_0805d6c8(media database), called at boot
   and after a sync; both call sites are wrapped to re-apply saved additions. */
#define OSOS_DATABASE_LOAD 0x0805d6c8
#define OSOS_DATABASE_LOAD_BOOT_CALL 0x0804d7f0
#define OSOS_DATABASE_LOAD_RELOAD_CALL 0x081aec20
/* Media database: track data ('tdat') and the On-The-Go list, plus the byte the
   native On-The-Go add sets so the playlist is written back. */
#define OSOS_DATABASE_TRACK_DATA 0xf60
#define OSOS_DATABASE_OTG_LIST 0xf40
#define OSOS_DATABASE_OTG_STATE 0xf00
#define OSOS_OTG_STATE_DIRTY 0xb96
/* Track data list walk: first list ('tdat' +0x34) and the next-list routine. */
#define OSOS_TRACK_DATA_FIRST_LIST 0x34
#define OSOS_TRACK_DATA_NEXT_LIST 0x08048d18
#define OSOS_TRACK_DATA_MAGIC 0x74646174u /* 'tdat' */
#define OSOS_LIST_MAGIC 0x706c7374u       /* 'plst' */
/* 64-bit iTunesDB track persistent ID (mhit +0x70) in the song record. */
#define OSOS_RECORD_PERSISTENT_ID 0x110
/* List view model shared by media lists: highlighted row is vtable +0x168. */
#define OSOS_LIST_VIEW_MODEL 0x0817dde4
#define OSOS_LIST_VIEW_HIGHLIGHTED 0x168
/* List UI model: container view at +0x38; cast (vtable +0x14 via 0x0826c594) to
   the list view interface 0x1700 (TListView: select row r1, scroll if r2, at
   vtable +0x16c). */
#define OSOS_LIST_UI_CONTAINER 0x38
#define OSOS_INTERFACE_CAST 0x0826c594
#define OSOS_LIST_VIEW_INTERFACE 0x1700
#define OSOS_LIST_VIEW_SELECT_ROW 0x16c
/* TListView blink timer end: sends the blink's list event (0x08119f7c). */
#define OSOS_LIST_BLINK_DONE_CALL 0x0827f4e4
#define OSOS_VIEW_SEND_EVENT 0x08119f7c
/* Blink a list row (list view vtable +0x18c), as after an On-The-Go add. */
#define OSOS_LIST_FLASH_ROW 0x082896f4
/* List byte that makes the cached song counts (+0x61c, +0x620) recount. */
#define OSOS_LIST_COUNTS_STALE 0x618
/* Media-list controller: list row -> data row (vtable +0x130). */
#define OSOS_MEDIA_LIST_MAP_ROW 0x130
/* Library list flag (list +0x18c byte): set on the whole-library list. */
#define OSOS_LIST_KIND_FLAGS 0x18c
#define OSOS_LIST_KIND_LIBRARY 0x40

/* Remove from Playlist. Native playlist item delete ('dpli') and the
   playlist-changed notice ('cpld'), as used by the list reference remove-row
   routine 0x0827a8c8. */
#define OSOS_PLAYLIST_ITEM_DELETE 0x0803db30
#define OSOS_PLAYLIST_CHANGED 0x080564b8
/* Playlists > a playlist (TSilverMediaListCntlr_NestedPlaylists): hold menu
   builder (vtable +0x148, both vtables) and its browser model reference. */
#define OSOS_NESTED_PLAYLISTS_BUILD_MENU 0x0822fdb4
#define OSOS_NESTED_PLAYLISTS_MENU_SLOT 0x089aaba0
#define OSOS_NESTED_PLAYLISTS_TRANSITION_MENU_SLOT 0x089a64c8
#define OSOS_NESTED_PLAYLISTS_BROWSER 0xb4
#define OSOS_LIBRARY_MODEL_REF_GET 0x083ccd1c
/* Song list controller's own playlist reference, and the browser model method
   that makes a list reference current again (vtable +0x1ec). */
#define OSOS_NESTED_PLAYLISTS_LIST_REF 0xd4
#define OSOS_BROWSER_SET_CURRENT_LIST_REF 0x1ec
/* Playlists model refresh (vtable +0x9c). */
#define OSOS_PLAYLISTS_REFRESH 0x9c
/* Browser model: current list as a list reference (vtable +0x1f4). */
#define OSOS_BROWSER_CURRENT_LIST_REF 0x1f4
#define OSOS_LIST_REF_INIT 0x0827aae0
#define OSOS_LIST_REF_DESTROY 0x0827ab80

/* Cover Flow (TCoverFlowCntlr): action dispatcher (vtable +0x114, both vtables). */
#define OSOS_COVERFLOW_ACTION 0x08162928
#define OSOS_COVERFLOW_ACTION_SLOT 0x089905b0
#define OSOS_COVERFLOW_TRANSITION_ACTION_SLOT 0x0899e5f8
/* Controller +0xb0: the flow; flow +0xc0: album shown on the backside (-1 none). */
#define OSOS_COVERFLOW_FLOW 0xb0
#define OSOS_COVERFLOW_FLOW_ALBUM 0xc0
/* Backside track list: the Cover Flow model (0x08274e84) keeps the album's song
   browser at +0x158; browser vtable +0x22c builds the media item for a row, as
   the model's play-song method (0x08274c20) does. */
#define OSOS_COVERFLOW_MODEL_GET 0x08274e84
#define OSOS_COVERFLOW_MODEL_BROWSER 0x158
/* Cover Flow controller shown (vtable +0xec; r1 = first time). Entry hooked to
   re-list the album after the playlist chooser closes. */
#define OSOS_COVERFLOW_APPEAR 0x08162ba8
#define OSOS_COVERFLOW_APPEAR_RESUME 0x08162bb0
/* Cover Flow model: list an album on the back side (-1 clears), vtable +0xdc. */
#define OSOS_COVERFLOW_MODEL_SHOW_ALBUM 0xdc
#define OSOS_BROWSER_ITEM_AT 0x22c
#define OSOS_MEDIA_ITEM_DESTROY 0x08266da0
/* Hold menus: construct, show from a controller, destroy. */
#define OSOS_CONTEXT_MENU_INIT 0x081624e8
#define OSOS_CONTROLLER_SHOW_CONTEXT_MENU 0x08129d78
#define OSOS_CONTEXT_MENU_DESTROY 0x0816250c
