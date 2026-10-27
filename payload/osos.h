/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_OSOS_H
#define CFW_OSOS_H

#include "native/bindings.h"

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

/* Native action handlers receive this argument when queried for support. */
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef

/* A null signature skips signature verification when opening the manifest. */
static inline void osos_game_manifest_reader_init(void *reader, const char *manifest,
                                                  uint8_t mode, const char *signature)
{
    typedef void (*fn)(void *, const char *, uint8_t, const char *);
    ((fn)OSOS_GAME_MANIFEST_READER_INIT)(reader, manifest, mode, signature);
}

/* Apple's libstdc++ string stores one pointer and owns its backing storage. */
struct osos_string {
    char *data;
};

static inline void osos_string_init(struct osos_string *string, const char *text)
{
    typedef void (*fn)(struct osos_string *, const char *);
    ((fn)OSOS_STRING_INIT)(string, text);
}

static inline void osos_string_destroy(struct osos_string *string)
{
    typedef void (*fn)(struct osos_string *);
    ((fn)OSOS_STRING_DESTROY)(string);
}

/* Find or insert a name in the global resource-name map and return its ID slot. */
static inline uint32_t *osos_resource_name_slot(const struct osos_string *name)
{
    typedef uint32_t *(*fn)(void *, const struct osos_string *);
    return ((fn)OSOS_RESOURCE_NAME_SLOT)((void *)OSOS_RESOURCE_NAME_MAP, name);
}

/* Copy resource data into the bank's override map. */
static inline void osos_resource_set(void *bank, uint32_t type, uint32_t id,
                                     const void *data, uint32_t size)
{
    typedef void (*fn)(void *, uint32_t, uint32_t, const void *, uint32_t);
    ((fn)OSOS_RESOURCE_SET)(bank, type, id, data, size);
}

/* Advance an active resource enumeration; ROM banks enumerate the stock index. */
static inline const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{
    typedef const void *(*fn)(void *, uint32_t *, uint32_t *);
    return ((fn)OSOS_RESOURCE_NEXT)(bank, id, size);
}

static inline void *osos_resource_bank(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_RESOURCE_BANK)();
}

static inline void *osos_resource_get(void *bank, uint32_t type, uint32_t id,
                                      uint32_t *size)
{
    typedef void *(*fn)(void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)OSOS_RESOURCE_GET)(bank, type, id, size);
}

static inline void *osos_ui_resources(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_MENU_ITEM_CHANGED)();
}

static inline uintptr_t osos_ui_string(uint32_t id)
{
    typedef uintptr_t (*fn)(void *, uint32_t);
    return ((fn)OSOS_RESOURCE_STRING)(osos_ui_resources(), id);
}

static inline uintptr_t osos_ui_bitmap(uint32_t id)
{
    typedef uintptr_t (*fn)(void *, uint32_t);
    return ((fn)OSOS_RESOURCE_BITMAP)(osos_ui_resources(), id);
}

static inline uintptr_t osos_settings_on_off(int enabled)
{
    return osos_ui_string(enabled ? OSOS_SETTINGS_ON_STRING : OSOS_SETTINGS_OFF_STRING);
}

enum {
    OSOS_PROPERTY_STRING = 0x53747220, /* Str  */
    OSOS_PROPERTY_COUNT = 0x564d6178,  /* VMax */
    OSOS_NOTIFY_NONE = 0x4e6f6e65,     /* None */
    OSOS_NOTIFY_ALL = 0x2a2a2a2a,      /* **** */
};

static inline void osos_model_notify(void *model, uint32_t type, uint32_t property)
{
    typedef void (*fn)(void *, uint32_t, uint32_t);
    uintptr_t *vtable = *(uintptr_t **)model;
    ((fn)vtable[0x58 / 4])(model, type, property);
}

static inline void *osos_now_playing_model(void)
{
    typedef void *(*lookup_fn)(uint32_t);
    typedef void *(*interface_fn)(void *, uint32_t);
    void *service = ((lookup_fn)OSOS_UI_SERVICE_GET)(0x7f00);
    if (!service)
        return 0;
    uintptr_t *vtable = *(uintptr_t **)service;
    return ((interface_fn)vtable[0x14 / 4])(service, 5);
}

