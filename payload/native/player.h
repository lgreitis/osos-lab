/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_NATIVE_PLAYER_H
#define CFW_NATIVE_PLAYER_H

/* Included through osos.h after the native types and osos_method. */
#define OSOS_BROWSER_SONGS_OFFSET 0xeec
#define OSOS_BROWSER_SONGS_JUMP_OFFSET 0xef0
#define OSOS_BROWSER_SONGS_COUNT_OFFSET 0xef4

static inline void **osos_browser_registry(void)
{
    return (void **)OSOS_BROWSER_REGISTRY;
}

static inline int osos_video_playlist_action(void *controller, const char *action,
                                             uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)OSOS_VIDEO_PLAYLIST_ACTION)(controller, action, argument);
}

static inline void *osos_navigator_top(void)
{
    typedef void *(*get_fn)(void);
    typedef void *(*top_fn)(void *);
    void *navigator = ((get_fn)OSOS_NAVIGATOR)();
    return navigator ? ((top_fn)osos_method(navigator, 0x14))(navigator) : NULL;
}

static inline uint32_t osos_screen_id(void *screen)
{
    typedef uint32_t (*fn)(void *);
    return screen ? ((fn)osos_method(screen, 0x68))(screen) : 0;
}

static inline void osos_pop_to_main_screen(void)
{
    typedef void (*fn)(void);
    ((fn)OSOS_POP_TO_MAIN_SCREEN)();
}

static inline void *osos_selection_get(void)
{
    typedef void *(*fn)(void);
    return ((fn)OSOS_SELECTION_GET)();
}

static inline void **osos_selection_reference(void *selection)
{
    return (void **)((uint8_t *)selection + 0x18);
}

static inline void osos_selection_set_list(void *selection, void *reference,
                                           uint32_t category, uint32_t option,
                                           uint8_t extra)
{
    typedef void (*fn)(void *, void *, uint32_t, uint32_t, uint8_t);
    ((fn)OSOS_SELECTION_SET_LIST)(selection, reference, category, option, extra);
}

static inline void osos_selection_set_row(void *selection, int row)
{
    typedef void (*fn)(void *, int);
    ((fn)OSOS_SELECTION_SET_ROW)(selection, row);
}

static inline void osos_selection_set_title(void *selection, uint32_t kind,
                                            uint32_t title)
{
    typedef void (*fn)(void *, uint32_t, uint32_t);
    ((fn)OSOS_SELECTION_SET_TITLE)(selection, kind, title);
}

static inline void osos_selection_reload(void *selection)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_SELECTION_RELOAD)(selection);
}

static inline void osos_row_blink(int row)
{
    typedef void (*fn)(int);
    ((fn)OSOS_ROW_BLINK)(row);
}

static inline int osos_list_view_row(void *view)
{
    typedef int (*fn)(void *);
    return ((fn)osos_method(view, 0x168))(view);
}

static inline void osos_list_view_set_row(void *view, int row)
{
    typedef void (*fn)(void *, int);
    ((fn)osos_method(view, 0x184))(view, row);
}

static inline void osos_controller_post_action(void *controller,
                                               struct osos_string *action)
{
    typedef void (*fn)(void *, struct osos_string *);
    ((fn)osos_method(controller, 0x11c))(controller, action);
}

static inline struct osos_media_entry **osos_songs_alloc(uint32_t bytes)
{
    typedef void *(*fn)(uint32_t);
    return ((fn)OSOS_SONGS_ALLOC)(bytes);
}

static inline int osos_media_entry_delete(struct osos_media_entry *entry)
{
    typedef int (*fn)(struct osos_media_entry *);
    return ((fn)OSOS_MEDIA_ENTRY_DELETE)(entry);
}

static inline void osos_library_read_song_item(struct osos_media_item *out, void *model,
                                               int row)
{
    typedef void (*fn)(struct osos_media_item *, void *, int);
    ((fn)OSOS_LIBRARY_READ_SONG_ITEM)(out, model, row);
}

static inline void osos_library_model_prepare_songs(void *model)
{
    typedef void (*reset_fn)(void *);
    typedef void (*configure_fn)(void *, uint32_t, uint32_t);
    typedef void (*order_fn)(void *, uint32_t);
    ((reset_fn)osos_method(model, 0x38))(model);
    ((reset_fn)osos_method(model, 0xec))(model);
    ((configure_fn)osos_method(model, 0x5c))(model, 0x11, 0);
    ((order_fn)osos_method(model, 0x100))(model, 0);
}

static inline void osos_player_skip(int previous)
{
    typedef void *(*get_fn)(void);
    typedef int (*state_fn)(void *);
    typedef void (*skip_fn)(void *, int);
    typedef void (*resume_fn)(void *);
    uint8_t *interface = ((get_fn)OSOS_PLAYER_INTERFACE_GET)();
    if (!interface)
        return;
    void *player = interface - 20;
    if (!((state_fn)osos_method(player, 0x1a0))(player))
        ((skip_fn)osos_method(player, previous ? 0x10c : 0x108))(player, 1);
    else
        ((resume_fn)osos_method(player, 0xfc))(player);
}

static inline void osos_player_play_pause(void)
{
    typedef void (*fn)(void);
    ((fn)OSOS_PLAYER_PLAY_PAUSE)();
}

static inline int osos_player_play_index(struct osos_player *player, int index)
{
    typedef int (*fn)(void *, int, int, unsigned, unsigned);
    return ((fn)osos_method(player, 0xe0))(player, index, 0, 0, 0);
}

#endif
