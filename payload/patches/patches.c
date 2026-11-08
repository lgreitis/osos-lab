/* SPDX-License-Identifier: GPL-3.0-only */

#include "patch.h"
#include "native/bindings.h"
#include "patches/sites.h"

/* Dark-mode software colors and manifest-selected bitmap artwork. */
PATCH_CALL(OSOS_GRAPHICS_FILL_RECT_CALL, OSOS_SURFACE_FILL_RECT, cfw_dark_mode_fill);
PATCH_CALL(OSOS_TEXT_DRAW_COLOR_CALL, OSOS_GRAPHICS_SET_COLOR,
           cfw_dark_mode_text_color_hook);
PATCH_CALL(OSOS_MARQUEE_DRAW_COLOR_CALL, OSOS_GRAPHICS_SET_COLOR,
           cfw_dark_mode_marquee_color_hook);
PATCH_CALL(OSOS_COVER_FLOW_CLEAR_COLOR_CALL, OSOS_GLES_CLEAR_COLOR_X,
           cfw_dark_mode_clear_color);
PATCH_CALL(OSOS_ALBUM_ART_CLEAR_COLOR_CALL, OSOS_GLES_CLEAR_COLOR_X,
           cfw_dark_mode_clear_color);
PATCH_CALL(OSOS_ARTWORK_REFLECTION_BLEND_CALL, OSOS_RGB565_BLEND_FIXED,
           cfw_dark_mode_reflection_blend);
PATCH_CALL(OSOS_VIEW_BACKGROUND_FILL_CALL, OSOS_GRAPHICS_FILL_RECT,
           cfw_dark_mode_background_fill_hook);

/* Shared Settings hooks */
PATCH_POINTER(OSOS_SETTINGS_PROPERTY_SLOT, OSOS_SETTINGS_PROPERTY,
              cfw_settings_property);
PATCH_CALL(OSOS_PREFERENCES_LOAD_CALL, OSOS_PREFERENCES_LOAD, cfw_preferences_hook);
PATCH_POINTER(OSOS_SETTINGS_ACTION_SLOT, OSOS_SETTINGS_ACTION, cfw_settings_action);
PATCH_POINTER(OSOS_SETTINGS_SECONDARY_ACTION_SLOT, OSOS_SETTINGS_ACTION,
              cfw_settings_action);

/* Custom EQ */
/* Original entry: sub r0, r0, #100; cmp r0, #21. */
PATCH_JUMP(OSOS_EQ_MAP_ENTRY, 0xe2400064, 0xe3500015, cfw_eq_map_hook);
/* Replace cmp r0, #107 with a hook that also recognizes Custom (122). */
PATCH_CALL_WORD(OSOS_EQ_TRACK_GUARD_WORD, 0xe350006b, cfw_eq_track_guard);
PATCH_POINTER(OSOS_EQ_MENU_IDS_LITERAL, OSOS_EQ_PRESET_IDS, cfw_eq_preset_ids);
PATCH_POINTER(OSOS_EQ_SETTINGS_IDS_LITERAL, OSOS_EQ_PRESET_IDS, cfw_eq_preset_ids);
PATCH_POINTER(OSOS_EQ_PREVIEW_TABLE_LITERAL, OSOS_EQ_PRESETS, cfw_eq_presets);
PATCH_POINTER(OSOS_EQ_NAME_TABLE_LITERAL, OSOS_EQ_PRESETS, cfw_eq_presets);
PATCH_POINTER(OSOS_EQ_RESOURCE_TABLE_LITERAL, OSOS_EQ_PRESETS, cfw_eq_presets);
/* cmp r2, #23 -> cmp r2, #24 */
PATCH_WORD(OSOS_EQ_SETTINGS_COUNT_WORD, 0xe3520017, 0xe3520018);
/* cmp r1, #23 -> cmp r1, #24 */
PATCH_WORD(OSOS_EQ_PREVIEW_COUNT_WORD, 0xe3510017, 0xe3510018);
/* cmp r1, #23 -> cmp r1, #24 */
PATCH_WORD(OSOS_EQ_NAME_COUNT_WORD, 0xe3510017, 0xe3510018);
/* mov r0, #23 -> mov r0, #24 */
PATCH_WORD(OSOS_EQ_PROVIDER_COUNT_WORD, 0xe3a00017, 0xe3a00018);
/* cmp r5, #23 -> cmp r5, #24 */
PATCH_WORD(OSOS_EQ_RESOURCE_COUNT_WORD, 0xe3550017, 0xe3550018);