/* Notify native list providers that the item's displayed content changed. */
static inline void osos_menu_item_changed(uint32_t item)
{
    osos_model_notify(osos_ui_resources(), OSOS_NOTIFY_NONE, item);
}

static inline void *osos_settings_model(void *controller)
{
    return *(void **)((unsigned char *)controller + 0xb0);
}

static inline int osos_settings_property(void *model, uint32_t type, uint32_t property,
                                         uintptr_t *out)
{
    typedef int (*fn)(void *, uint32_t, uint32_t, uintptr_t *);
    return ((fn)OSOS_SETTINGS_PROPERTY)(model, type, property, out);
}

static inline int osos_settings_action(void *controller, const char *action,
                                       uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)OSOS_SETTINGS_ACTION)(controller, action, argument);
}

#include "native/graphics.h"

static inline void osos_eq_load(void *state)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_EQ_LOAD)(state);
}

static inline void osos_eq_reset(void *state)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_EQ_RESET)(state);
}

static inline void osos_eq_process(void *state, int16_t *samples, uint32_t frames,
                                   int16_t **output, uint32_t *output_frames)
{
    typedef void (*fn)(void *, int16_t *, uint32_t, int16_t **, uint32_t *);
    ((fn)OSOS_EQ_PROCESS)(state, samples, frames, output, output_frames);
}

/* Shared native layouts; media items are non-owning views. */
struct osos_media_list;

struct osos_media_entry {
    struct osos_media_list *list;
    struct osos_media_entry *parent, *next, *previous;
    uint32_t id, reserved[2];
    uint8_t flags[4];

    union {
        struct {
            struct osos_media_record *record;
            uint32_t shuffle_rank;
            struct osos_media_entry *next_record_entry;
        } song;

        struct {
            struct osos_media_entry *first, *last;
            uint32_t name;
        } group;
    };

    uint16_t groups, tracks;
};

struct osos_media_order {
    uint32_t reserved, capacity, bytes, count;
    struct osos_media_entry *entries[];
};

struct osos_media_record {
    uint8_t reserved[0x24];
    struct osos_media_entry *entries;
};

struct osos_media_item {
    uintptr_t *vtable;
    struct osos_media_entry *entry;
    struct osos_media_record *record;
    uint32_t reserved[3];
};

struct osos_playback_list {
    uintptr_t *vtable;
    struct osos_media_list *list;
    uint32_t reserved[3];
};

struct osos_player {
    uintptr_t *vtable;
    uint8_t reserved[0x50];
    void *current, *prepared;
    int current_index;
    struct osos_playback_list playback;
    uint8_t reserved_74[0x8d4];
    void *playback_source;
};

struct osos_context_menu;

_Static_assert(offsetof(struct osos_media_entry, tracks) == 0x2e, "Entry layout");
_Static_assert(offsetof(struct osos_media_entry, song.shuffle_rank) == 0x24,
               "Shuffle rank layout");
_Static_assert(offsetof(struct osos_media_order, entries) == 0x10, "Order layout");
_Static_assert(offsetof(struct osos_player, playback) == 0x60, "Player layout");
_Static_assert(offsetof(struct osos_player, playback_source) == 0x948,
               "Playback source layout");
_Static_assert(offsetof(struct osos_media_record, entries) == 0x24, "Record layout");
_Static_assert(offsetof(struct osos_media_item, record) == 8, "Media item record");
_Static_assert(sizeof(struct osos_media_item) == 24, "Media item layout");

static inline uintptr_t osos_method(void *object, unsigned int offset)
{
    return (*(uintptr_t **)object)[offset / 4];
}

static inline struct osos_player *osos_player_get(void)
{
    typedef struct osos_player *(*get_instance)(void);
    return ((get_instance)OSOS_PLAYER_GET)();
}

static inline void osos_player_lock(struct osos_player *player)
{
    ((void (*)(void *))OSOS_PLAYER_LOCK)((uint8_t *)player + 0xa4c);
}

static inline void osos_player_unlock(struct osos_player *player)
{
    ((void (*)(void *))OSOS_PLAYER_UNLOCK)((uint8_t *)player + 0xa4c);
}

