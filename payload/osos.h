/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_OSOS_H
#define CFW_OSOS_H

/* Classic 7G FW2.0.4 runtime addresses. Function entries below use ARM state. */
#define OSOS_RESOURCE_BANK_INIT_OVERRIDES 0x08111d0c

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
    ((fn)0x0825ccac)(reader, manifest, mode, signature);
}

/* Apple's libstdc++ string stores one pointer and owns its backing storage. */
struct osos_string {
    char *data;
};

static inline void osos_string_init(struct osos_string *string, const char *text)
{
    typedef void (*fn)(struct osos_string *, const char *);
    ((fn)0x083cdc84)(string, text);
}

static inline void osos_string_destroy(struct osos_string *string)
{
    typedef void (*fn)(struct osos_string *);
    ((fn)0x083cdc2c)(string);
}

/* Find or insert a name in the global resource-name map and return its ID slot. */
static inline uint32_t *osos_resource_name_slot(const struct osos_string *name)
{
    typedef uint32_t *(*fn)(void *, const struct osos_string *);
    return ((fn)0x083d0884)((void *)0x08ad7c6c, name);
}

/* Copy resource data into the bank's override map. */
static inline void osos_resource_set(void *bank, uint32_t type, uint32_t id,
                                     const void *data, uint32_t size)
{
    typedef void (*fn)(void *, uint32_t, uint32_t, const void *, uint32_t);
    ((fn)0x08111c2c)(bank, type, id, data, size);
}

/* Advance an active resource enumeration; ROM banks enumerate the stock index. */
static inline const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{
    typedef const void *(*fn)(void *, uint32_t *, uint32_t *);
    return ((fn)0x08111e0c)(bank, id, size);
}

static inline void *osos_resource_bank(void)
{
    typedef void *(*fn)(void);
    return ((fn)0x08194ed8)();
}

static inline void *osos_resource_get(void *bank, uint32_t type, uint32_t id,
                                      uint32_t *size)
{
    typedef void *(*fn)(void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x08111b80)(bank, type, id, size);
}

/* Notify native list providers that the item's displayed content changed. */
static inline void osos_menu_item_changed(uint32_t item)
{
    typedef void *(*service_fn)(void);
    typedef void (*notify_fn)(void *, uint32_t, uint32_t);
    void *service = ((service_fn)0x08267464)();
    uintptr_t *vtable = *(uintptr_t **)service;
    ((notify_fn)vtable[0x58 / 4])(service, 0x4e6f6e65, item); /* None */
}

static inline int osos_settings_action(void *controller, const char *action,
                                       uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)0x0821c690)(controller, action, argument);
}

static inline void osos_eq_load(void *state)
{
    typedef void (*fn)(void *);
    ((fn)0x08271bf0)(state);
}

static inline void osos_eq_reset(void *state)
{
    typedef void (*fn)(void *);
    ((fn)0x08271d60)(state);
}

static inline void osos_eq_process(void *state, int16_t *samples, uint32_t frames,
                                   int16_t **output, uint32_t *output_frames)
{
    typedef void (*fn)(void *, int16_t *, uint32_t, int16_t **, uint32_t *);
    ((fn)0x08271f2c)(state, samples, frames, output, output_frames);
}

/* Classic 7G 2.0.4 native layouts; the media item is a non-owning view. */
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

struct osos_media_list {
    uint8_t reserved_00[0x0c];
    void *database;
    uint8_t reserved_10[0x30];
    struct osos_media_entry *root;
    uint8_t reserved_44[0x84];
    uint32_t order;
    uint8_t reserved_cc[0xb8];
    uint16_t repeat;
    uint8_t reserved_186[6];
    uint8_t flags, order_flags;
    uint8_t reserved_18e[0x226];
    struct osos_media_order *shuffle_index;
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
_Static_assert(offsetof(struct osos_media_list, flags) == 0x18c, "List layout");
_Static_assert(offsetof(struct osos_media_list, shuffle_index) == 0x3b4,
               "Shuffle index layout");
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
    return ((get_instance)0x08171fdc)();
}

static inline void osos_player_lock(struct osos_player *player)
{
    ((void (*)(void *))0x082572d0)((uint8_t *)player + 0xa4c);
}

static inline void osos_player_unlock(struct osos_player *player)
{
    ((void (*)(void *))0x082572d4)((uint8_t *)player + 0xa4c);
}

