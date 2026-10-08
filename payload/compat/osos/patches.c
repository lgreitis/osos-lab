/* SPDX-License-Identifier: GPL-3.0-only */

#include "patch.h"
#include <osos-target.h>
#include <patch-sites.h>

/* Custom EQ */
PATCH_CALL(OSOS_PREFERENCES_LOAD_CALL, OSOS_PREFERENCES_LOAD, cfw_preferences_hook);
PATCH_POINTER(OSOS_SETTINGS_ACTION_SLOT, OSOS_SETTINGS_ACTION, cfw_settings_action);
PATCH_POINTER(OSOS_SETTINGS_SECONDARY_ACTION_SLOT, OSOS_SETTINGS_ACTION,
              cfw_settings_action);
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
/* cmp r1, #23 -> cmp r1, #24 */
PATCH_WORD(OSOS_EQ_DSP_COUNT_WORD, 0xe3510017, 0xe3510018);

/* Homebrew */
PATCH_CALL(OSOS_GAME_MANIFEST_INIT_CALL, OSOS_GAME_MANIFEST_READER_INIT,
           cfw_game_manifest_reader);

/* Reserve the payload above native BSS and stacks. */
PATCH_POINTER(OSOS_HEAP_REGION_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);
PATCH_POINTER(OSOS_RUNTIME_HEAP_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);

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
/* Retain the Album Artist MHOD that the native importer discards. */
PATCH_CALL(OSOS_ITUNES_IMPORT_TRACKS_CALL, OSOS_ITUNES_IMPORT_TRACKS,
           cfw_album_artist_import_tracks);
PATCH_CALL_WORD(OSOS_ITUNES_ALBUM_ARTIST_SLOT, OSOS_ITUNES_ALBUM_ARTIST_DISCARD_WORD,
                cfw_album_artist_import_hook);

/* Resources */
PATCH_CALL(OSOS_RESOURCE_INIT_CALL, OSOS_RESOURCE_BANK_INIT_OVERRIDES,
           cfw_resource_hook);
PATCH_CALL(OSOS_RESOURCE_NEXT_TEMPLATE_CALL, OSOS_RESOURCE_NEXT, cfw_next_template);

/* Report the non-European volume policy without changing SysInfo. */
/* and r0, r0, #1 -> mov r0, #0 */
PATCH_WORD(OSOS_REGIONAL_VOLUME_RESULT_WORD, 0xe2000001, 0xe3a00000);