static inline int osos_media_item_valid(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)OSOS_MEDIA_ITEM_VALID)(item);
}

/* Record-backed items omit entry; the record links its existing list entries. */
static inline struct osos_media_entry *
osos_media_item_entry(struct osos_media_item *item)
{
    if (item->entry)
        return item->entry;
    return item->record ? item->record->entries : NULL;
}

static inline int osos_media_item_has_record_flag_8f_01(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)OSOS_MEDIA_ITEM_HAS_RECORD_FLAG_8F_01)(item);
}

static inline int osos_media_item_has_kind_8(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)OSOS_MEDIA_ITEM_HAS_KIND_8)(item);
}

static inline int osos_media_item_has_kind_8062(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)OSOS_MEDIA_ITEM_HAS_KIND_8062)(item);
}

#include "native/media.h"

static inline void *osos_list_database(struct osos_media_list *list)
{
    return *(void **)((uint8_t *)list + 0x0c);
}

static inline struct osos_media_entry *osos_list_root(struct osos_media_list *list)
{
    return *(struct osos_media_entry **)((uint8_t *)list + 0x40);
}

static inline int osos_list_is_shuffled(struct osos_media_list *list)
{
    return *((uint8_t *)list + OSOS_LIST_FLAGS_OFFSET) & 1;
}

static inline int osos_list_is_reversed(struct osos_media_list *list)
{
    return (*((uint8_t *)list + OSOS_LIST_ORDER_FLAGS_OFFSET) & 4) != 0;
}

static inline struct osos_media_order *
osos_list_shuffle_index(struct osos_media_list *list)
{
    return *(struct osos_media_order **)((uint8_t *)list +
                                         OSOS_LIST_SHUFFLE_INDEX_OFFSET);
}

static inline int osos_playback_list_valid(struct osos_playback_list *list)
{
    typedef int (*fn)(struct osos_playback_list *);
    return ((fn)OSOS_PLAYBACK_LIST_VALID)(list);
}

static inline struct osos_media_entry *
osos_playback_entry(struct osos_playback_list *list, int index)
{
    typedef struct osos_media_entry *(*fn)(struct osos_playback_list *, int);
    return ((fn)OSOS_PLAYBACK_ENTRY)(list, index);
}

static inline void osos_database_begin_update(void *database)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_DATABASE_BEGIN_UPDATE)(database);
}

static inline void osos_media_list_invalidate_order(struct osos_media_list *list,
                                                    unsigned order)
{
    typedef void (*fn)(struct osos_media_list *, unsigned);
    ((fn)OSOS_MEDIA_LIST_INVALIDATE_ORDER)(list, order);
}

static inline void osos_database_end_update(void *database)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_DATABASE_END_UPDATE)(database);
}

static inline struct osos_media_entry *
osos_media_entry_copy(struct osos_media_entry *entry, struct osos_media_list *list,
                      struct osos_media_entry **cursor, int flags)
{
    typedef struct osos_media_entry *(*fn)(struct osos_media_entry *,
                                           struct osos_media_list *,
                                           struct osos_media_entry **, int);
    return ((fn)OSOS_MEDIA_ENTRY_COPY)(entry, list, cursor, flags);
}

static inline void osos_player_clear_prepared(struct osos_player *player)
{
    typedef void (*fn)(struct osos_player *);
    ((fn)OSOS_PLAYER_CLEAR_PREPARED)(player);
}

static inline void osos_player_stop_artwork(struct osos_player *player)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_ARTWORK_SESSION_STOP)((uint8_t *)player + 0x88);
}

static inline int osos_player_rebuild_artwork(struct osos_player *player)
{
    typedef int (*fn)(struct osos_player *);
    return ((fn)OSOS_PLAYER_REBUILD_ARTWORK)(player);
}

static inline void osos_audio_set_playback_source(void *source)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_AUDIO_SET_PLAYBACK_SOURCE)(source);
}

static inline void osos_context_menu_append(struct osos_context_menu *menu,
                                            uint32_t label, const char *action,
                                            uint32_t flags)
{
    typedef void (*append)(struct osos_context_menu *, uint32_t, const char *,
                           uint32_t);
    ((append)OSOS_CONTEXT_MENU_APPEND)(menu, label, action, flags);
}