/* EQ audio */
PATCH_CALL(OSOS_EQ_SET_PRESET_LOAD_CALL, OSOS_EQ_LOAD, cfw_eq_load);
PATCH_CALL(OSOS_EQ_SET_RATE_LOAD_CALL, OSOS_EQ_LOAD, cfw_eq_load);
PATCH_POINTER(OSOS_EQ_PROCESS_SLOT, OSOS_EQ_PROCESS, cfw_eq_process);
PATCH_POINTER(OSOS_EQ_RESET_SLOT, OSOS_EQ_RESET, cfw_eq_reset);
/* cmp r1, #23 -> cmp r1, #24 */
PATCH_WORD(OSOS_EQ_DSP_COUNT_WORD, 0xe3510017, 0xe3510018);

/* Homebrew */
PATCH_CALL(OSOS_GAME_MANIFEST_INIT_CALL, OSOS_GAME_MANIFEST_READER_INIT,
           cfw_game_manifest_reader);

/* Reserve the payload above native BSS and stacks. */
PATCH_POINTER(OSOS_HEAP_REGION_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);
PATCH_POINTER(OSOS_RUNTIME_HEAP_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);

/* Block-cache size: replace max(r0 - r1 - 14 MiB, 14 MiB) with the CFW budget. */
/* sub r0, r0, r1 -> bl cfw_block_cache_budget */
PATCH_CALL_WORD(OSOS_BLOCK_CACHE_IMPORT_COST_WORD, 0xe0400001, cfw_block_cache_budget);
/* sub r0, r0, #0xe00000; cmp r0, #0xe00000; movcc r0, #0xe00000 -> nop */
PATCH_WORD(OSOS_BLOCK_CACHE_RESERVE_NATIVE_WORD, 0xe240060e, 0xe1a00000);
PATCH_WORD(OSOS_BLOCK_CACHE_FLOOR_TEST_WORD, 0xe350060e, 0xe1a00000);
PATCH_WORD(OSOS_BLOCK_CACHE_FLOOR_SET_WORD, 0x33a0060e, 0xe1a00000);
/* Media-player read-ahead: replace max(r0, 6 MiB) with the CFW size. */
/* cmp r0, #0x600000 -> bl cfw_media_player_buffer; movlt r0, #0x600000 -> nop */
PATCH_CALL_WORD(OSOS_MEDIA_PLAYER_BUFFER_MIN_TEST_WORD, 0xe3500606,
                cfw_media_player_buffer);
PATCH_WORD(OSOS_MEDIA_PLAYER_BUFFER_MIN_SET_WORD, 0xb3a00606, 0xe1a00000);
/* Grow the block cache for a game's blocks and shrink it back when the game exits. */
PATCH_CALL(OSOS_EAPP_BLOCKS_ACQUIRE_CALL, OSOS_EAPP_BLOCKS_ACQUIRE,
           cfw_game_blocks_acquire);
PATCH_CALL(OSOS_EAPP_BLOCKS_RELEASE_CALL, OSOS_EAPP_BLOCKS_RELEASE,
           cfw_game_blocks_release);
/* Pool slots: round to 8 instead of 16 bytes (36-byte artwork thumbnails use 40). */
/* add r0, r4, #0xf; bic r0, r0, #0xf -> add r0, r4, #7; bic r0, r0, #7 */
PATCH_WORD(OSOS_POOL_SLOT_ROUND_ADD_WORD, 0xe284000f, 0xe2840007);
PATCH_WORD(OSOS_POOL_SLOT_ROUND_MASK_WORD, 0xe3c0000f, 0xe3c00007);
/* UI task stack: 32 KiB instead of 128 KiB. mov r1, r0, lsl #4 -> lsl #2 */
PATCH_WORD(OSOS_UI_TASK_STACK_SHIFT_WORD, 0xe1a01200, 0xe1a01100);

/* Panic */
PATCH_JUMP(OSOS_PANIC_ENTRY, 0xe59f1010, 0xe3a00004, cfw_panic_entry);
PATCH_JUMP(OSOS_ABORT_ENTRY, 0xe3a01000, 0xe92d4010, cfw_abort_entry);

/* Song context actions */
PATCH_CALL(OSOS_PLAYLIST_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_song_menu_cancel);
PATCH_CALL(OSOS_SONG_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_song_menu_cancel);
PATCH_CALL(OSOS_GENIUS_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_song_menu_cancel);
PATCH_JUMP(OSOS_PLAYLIST_ACTION_ENTRY, 0xe92d41f0, 0xe1a07001, cfw_playlist_action);
PATCH_JUMP(OSOS_SONG_ACTION_ENTRY, 0xe92d41f0, 0xe1a07001, cfw_song_action);
PATCH_JUMP(OSOS_GENIUS_ACTION_ENTRY, 0xe92d41f0, 0xe1a07001, cfw_genius_action);