static inline int osos_media_item_valid(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)0x08298ef4)(item);
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
    return ((fn)0x0829969c)(item);
}

static inline int osos_media_item_has_kind_200000(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)0x082996cc)(item);
}

static inline int osos_media_item_has_kind_8(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)0x082990ec)(item);
}

static inline int osos_media_item_has_kind_8062(struct osos_media_item *item)
{
    typedef int (*fn)(struct osos_media_item *);
    return ((fn)0x082991f0)(item);
}

static inline int osos_playback_list_valid(struct osos_playback_list *list)
{
    typedef int (*fn)(struct osos_playback_list *);
    return ((fn)0x0829b748)(list);
}

static inline struct osos_media_entry *
osos_playback_entry(struct osos_playback_list *list, int index)
{
    typedef struct osos_media_entry *(*fn)(struct osos_playback_list *, int);
    return ((fn)0x0829b1d0)(list, index);
}

static inline void osos_database_begin_update(void *database)
{
    typedef void (*fn)(void *);
    ((fn)0x08051348)(database);
}

static inline void osos_media_list_invalidate_order(struct osos_media_list *list,
                                                    unsigned order)
{
    typedef void (*fn)(struct osos_media_list *, unsigned);
    ((fn)0x080d187c)(list, order);
}

static inline void osos_database_end_update(void *database)
{
    typedef void (*fn)(void *);
    ((fn)0x080611e0)(database);
}

static inline struct osos_media_entry *
osos_media_entry_copy(struct osos_media_entry *entry, struct osos_media_list *list,
                      struct osos_media_entry **cursor, int flags)
{
    typedef struct osos_media_entry *(*fn)(struct osos_media_entry *,
                                           struct osos_media_list *,
                                           struct osos_media_entry **, int);
    return ((fn)0x0803bbf0)(entry, list, cursor, flags);
}

static inline void osos_player_clear_prepared(struct osos_player *player)
{
    typedef void (*fn)(struct osos_player *);
    ((fn)0x08171110)(player);
}

static inline void osos_audio_set_playback_source(void *source)
{
    typedef void (*fn)(void *);
    ((fn)0x081a5c00)(source);
}

static inline void osos_context_menu_append(struct osos_context_menu *menu,
                                            uint32_t label, const char *action,
                                            uint32_t flags)
{
    typedef void (*append)(struct osos_context_menu *, uint32_t, const char *,
                           uint32_t);
    ((append)0x08161bcc)(menu, label, action, flags);
}

static inline void *osos_genius_get(void)
{
    typedef void *(*fn)(void);
    return ((fn)0x08121854)();
}

static inline int osos_genius_map_row(void *genius, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)0x08121f88)(genius, row);
}

static inline void osos_media_item_init(struct osos_media_item *item)
{
    typedef void (*fn)(struct osos_media_item *, int, int);
    ((fn)0x082654bc)(item, 0, 0);
}

static inline void osos_genius_get_item(void *genius, int row,
                                        struct osos_media_item *item)
{
    typedef void (*fn)(void *, int, struct osos_media_item *);
    ((fn)0x08121884)(genius, row, item);
}

static inline int osos_song_controller_map_row(void *controller, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)osos_method(controller, 0x130))(controller, row);
}

static inline void *osos_song_controller_model(void *controller)
{
    typedef void *(*fn)(void *);
    return ((fn)0x083cb174)((uint8_t *)controller + 0xb4);
}

static inline void osos_song_model_get_item(void *model, int row,
                                            struct osos_media_item *item)
{
    /* Native struct return uses an output pointer before this. */
    typedef void (*fn)(struct osos_media_item *, void *, int);
    ((fn)osos_method(model, 0x22c))(item, model, row);
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
    return ((fn)0x0804f75c)(path, 0x64617461, mode, handle);
}

static inline int osos_file_read(void *handle, void *data, uint32_t bytes,
                                 uint32_t *read)
{
    typedef int (*fn)(void *, void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x0804f810)(handle, data, bytes, 0, read);
}

static inline int osos_file_write(void *handle, const void *data, uint32_t bytes,
                                  uint32_t *written)
{
    typedef int (*fn)(void *, const void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x0804ff40)(handle, data, bytes, 1, written);
}

static inline void osos_file_close(void *handle)
{
    typedef int (*fn)(void *);
    ((fn)0x0804f6bc)(handle);
}

#endif
#endif