static inline void *osos_genius_get(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_GENIUS_GET)();
}

static inline int osos_genius_map_row(void *genius, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)OSOS_GENIUS_MAP_ROW)(genius, row);
}

static inline void osos_media_item_init(struct osos_media_item *item)
{
    typedef struct osos_media_item *(*fn)(struct osos_media_item *,
                                          struct osos_media_record *, uint8_t);
    ((fn)OSOS_MEDIA_ITEM_INIT)(item, NULL, 0);
}

static inline void osos_genius_get_item(void *genius, int row,
                                        struct osos_media_item *item)
{
    typedef void (*fn)(void *, int, struct osos_media_item *);
    ((fn)OSOS_GENIUS_GET_ITEM)(genius, row, item);
}

static inline int osos_song_controller_map_row(void *controller, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)osos_method(controller, 0x130))(controller, row);
}

static inline void *osos_song_controller_model(void *controller)
{
    typedef void *(*fn)(void *);
    return ((fn)OSOS_SONG_CONTROLLER_MODEL)((uint8_t *)controller + 0xb4);
}

static inline void osos_song_model_get_item(void *model, int row,
                                            struct osos_media_item *item)
{
    /* Native struct return uses an output pointer before this. */
    typedef void (*fn)(struct osos_media_item *, void *, int);
    ((fn)osos_method(model, OSOS_SONG_MODEL_GET_ITEM_SLOT))(item, model, row);
}

struct osos_native_string {
    uintptr_t *vtable;
    char *data;
};

static inline void osos_native_string_init(struct osos_native_string *string)
{
    typedef struct osos_native_string *(*fn)(struct osos_native_string *);
    ((fn)OSOS_NATIVE_STRING_INIT)(string);
}

static inline void osos_native_string_destroy(struct osos_native_string *string)
{
    typedef struct osos_native_string *(*fn)(struct osos_native_string *);
    ((fn)OSOS_NATIVE_STRING_DESTROY)(string);
}

static inline const char *osos_native_string_data(struct osos_native_string *string)
{
    typedef char *(*fn)(struct osos_native_string *);
    return ((fn)OSOS_NATIVE_STRING_DATA)(string);
}

static inline void osos_native_string_assign_utf16(struct osos_native_string *string,
                                                   const uint16_t *text, int32_t units)
{
    typedef void (*fn)(struct osos_native_string *, const uint16_t *, int32_t);
    ((fn)OSOS_NATIVE_STRING_ASSIGN_UTF16)(string, text, units);
}

enum osos_media_text {
    OSOS_MEDIA_TITLE = 0x50,
    OSOS_MEDIA_ALBUM = 0x54,
    OSOS_MEDIA_ARTIST = 0x58,
    OSOS_MEDIA_GENRE = 0x60,
    OSOS_MEDIA_COMPOSER = 0x64,
    OSOS_MEDIA_GROUPING = 0x68,
};

static inline void osos_media_item_text(struct osos_media_item *item,
                                        enum osos_media_text field,
                                        struct osos_native_string *text)
{
    typedef void (*fn)(struct osos_media_item *, struct osos_native_string *);
    ((fn)osos_method(item, field))(item, text);
}

#define OSOS_MEDIA_GETTER(name, type, slot)                                            \
    static inline type osos_media_item_##name(struct osos_media_item *item)            \
    {                                                                                  \
        typedef type (*fn)(struct osos_media_item *);                                  \
        return ((fn)osos_method(item, slot))(item);                                    \
    }
OSOS_MEDIA_GETTER(duration, uint32_t, 0xa0)
OSOS_MEDIA_GETTER(file_size, uint32_t, 0xac)
OSOS_MEDIA_GETTER(bitrate, uint16_t, 0xb0)
OSOS_MEDIA_GETTER(year, uint16_t, 0xb4)
OSOS_MEDIA_GETTER(rating, int32_t, 0xb8)
OSOS_MEDIA_GETTER(track, uint16_t, 0xbc)
OSOS_MEDIA_GETTER(disc, uint16_t, 0xc0)
OSOS_MEDIA_GETTER(play_count, uint32_t, 0xc4)
OSOS_MEDIA_GETTER(skip_count, uint32_t, 0xcc)
#undef OSOS_MEDIA_GETTER