PATCH_CALL(OSOS_NOW_PLAYING_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_now_playing_menu_cancel);
PATCH_POINTER(OSOS_NOW_PLAYING_ACTION_SLOT, OSOS_NOW_PLAYING_ACTION,
              cfw_now_playing_action);
PATCH_POINTER(OSOS_NOW_PLAYING_SECONDARY_ACTION_SLOT, OSOS_NOW_PLAYING_ACTION,
              cfw_now_playing_action);
PATCH_CALL(OSOS_TRACKDATA_DESTROY_STORAGE_CALL, OSOS_TRACKDATA_DESTROY_STORAGE,
           cfw_album_artist_destroy_tracks);
/* Share identical artwork between ArtworkDB images and drop the duplicates. */
PATCH_CALL(OSOS_ARTWORK_IMPORT_CALL, OSOS_ARTWORK_IMPORT, cfw_artwork_import);
PATCH_CALL(OSOS_ARTWORK_IMAGE_THUMBS_CALL, OSOS_ARTWORK_IMAGE_THUMBS,
           cfw_artwork_image_thumbs);
PATCH_CALL(OSOS_ARTWORK_ALBUM_IMAGE_CALL, OSOS_ARTWORK_FIND_IMAGE,
           cfw_artwork_album_image);
PATCH_CALL(OSOS_ARTWORK_TRACK_DBID_CALL, OSOS_ARTWORK_FIND_IMAGE_BY_DBID,
           cfw_artwork_image_by_dbid);
PATCH_CALL(OSOS_ARTWORK_PLAYING_DBID_CALL, OSOS_ARTWORK_FIND_IMAGE_BY_DBID,
           cfw_artwork_image_by_dbid);
PATCH_CALL_WORD(OSOS_ITUNES_TRACK_ARTWORK_ID_WORD, 0xe58400f0, cfw_artwork_track_hook);
/* Retain Album Artist and sort names that the native importer discards. */
PATCH_CALL(OSOS_ITUNES_IMPORT_TRACKS_CALL, OSOS_ITUNES_IMPORT_TRACKS,
           cfw_album_artist_import_tracks);
PATCH_CALL_WORD(OSOS_ITUNES_ALBUM_ARTIST_SLOT, OSOS_ITUNES_ALBUM_ARTIST_DISCARD_WORD,
                cfw_album_artist_import_hook);
PATCH_CALL_WORD(OSOS_ITUNES_SORT_ARTIST_SLOT, OSOS_ITUNES_SORT_ARTIST_DISCARD_WORD,
                cfw_album_artist_import_hook);
PATCH_CALL_WORD(OSOS_ITUNES_SORT_ALBUM_ARTIST_SLOT,
                OSOS_ITUNES_SORT_ALBUM_ARTIST_DISCARD_WORD,
                cfw_album_artist_import_hook);

PATCH_POINTER(OSOS_MUSIC_ACTION_SLOT, OSOS_MUSIC_ACTION, cfw_album_artists_action);
PATCH_POINTER(OSOS_MUSIC_SECONDARY_ACTION_SLOT, OSOS_MUSIC_ACTION,
              cfw_album_artists_action);
PATCH_POINTER(OSOS_MAIN_ACTION_SLOT, OSOS_MUSIC_ACTION, cfw_album_artists_action);
PATCH_POINTER(OSOS_MAIN_SECONDARY_ACTION_SLOT, OSOS_MUSIC_ACTION,
              cfw_album_artists_action);
PATCH_JUMP(OSOS_ARTISTS_BUILD_ENTRY, 0xe92d4038, OSOS_ARTISTS_BUILD_LOAD_WORD,
           cfw_album_artists_build);
PATCH_JUMP(OSOS_ARTIST_NAME_ENTRY, 0xe92d41f0, OSOS_ARTIST_NAME_SAVE_WORD,
           cfw_album_artist_name);
PATCH_JUMP(OSOS_ARTIST_LETTER_ENTRY, 0xe92d41f0, 0xe1a07000, cfw_album_artist_letter);
PATCH_CALL(OSOS_ARTIST_FILTER_COMPARE_CALL, OSOS_TEXT_COMPARE,
           cfw_album_artist_filter_hook);
PATCH_POINTER(OSOS_ALBUM_GROUP_LITERAL, OSOS_BROWSER_ALBUM_GROUP,
              cfw_album_artists_album_group);
PATCH_POINTER(OSOS_ALBUM_SUBTITLE_SLOT, OSOS_MODEL_ALBUM_SUBTITLE,
              cfw_album_artists_album_subtitle);
PATCH_CALL(OSOS_SELECT_ARTWORK_SOURCE_CALL, OSOS_SELECT_ARTWORK_SOURCE,
           cfw_album_artists_select_artwork);
