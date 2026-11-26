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

PATCH_CALL(OSOS_DISK_MODE_RELOAD_CALL, OSOS_MEDIA_DATABASE_RELOAD,
           cfw_disk_mode_reload);

/* Shared Settings hooks */
PATCH_POINTER(OSOS_SETTINGS_PROPERTY_SLOT, OSOS_SETTINGS_PROPERTY,
              cfw_settings_property);
PATCH_CALL(OSOS_PREFERENCES_LOAD_CALL, OSOS_PREFERENCES_LOAD, cfw_preferences_hook);
PATCH_CALL(OSOS_PREFERENCES_FLUSH_CALL, OSOS_PREFERENCES_FLUSH,
           cfw_preferences_flush_hook);
PATCH_CALL(OSOS_SLEEP_CALL, OSOS_SLEEP, cfw_sleep_hook);
PATCH_CALL(OSOS_SLEEP_REQUEST_CALL, OSOS_SLEEP, cfw_sleep_hook);
PATCH_CALL(OSOS_RESET_ALL_SETTINGS_CALL, OSOS_RESET_ALL_SETTINGS, cfw_settings_reset);
PATCH_POINTER_PAIR(OSOS_SETTINGS_ACTION_SLOT, OSOS_SETTINGS_SECONDARY_ACTION_SLOT,
                   OSOS_SETTINGS_ACTION, cfw_settings_action);

/* Homebrew */
PATCH_CALL(OSOS_GAME_MANIFEST_INIT_CALL, OSOS_GAME_MANIFEST_READER_INIT,
           cfw_game_manifest_reader);

/* Reserve the payload above native BSS and stacks. */
PATCH_POINTER(OSOS_HEAP_REGION_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);
PATCH_POINTER(OSOS_RUNTIME_HEAP_BASE_LITERAL, OSOS_NATIVE_HEAP_BASE, __payload_limit);

/* Panic */
PATCH_JUMP(OSOS_PANIC_ENTRY, 0xe59f1010, 0xe3a00004, cfw_panic_entry);
PATCH_JUMP(OSOS_ABORT_ENTRY, 0xe3a01000, 0xe92d4010, cfw_abort_entry);

/* Main and Music menu visibility */
PATCH_POINTER(OSOS_SETTINGS_INDEXED_PROPERTY_SLOT, OSOS_SETTINGS_INDEXED_PROPERTY,
              cfw_settings_indexed_property);
PATCH_CALL_WORD(OSOS_MAIN_MENU_INDEX_CALL, 0xe12fff31, cfw_ui_main_menu_index);
PATCH_CALL_WORD(OSOS_MUSIC_MENU_INDEX_CALL, 0xe12fff31, cfw_ui_music_menu_index);
PATCH_CALL(OSOS_MAIN_MENU_TOGGLE_CALL, OSOS_MAIN_MENU_TOGGLE, cfw_ui_main_menu_toggle);
PATCH_CALL(OSOS_MUSIC_MENU_TOGGLE_CALL, OSOS_MUSIC_MENU_TOGGLE,
           cfw_ui_music_menu_toggle);

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

/* Movies and TV episodes have no native contextual menu. */
PATCH_POINTER_PAIR(OSOS_MOVIE_MENU_SLOT, OSOS_MOVIE_SECONDARY_MENU_SLOT,
                   OSOS_LIST_NO_CONTEXT_MENU, cfw_movie_menu);
PATCH_POINTER_PAIR(OSOS_TV_EPISODE_MENU_SLOT, OSOS_TV_EPISODE_SECONDARY_MENU_SLOT,
                   OSOS_LIST_NO_CONTEXT_MENU, cfw_tv_episode_menu);