static inline void osos_player_current_item(struct osos_player *player,
                                            struct osos_media_item *item)
{
    typedef void (*fn)(struct osos_player *, struct osos_media_item *);
    ((fn)osos_method(player, OSOS_PLAYER_CURRENT_ITEM_SLOT))(player, item);
}

struct osos_path {
    uint16_t volume;
    char text[258];
};

/* Native file modes: 1 reads; 2 opens or creates for writing. */
static inline int osos_file_open(const struct osos_path *path, uint32_t mode,
                                 void **handle)
{
    typedef int (*fn)(const struct osos_path *, uint32_t, uint32_t, void **);
    return ((fn)OSOS_FILE_OPEN)(path, 0x64617461, mode, handle);
}

static inline int osos_file_read(void *handle, void *data, uint32_t bytes,
                                 uint32_t *read)
{
    typedef int (*fn)(void *, void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)OSOS_FILE_READ)(handle, data, bytes, 0, read);
}

static inline int osos_file_write(void *handle, const void *data, uint32_t bytes,
                                  uint32_t *written)
{
    typedef int (*fn)(void *, const void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)OSOS_FILE_WRITE)(handle, data, bytes, 1, written);
}

static inline void osos_file_close(void *handle)
{
    typedef int (*fn)(void *);
    ((fn)OSOS_FILE_CLOSE)(handle);
}

/* Resource-screen factories receive name, screen, layout and transition. */
typedef void *(*osos_controller_factory)(const char *, uint32_t, uint32_t, uint32_t);

struct osos_album_group {
    uint32_t token, artist, display_artist;
    uint32_t artwork;
    uint32_t tracks, flags;
};

_Static_assert(sizeof(struct osos_album_group) == 24, "Album group layout");

static inline void *osos_import_alloc(uint32_t bytes)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_IMPORT_ALLOC)(bytes);
}

static inline void osos_import_free(void *allocation)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_IMPORT_FREE)(allocation);
}

static inline void *osos_cpp_new(uint32_t bytes)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_CPP_NEW)(bytes);
}

static inline int osos_import_string_cache_release(void *cache, uint32_t token)
{
    typedef int (*fn)(void *, uint32_t);
    return ((fn)OSOS_IMPORT_STRING_CACHE_RELEASE)(cache, token);
}

static inline int osos_import_string_cache_read(void *cache, uint32_t token, void *text,
                                                uint32_t *bytes, uint32_t capacity)
{
    typedef int (*fn)(void *, uint32_t, void *, uint32_t *, uint32_t);
    return ((fn)OSOS_IMPORT_STRING_CACHE_READ)(cache, token, text, bytes, capacity);
}

static inline int osos_import_file_position(void *file, uint64_t *position)
{
    typedef int (*fn)(void *, uint64_t *);
    return ((fn)OSOS_IMPORT_FILE_POSITION)(file, position);
}

static inline int osos_itunes_import_tracks(void *reader)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_ITUNES_IMPORT_TRACKS)(reader);
}

static inline int osos_itunes_skip_bytes(void *reader, int32_t bytes)
{
    typedef int (*fn)(void *, int32_t);
    return ((fn)OSOS_ITUNES_SKIP_BYTES)(reader, bytes);
}

static inline int osos_itunes_import_string(void *reader, uint32_t output_encoding,
                                            uint32_t mhod_type, void *cache,
                                            uint32_t *token, uint32_t glyph_field,
                                            int collect_characters)
{
    typedef int (*fn)(void *, uint32_t, uint32_t, void *, uint32_t *, uint32_t, int);
    return ((fn)OSOS_ITUNES_IMPORT_STRING)(reader, output_encoding, mhod_type, cache,
                                           token, glyph_field, collect_characters);
}

static inline void osos_trackdata_destroy_storage(void *track_data)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_TRACKDATA_DESTROY_STORAGE)(track_data);
}