PATCH_CALL(OSOS_ALBUM_ARTWORK_CREATE_CALL, OSOS_ALBUM_ARTWORK_CREATE,
           cfw_album_artists_create_artwork);
PATCH_JUMP(OSOS_MODEL_DESTROY_ENTRY, 0xe92d4010, 0xe1a04000,
           cfw_album_artists_destroy_model);
PATCH_POINTER(OSOS_SETTINGS_INDEXED_PROPERTY_SLOT, OSOS_SETTINGS_INDEXED_PROPERTY,
              cfw_settings_indexed_property);
PATCH_CALL_WORD(OSOS_MAIN_MENU_INDEX_CALL, 0xe12fff31, cfw_ui_main_menu_index);
PATCH_CALL_WORD(OSOS_MUSIC_MENU_INDEX_CALL, 0xe12fff31, cfw_ui_music_menu_index);
PATCH_CALL(OSOS_MAIN_MENU_TOGGLE_CALL, OSOS_MAIN_MENU_TOGGLE, cfw_ui_main_menu_toggle);
PATCH_CALL(OSOS_MUSIC_MENU_TOGGLE_CALL, OSOS_MUSIC_MENU_TOGGLE,
           cfw_ui_music_menu_toggle);
PATCH_CALL(OSOS_MUSIC_MENU_APPLY_CALL, OSOS_MUSIC_MENU_APPLY, cfw_ui_visibility_load);
PATCH_JUMP(OSOS_MAIN_MENU_RESET_ENTRY, 0xe92d47f0, 0xe1a09000, cfw_ui_main_menu_reset);
PATCH_JUMP(OSOS_MUSIC_MENU_RESET_ENTRY, 0xe92d47f0, 0xe1a08000,
           cfw_ui_music_menu_reset);

/* Resources */
PATCH_CALL(OSOS_RESOURCE_BLOCK_APPEND_CALL, OSOS_RESOURCE_BLOCK_APPEND,
           cfw_ui_append_resource_block);
PATCH_CALL(OSOS_UI_LOAD_EVENTS_CALL, OSOS_UI_LOAD_LAYOUT_EVENTS, cfw_ui_load_events);
PATCH_CALL(OSOS_UI_TEMPLATE_EVENTS_GET_CALL, OSOS_RESOURCE_GET, cfw_ui_event_resource);
PATCH_CALL(OSOS_UI_LAYOUT_EVENTS_GET_CALL, OSOS_RESOURCE_GET, cfw_ui_event_resource);
PATCH_CALL(OSOS_UI_CONTROLLER_EVENTS_GET_CALL, OSOS_RESOURCE_GET,
           cfw_ui_event_resource);
PATCH_CALL(OSOS_RESOURCE_INIT_CALL, OSOS_RESOURCE_BANK_INIT_OVERRIDES,
           cfw_resource_hook);
PATCH_CALL(OSOS_RESOURCE_NEXT_TEMPLATE_CALL, OSOS_RESOURCE_NEXT, cfw_next_template);

/* Report the non-European volume policy without changing SysInfo. */
/* and r0, r0, #1 -> mov r0, #0 */
PATCH_WORD(OSOS_REGIONAL_VOLUME_RESULT_WORD, 0xe2000001, 0xe3a00000);

/* Play Next and Play Last for albums and artists */
PATCH_CALL(OSOS_ALBUMS_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_selection_menu_cancel);
PATCH_CALL(OSOS_ARTISTS_CANCEL_APPEND_CALL, OSOS_CONTEXT_MENU_APPEND,
           cfw_selection_menu_cancel);
/* Albums, Artists and other library lists share this dispatcher. */
PATCH_JUMP(OSOS_MEDIA_LIST_ACTION_ENTRY, 0xe92d41f0, 0xe1a07001, cfw_media_list_action);
PATCH_CALL(OSOS_MEDIA_LIST_OTG_ADD_CALL, OSOS_OTG_ADD_BROWSER, cfw_queue_selection);

/* iAP accessory authentication bypass: force both device-signature verifiers to
 * report success so accessories authenticate without a Made-for-iPod coprocessor. */
/* stmdb sp!,{r4-r9,lr}; sub sp,sp,#0x6c */
PATCH_JUMP(OSOS_IAP_AUTH_VERIFY_A, 0xe92d43f0, 0xe24dd06c, cfw_iap_auth_verify);
/* stmdb sp!,{r2,r3,r4,lr}; cmp r1,#0 */
PATCH_JUMP(OSOS_IAP_AUTH_VERIFY_B, 0xe92d401c, 0xe3510000, cfw_iap_auth_verify);
