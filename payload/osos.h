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

static inline void *osos_ui_service_get(uint32_t id)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_UI_SERVICE_GET)(id);
}

static inline void *osos_now_playing_model(void)
{
    typedef void *(*interface_fn)(void *, uint32_t);
    void *service = osos_ui_service_get(0x7f00);
    if (!service)
        return 0;
    uintptr_t *vtable = *(uintptr_t **)service;
    return ((interface_fn)vtable[0x14 / 4])(service, 5);
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

/* Media items are non-owning views of a record. */
struct osos_media_record;

struct osos_media_item {
    uintptr_t *vtable;
    void *entry;
    struct osos_media_record *record;
    uint32_t reserved[3];
};

struct osos_player;

struct osos_context_menu;

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

static inline int osos_media_item_valid(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)OSOS_MEDIA_ITEM_VALID)(item);
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

static inline void osos_media_item_init_record(struct osos_media_item *item,
                                               struct osos_media_record *record)
{
    typedef struct osos_media_item *(*fn)(struct osos_media_item *,
                                          struct osos_media_record *, uint8_t);
    ((fn)OSOS_MEDIA_ITEM_INIT)(item, record, 0);
}

static inline void osos_media_item_init(struct osos_media_item *item)
{
    osos_media_item_init_record(item, NULL);
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

#include "native/path.h"

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

static inline int osos_file_size(void *handle, uint32_t *size)
{
    typedef int (*fn)(void *, uint32_t *);
    return ((fn)OSOS_FILE_SIZE)(handle, size);
}

static inline int osos_file_seek(void *handle, uint64_t offset, int origin)
{
    typedef int (*fn)(void *, uint64_t, int);
    return ((fn)OSOS_FILE_SEEK)(handle, offset, origin);
}

static inline void osos_file_close(void *handle)
{
    typedef int (*fn)(void *);
    ((fn)OSOS_FILE_CLOSE)(handle);
}

/* Deletes a file (or empty directory); returns 0, or -43 when it does not exist. */
static inline int osos_file_delete(const struct osos_path *path)
{
    typedef int (*fn)(const struct osos_path *);
    return ((fn)OSOS_FILE_DELETE)(path);
}

/* Writes the native Preferences file only when a native setter marked it dirty. */
static inline void osos_preferences_flush(void *preferences)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_PREFERENCES_FLUSH)(preferences);
}

/* Takes the settings model; also resets the main and music menus. */
static inline void osos_reset_all_settings(void *model)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_RESET_ALL_SETTINGS)(model);
}

/* Resource-screen factories receive name, screen, layout and transition. */
typedef void *(*osos_controller_factory)(const char *, uint32_t, uint32_t, uint32_t);

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

static inline void *osos_ui_selection(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_UI_SELECTION)();
}

/* Select a row of the current layout's list, as TCSettings_EQ does after layout. */
static inline void osos_ui_list_select(uint32_t index)
{
    typedef void *(*view_fn)(void *);
    typedef void *(*interface_fn)(void *, uint32_t);
    typedef void (*select_fn)(void *, uint32_t);
    void *selection = osos_ui_selection();
    void *view = selection ? ((view_fn)osos_method(selection, 0xe4))(selection) : 0;
    void *list =
        view ? ((interface_fn)OSOS_VIEW_INTERFACE)(view, OSOS_UI_LIST_INTERFACE) : 0;
    if (list)
        ((select_fn)osos_method(list, 0x174))(list, index);
}

static inline int osos_settings_indexed_property(void *model, uint32_t type,
                                                 uint32_t property, uint32_t index,
                                                 uintptr_t *out)
{
    typedef int (*fn)(void *, uint32_t, uint32_t, uint32_t, uintptr_t *);
    return ((fn)OSOS_SETTINGS_INDEXED_PROPERTY)(model, type, property, index, out);
}

static inline void osos_wheel_decode(void *event)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_WHEEL_DECODE)(event);
}

static inline uint32_t osos_button_ui_key(void *event)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)OSOS_BUTTON_EVENT_UI_KEY)(event);
}

static inline void *osos_wheel_accumulator_init(void *state, int threshold,
                                                unsigned char accelerate,
                                                uint32_t interval)
{
    typedef void *(*fn)(void *, int, unsigned char, uint32_t);
    return ((fn)OSOS_WHEEL_ACCUMULATOR_INIT)(state, threshold, accelerate, interval);
}

static inline int osos_wheel_accumulator_step(void *state, int motion, uint32_t speed)
{
    typedef int (*fn)(void *, int, uint32_t);
    return ((fn)OSOS_WHEEL_ACCUMULATOR_STEP)(state, motion, speed);
}

static inline void osos_wheel_accumulator_reset(void *state)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_WHEEL_ACCUMULATOR_RESET)(state);
}

static inline int osos_controller_handle_event(void *controller, void *event)
{
    typedef int (*fn)(void *, void *);
    return ((fn)OSOS_CONTROLLER_HANDLE_EVENT)(controller, event);
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

static inline uint32_t osos_selection_index(void *selection)
{
    typedef uint32_t (*fn)(void *);
    return ((fn)osos_method(selection, 0x168))(selection);
}

static inline void osos_menu_toggle(void *model, unsigned menu, uint32_t index)
{
    typedef void (*fn)(void *, uint32_t);
    fn toggle = menu ? (fn)OSOS_MUSIC_MENU_TOGGLE : (fn)OSOS_MAIN_MENU_TOGGLE;
    toggle(model, index);
}

#include "native/context_menu.h"

static inline void osos_controller_push(void *controller, void *screen)
{
    typedef void (*fn)(void *, void *);
    ((fn)osos_method(controller, 0xd0))(controller, screen);
}

#include "native/player.h"

#endif
#endif