static inline int osos_string_cache_view(void *cache, uint32_t token,
                                         const uint16_t **text, uint32_t *bytes)
{
    typedef int (*fn)(void *, uint32_t, const uint16_t **, uint32_t *);
    return ((fn)OSOS_STRING_CACHE_VIEW)(cache, token, text, bytes);
}

static inline int osos_text_compare(void *context, const uint16_t *left,
                                    uint32_t left_units, const uint16_t *right,
                                    uint32_t right_units)
{
    typedef int (*fn)(void *, const uint16_t *, uint32_t, const uint16_t *, uint32_t);
    return ((fn)OSOS_TEXT_COMPARE)(context, left, left_units, right, right_units);
}

static inline void osos_string_init_bytes(struct osos_string *string, const void *data,
                                          uint32_t size, void *allocator)
{
    typedef void (*fn)(struct osos_string *, const void *, uint32_t, void *);
    ((fn)OSOS_STRING_INIT_BYTES)(string, data, size, allocator);
}

static inline void osos_string_stream_init(void *stream, struct osos_string *string,
                                           uint32_t mode)
{
    typedef void (*fn)(void *, struct osos_string *, uint32_t);
    ((fn)OSOS_STRING_STREAM_INIT)(stream, string, mode);
}

static inline void osos_string_stream_destroy(void *stream, uint32_t flags,
                                              void *storage)
{
    typedef void (*fn)(void *, uint32_t, void *);
    ((fn)OSOS_STRING_STREAM_DESTROY)(stream, flags, storage);
}

static inline void osos_ui_event_map_read(void *stream, void *map)
{
    typedef void (*fn)(void *, void *);
    ((fn)OSOS_UI_EVENT_MAP_READ)(stream, map);
}

static inline void osos_ui_load_layout_events(void *layout)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_UI_LOAD_LAYOUT_EVENTS)(layout);
}

static inline void osos_resource_block_append(void *list, void *data, uint32_t size,
                                              uint32_t metadata)
{
    typedef void (*fn)(void *, void *, uint32_t, uint32_t);
    ((fn)OSOS_RESOURCE_BLOCK_APPEND)(list, data, size, metadata);
}

static inline void *osos_ui_service_get(uint32_t id)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_UI_SERVICE_GET)(id);
}

static inline void *osos_ui_selection(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_UI_SELECTION)();
}

static inline int osos_menu_item_set_visible(uint32_t item, uint32_t visible)
{
    typedef int (*fn)(uint32_t, uint32_t);
    return ((fn)OSOS_MENU_ITEM_SET_VISIBLE)(item, visible);
}

static inline int osos_settings_indexed_property(void *model, uint32_t type,
                                                 uint32_t property, uint32_t index,
                                                 uintptr_t *out)
{
    typedef int (*fn)(void *, uint32_t, uint32_t, uint32_t, uintptr_t *);
    return ((fn)OSOS_SETTINGS_INDEXED_PROPERTY)(model, type, property, index, out);
}

static inline void *osos_music_menu_apply(void *model)
{
    typedef void *(*fn)(void *);
    return ((fn)OSOS_MUSIC_MENU_APPLY)(model);
}

static inline int osos_music_action(void *controller, const char *action,
                                    uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)OSOS_MUSIC_ACTION)(controller, action, argument);
}

static inline int osos_now_playing_action(void *controller, const char *action,
                                          uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)OSOS_NOW_PLAYING_ACTION)(controller, action, argument);
}

static inline void osos_wheel_decode(void *event)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_WHEEL_DECODE)(event);
}

static inline int osos_wheel_step(uint32_t delta, uint32_t step)
{
    typedef int (*fn)(uint32_t, uint32_t);
    return ((fn)OSOS_WHEEL_STEP)(delta, step);
}

static inline int osos_controller_handle_event(void *controller, void *event)
{
    typedef int (*fn)(void *, void *);
    return ((fn)OSOS_CONTROLLER_HANDLE_EVENT)(controller, event);
}

static inline uint32_t osos_controller_deactivate(void *controller, uint32_t leaving)
{
    typedef uint32_t (*fn)(void *, uint32_t);
    return ((fn)OSOS_CONTROLLER_DEACTIVATE)(controller, leaving);
}

static inline void *osos_controller_create_screen(const char *name, uint32_t screen,
                                                  uint32_t layout, uint32_t transition)
{
    typedef void *(*fn)(const char *, uint32_t, uint32_t, uint32_t);
    return ((fn)OSOS_CONTROLLER_CREATE_SCREEN)(name, screen, layout, transition);
}

static inline void *osos_controller_registry(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_CONTROLLER_REGISTRY)();
}

static inline void *osos_create_screen_controller(void *registry, uint32_t screen,
                                                  uint32_t layout, uint32_t transition)
{
    typedef void *(*fn)(void *, uint32_t, uint32_t, uint32_t);
    return ((fn)OSOS_CREATE_SCREEN_CONTROLLER)(registry, screen, layout, transition);
}

static inline void osos_ui_register_controllers(void *registry, const char *name,
                                                osos_controller_factory ordinary,
                                                osos_controller_factory resource)
{
    typedef void (*fn)(void *, const char *, osos_controller_factory,
                       osos_controller_factory);
    ((fn)OSOS_UI_REGISTER_CONTROLLERS)(registry, name, ordinary, resource);
}

static inline int osos_media_list_add_to_otg(void *controller)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_MEDIA_LIST_ADD_TO_OTG)(controller);
}

static inline int osos_otg_add_browser(void *otg, int row, void *browser, int save)
{
    typedef int (*fn)(void *, int, void *, int);
    return ((fn)OSOS_OTG_ADD_BROWSER)(otg, row, browser, save);
}

static inline void *osos_browser_owner(void *browser)
{
    typedef void *(*fn)(void *);
    return ((fn)OSOS_BROWSER_OWNER)(browser);
}

static inline struct osos_media_list *osos_browser_current_list(void *browser)
{
    typedef struct osos_media_list *(*fn)(void *);
    return ((fn)OSOS_BROWSER_CURRENT_LIST)(browser);
}

static inline uint32_t osos_browser_build_songs(void *browser)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)OSOS_BROWSER_BUILD_SONGS)(browser);
}

static inline int osos_browser_order_mode(void *browser)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_BROWSER_ORDER_MODE)(browser);
}

static inline int osos_browser_create(void *owner, void **browser)
{
    typedef int (*fn)(void *, void **);
    return ((fn)OSOS_BROWSER_CREATE)(owner, browser);
}

static inline int osos_browser_filter(void *browser, struct osos_media_entry *entry,
                                      struct osos_media_list *list)
{
    typedef int (*fn)(void *, struct osos_media_entry *, struct osos_media_list *);
    return ((fn)OSOS_BROWSER_FILTER)(browser, entry, list);
}

static inline int osos_browser_invalidate(void *browser)
{
    typedef int (*fn)(void *);
    return ((fn)OSOS_BROWSER_INVALIDATE)(browser);
}

static inline void osos_browser_clear_artists(void *browser)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_BROWSER_CLEAR_ARTISTS)(browser);
}

static inline void osos_browser_clear_albums(void *browser)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_BROWSER_CLEAR_ALBUMS)(browser);
}

static inline void osos_browser_clear_songs(void *browser)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_BROWSER_CLEAR_SONGS)(browser);
}

static inline uint32_t osos_browser_artwork(void *owner,
                                            struct osos_media_record *record)
{
    typedef uint32_t (*fn)(void *, struct osos_media_record *);
    return ((fn)OSOS_BROWSER_ARTWORK)(owner, record);
}

static inline int osos_browser_album_group(void *browser, void *callback,
                                           uint32_t field_low, uint32_t field_high,
                                           uint32_t index,
                                           struct osos_media_entry *entry,
                                           struct osos_media_record *record, int first)
{
    typedef int (*fn)(void *, void *, uint32_t, uint32_t, uint32_t,
                      struct osos_media_entry *, struct osos_media_record *, int);
    return ((fn)OSOS_BROWSER_ALBUM_GROUP)(browser, callback, field_low, field_high,
                                          index, entry, record, first);
}

static inline void osos_album_vector_append(void *vector, struct osos_album_group *row)
{
    typedef void (*fn)(void *, struct osos_album_group *);
    ((fn)OSOS_ALBUM_VECTOR_APPEND)(vector, row);
}

static inline struct osos_media_entry *
osos_list_order_entry(struct osos_media_list *list, int mode, uint32_t grouped,
                      uint32_t index)
{
    typedef struct osos_media_entry *(*fn)(struct osos_media_list *, int, uint32_t,
                                           uint32_t);
    return ((fn)OSOS_LIST_ORDER_ENTRY)(list, mode, grouped, index);
}

static inline void *osos_model_create(uint32_t ownership)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_MODEL_CREATE)(ownership);
}

static inline void osos_model_ref_init(void **reference, void *model, int flags)
{
    typedef void (*fn)(void **, void *, int);
    ((fn)OSOS_MODEL_REF_INIT)(reference, model, flags);
}

static inline void osos_model_ref_assign(void *reference, void *source)
{
    typedef void (*fn)(void *, void *);
    ((fn)OSOS_MODEL_REF_ASSIGN)(reference, source);
}

static inline void osos_model_ref_release(void *reference)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_MODEL_REF_RELEASE)(reference);
}

static inline void *osos_model_ref_get(void *reference)
{
    typedef void *(*fn)(void *);
    return ((fn)OSOS_MODEL_REF_GET)(reference);
}

static inline void *osos_model_init_borrowed(void *model, uint8_t item_flags,
                                             void *browser)
{
    typedef void *(*fn)(void *, uint8_t, void *);
    return ((fn)OSOS_MODEL_INIT_BORROWED)(model, item_flags, browser);
}

static inline void osos_model_album_subtitle(void *model, int index,
                                             struct osos_native_string *out)
{
    typedef void (*fn)(void *, int, struct osos_native_string *);
    ((fn)OSOS_MODEL_ALBUM_SUBTITLE)(model, index, out);
}

static inline uint32_t osos_select_artwork_source(void *selection, uint32_t category)
{
    typedef uint32_t (*fn)(void *, uint32_t);
    return ((fn)OSOS_SELECT_ARTWORK_SOURCE)(selection, category);
}

static inline void *osos_album_artwork_create(uint32_t kind, uint32_t selector)
{
    typedef void *(*fn)(uint32_t, uint32_t);
    return ((fn)OSOS_ALBUM_ARTWORK_CREATE)(kind, selector);
}

static inline uint32_t osos_selection_index(void *selection)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)osos_method(selection, 0x168))(selection);
}

static inline uint32_t osos_preferences_get(void *preferences, uint32_t key,
                                            uint32_t type)
{
    typedef uint32_t (*fn)(void *, uint32_t, uint32_t);
    return ((fn)osos_method(preferences, 0xdc))(preferences, key, type);
}

static inline void osos_preferences_set(void *preferences, uint32_t key, uint32_t type,
                                        uint32_t value)
{
    typedef void (*fn)(void *, uint32_t, uint32_t, uint32_t);
    ((fn)osos_method(preferences, 0xd4))(preferences, key, type, value);
}

static inline void osos_menu_toggle(void *model, unsigned menu, uint32_t index)
{
    typedef void (*fn)(void *, uint32_t);
    fn toggle = menu ? (fn)OSOS_MUSIC_MENU_TOGGLE : (fn)OSOS_MAIN_MENU_TOGGLE;
    toggle(model, index);
}

static inline void osos_model_destroy(void *model)
{
    typedef void (*fn)(void *);
    ((fn)osos_method(model, 0x1c))(model);
}

static inline void osos_library_model_prepare_artists(void *model)
{
    typedef void (*reset_fn)(void *);
    typedef void (*configure_fn)(void *, uint32_t, uint32_t);
    ((reset_fn)osos_method(model, 0x38))(model);
    ((reset_fn)osos_method(model, 0xd8))(model);
    ((configure_fn)osos_method(model, 0x5c))(model, 0x11, 0);
}

static inline void osos_controller_push(void *controller, void *screen)
{
    typedef void (*fn)(void *, void *);
    ((fn)osos_method(controller, 0xd0))(controller, screen);
}

#endif
#endif
