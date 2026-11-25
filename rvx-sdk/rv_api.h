/* SPDX-License-Identifier: GPL-3.0-only */

/* The core services an extension may call, and the structs it declares itself
 * with. Within a major version the rv_api and rv_gfx tables only grow at the
 * end: a module built against an older minor finds every entry it knows at the
 * same offset and tests a newer one with RV_HAS before calling it. The structs
 * an extension fills in (descriptor, hooks, settings, screens, rows, entries)
 * keep their layout. The manifest's `api` names the minor a module needs, and
 * an older core leaves the extension off. The major changes when an entry or
 * a layout changes meaning. Apple functions are not here; modules call them
 * directly through the symbols header and linker script of the OSOS build
 * named by RV_FIRMWARE_ID. Until the first release a minor may also insert
 * entries and change layouts; extensions are rebuilt against the SDK they
 * run with. */

#ifndef RV_API_H
#define RV_API_H
#include <stddef.h>
#include <stdint.h>

#define RV_API_MAJOR 1
#define RV_API_MINOR 84
#define RV_API_VERSION ((RV_API_MAJOR << 16) | RV_API_MINOR)

/* A field of an Apple struct at an offset of the symbols header:
 * RV_FIELD(eq, ThreeBandEQ_preset, uint32_t). */
#define RV_FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

/* The method of an Apple object at a vtable offset of the symbols header, as a
 * function pointer of `type`: RV_METHOD(item, MediaItem_title_method, getter). */
#define RV_METHOD(object, offset, type)                                                        \
    ((type)RV_FIELD(RV_FIELD(object, 0, void *), offset, void *))

/* Whether a table with a leading `size` (rv_api, rv_gfx, rv_debug_info) has
 * `field`: RV_HAS(api, format_size). */
#define RV_HAS(table, field)                                                                   \
    ((table)->size >= __builtin_offsetof(__typeof__(*(table)), field) + sizeof((table)->field))

/* Calls `function` of an optional library when the core found it, else gives
 * `fallback`, (void)0 for a void function: RV_OPTIONAL(rv_dsp_peak, 0, c, 5,
 * rate). A bare call to an optional library's export does not compile. */
#define RV_OPTIONAL(function, fallback, ...)                                                   \
    ((function) ? (function)(__VA_ARGS__) : (fallback))

/* Compiles a function as ARM. Needed for DSP instructions that exist only in ARM
 * state and for calls into Apple routines that return with `mov pc, lr`; the
 * call must not become a tail call, or the routine returns to the Thumb caller in
 * ARM state. */
#define RV_ARM __attribute__((target("arm"), noinline, optimize("no-optimize-sibling-calls")))

/* The loader's record of the extension; passed back to every call. */
typedef struct rv_extension_record *rv_extension_handle;

struct rv_timer;
struct rv_file;
struct rv_debug_info;     /* rv_debug.h */
struct rv_asset_provider; /* rv_theme.h */

enum rv_file_mode { RV_FILE_READ = 1, RV_FILE_WRITE = 2 };

enum rv_result {
    RV_OK,
    RV_CONFLICT,   /* a replace handler meets another handler, or the site is exclusive */
    RV_NO_MEMORY,
    RV_NOT_BOUND,
    RV_BAD_SITE,   /* no site with that id in this core */
    RV_BAD_ARGUMENT,
    RV_NOT_FOUND,
    RV_ALREADY_BOUND, /* the extension's handler is already at the site, or another
                         extension holds the colour roles or the asset provider */
};

enum rv_hook_mode {
    RV_HOOK_MODE_FILTER,  /* receives `next`; may continue through api->hook_next or short-circuit */
    RV_HOOK_MODE_REPLACE, /* exclusive; never continues */
};

/* A handler receives the Apple arguments followed by `next` (in the register
 * after the last argument for sites with at most three register arguments, as
 * the first stack argument for four), then any arguments Apple passes on the
 * stack. Site ids are RV_SITE_* from rv_sites.h, or RV_RAW_SITE(n) for the n-th
 * [[site]] of the extension's manifest. The struct must live as long as the
 * binding. Word sites use REPLACE mode and the uint32_t word member instead of
 * a handler. The core writes the instruction directly; it is never called. A
 * word site that holds an address takes one of the module's own objects
 * through RV_HOOK_POINTER; the loader relocates it. */
#define RV_RAW_SITE(index) (0xffffff00u | (index))
struct rv_hook {
    uint32_t site;
    union {
        void *handler;
        uint32_t word;
    };
    int16_t priority; /* lower runs first */
    uint8_t mode;     /* rv_hook_mode */
    uint8_t reserved;
};

enum rv_setting_type {
    RV_SETTING_TYPE_BOOL = 1,
    RV_SETTING_TYPE_INT = 2,
    RV_SETTING_TYPE_STRING = 3,
    RV_SETTING_TYPE_BLOB = 4,
};

/* The key the core keeps in every extension's settings for its enabled flag; an
 * extension never writes it. RV_SETTING_KEY_ALL is what setting_changed
 * receives after Reset All Settings emptied the store. */
#define RV_SETTING_KEY_ENABLED 0
#define RV_SETTING_KEY_ALL 0xffff

/* A declared setting: the core applies `fallback` when the store has no record,
 * clears the record on Reset All Settings, and renders it from `label`. An
 * RV_ROW_SETTING row shows a bool as On or Off and flips it; an int with
 * `labels` shows the chosen label (nothing for a multi-choice) and steps to
 * the next one, or with `screen` opens a screen of that kind titled `label`:
 * RV_SCREEN_KIND_CHOICE (the value is the chosen index), RV_SCREEN_KIND_SLIDER
 * (a bar over the labels) or RV_SCREEN_KIND_MULTI_CHOICE (bit n of the value
 * is label n). */
struct rv_setting {
    uint16_t key;
    uint8_t type;
    uint8_t screen; /* rv_screen_kind, 0 for no screen of its own */
    int32_t fallback;
    int32_t minimum;
    int32_t maximum;
    const char *label;
    const char *const *labels; /* one per value; at most 32 for a multi-choice */
    uint32_t label_count;
};

enum rv_row_kind {
    RV_ROW_KIND_PUSH,    /* opens `screen` */
    RV_ROW_KIND_ACTION,  /* calls `action` */
    RV_ROW_KIND_SETTING, /* the bool or int setting `key`: shows its value; flips a bool, opens
                            the screen of an int with labels and a screen kind, steps one
                            without to its next label */
    RV_ROW_KIND_INFO,    /* shows `label` or `text`; does nothing when chosen */
};

/* What every UI callback receives: where it runs. `controller` is the Apple
 * controller of the screen or menu, for Apple calls. `arg` is what ui_push
 * passed for this screen, NULL for a screen a row opened. In a context menu's
 * action, `item` is the MediaItem (struct MediaItem of the symbols header) of
 * the song, episode, audiobook or video the menu opened on, NULL for an album
 * or artist, `row` the model row the menu opened on (-1 for an album or
 * artist list's "All" row) and `list` the controller of the list it opened on
 * (NULL for Now Playing). A screen the menu's row or action opens keeps all
 * three for every callback until it closed, and so does a screen opened above
 * it; elsewhere `item` and `list` are NULL and `row` -1. A row that opens a screen from a
 * context menu follows the menu with no frame of the screen underneath. Valid
 * during the callback only. */
struct rv_context {
    rv_extension_handle self;
    void *controller;
    void *arg;
    const void *item;
    int32_t row;
    void *list;
};

/* One row of a list screen or an entry in a menu. `text`, when set, gives the
 * label each time the row is drawn, for a value that changes; `label` still
 * names the row and is what a context menu shows. */
struct rv_row {
    const char *label;
    uint8_t kind;
    uint8_t reserved;
    uint16_t key;
    const char *screen;
    void (*action)(const struct rv_context *ctx);
    const char *(*text)(const struct rv_context *ctx);
};

/* A row of a screen's long-press menu. `available` is asked each time the menu
 * opens; 0 leaves the row out, NULL always shows it. Its callbacks and the
 * row's get ctx->row, the row highlighted when the menu opened. */
struct rv_context_row {
    struct rv_row row;
    int (*available)(const struct rv_context *ctx);
};

enum rv_screen_kind {
    RV_SCREEN_KIND_LIST,         /* rows from `list` */
    RV_SCREEN_KIND_CHOICE,       /* one checkmark among `choice.labels`; stores the index in
                               the int setting `choice.key` and pops */
    RV_SCREEN_KIND_SLIDER,       /* a bar over `choice.labels`; wheel and Previous/Next change
                               the int setting `choice.key` */
    RV_SCREEN_KIND_TEXT,         /* the paragraphs of `text` */
    RV_SCREEN_KIND_CANVAS,       /* the extension draws and reads input */
    RV_SCREEN_KIND_DYNAMIC_LIST, /* rows from callbacks, for data known only at run time */
    RV_SCREEN_KIND_MULTI_CHOICE, /* a checkmark per label; bit n of the int setting `choice.key`
                               is label n (at most 32), Select flips it */
    RV_SCREEN_KIND_STOCK,        /* one of Apple's screens, `stock.screen`, under the
                               extension's title; Apple's controller and bindings */
};

/* The stock screens an extension may show under a title of its own. */
enum rv_stock_screen {
    RV_STOCK_SCREEN_GAMES = 1, /* the Games list, over the games service's list */
};

/* How a list screen (list, choice, multi-choice, dynamic list) is laid out.
 * Left at AUTO it follows the screen it opens from: split under Settings or a
 * split list, full width elsewhere, as the stock menus do. */
enum rv_layout {
    RV_LAYOUT_AUTO,
    RV_LAYOUT_FULL,  /* the list over the whole screen */
    RV_LAYOUT_SPLIT, /* the list on the left, the Settings preview pane on the right */
};

/* How a slider or canvas screen opens. */
enum rv_transition {
    RV_TRANSITION_SLIDE,     /* slides in from the right, as Settings > About */
    RV_TRANSITION_FROM_PANE, /* opened from a split list, it starts where the preview pane is
                             and slides into place, as Extras > Stopwatch; from anywhere
                             else as RV_TRANSITION_SLIDE. The whole screen moves, so this
                             suits a screen whose picture continues what the pane showed */
};

/* A bitmap asset of the extension by name, from a PNG in manifest.toml:
 * `.bitmap = RV_BITMAP("eq-icon")`. A menu pane's bitmap is an asset with
 * `pane = "icon"`, a 78 x 121 coverage mask the system draws in its own colour
 * like the stock Settings icons, or `pane = "picture"`, a colour picture of one
 * of the pane sizes; its badge is a `pane = "badge"` asset. The core decodes
 * only the ones shown. */
struct rv_bitmap {
    const char *asset;
};
#define RV_BITMAP(name) (&(const struct rv_bitmap){.asset = (name)})

/* Drawing, for canvas screens. Coordinates count from the top left of the
 * canvas, below the title bar; the canvas is clipped for the extension. */
struct rv_rect {
    int16_t x, y, width, height;
};

typedef uint32_t rv_color; /* 0xAARRGGBB */

/* The stock colours of the list screens; a theme may change what a role is. */
enum rv_color_role {
    RV_COLOR_BACKGROUND,
    RV_COLOR_TEXT,
    RV_COLOR_HIGHLIGHT,      /* the selected row */
    RV_COLOR_HIGHLIGHT_TEXT, /* text on the selected row */
    RV_COLOR_DIM,            /* secondary text, such as a row's detail */
    RV_COLOR_PARAGRAPH,      /* the text of a text page */
    RV_COLOR_ACCENT_TEXT,    /* the highlight's colour as text, such as the Search box's caret */
    /* The selection bar rv_gfx.bar draws: its top line, the gradient from
     * `start` at the top to `end` at the bottom, and its bottom line. */
    RV_COLOR_BAR_TOP_LINE,
    RV_COLOR_BAR_START,
    RV_COLOR_BAR_END,
    RV_COLOR_BAR_BOTTOM_LINE,
    RV_COLOR_ROLE_COUNT,
};

/* The stock fonts: the title bar's, the text pages' and the preview pane's
 * large caption. */
enum rv_font { RV_FONT_TITLE, RV_FONT_TEXT, RV_FONT_LARGE, RV_FONT_COUNT };

/* Where rv_gfx.text_in puts the text across its rectangle. */
enum rv_align { RV_ALIGN_LEFT = -1, RV_ALIGN_CENTER, RV_ALIGN_RIGHT };

/* The height of a row drawn by rv_gfx.row, as the stock list rows. */
#define RV_GFX_ROW_HEIGHT 25

/* What a canvas draws with. Valid inside the draw callback only. Grows like
 * rv_api: test a newer entry with RV_HAS. */
struct rv_gfx {
    uint32_t size;
    rv_color (*color)(uint8_t role);
    /* A rectangle in `color`; an alpha below 0xff blends it over what is there. */
    void (*fill)(const struct rv_rect *rect, rv_color color);
    /* `y` is the top of the text line. */
    void (*text)(int x, int y, uint8_t font, rv_color color, const char *utf8);
    int (*measure)(uint8_t font, const char *utf8); /* width in pixels */
    int (*line_height)(uint8_t font);
    /* A bitmap asset with its top left at (x, y); nothing when it is missing.
     * The core decodes it on first use and keeps it until the canvas closes. */
    void (*bitmap)(int x, int y, const struct rv_bitmap *bitmap);
    /* A BMap the extension made or holds itself. */
    void (*bmap)(int x, int y, const void *bmap);
    /* `utf8` wrapped at spaces to `width` from (x, y), at most `lines` lines;
     * returns the y below the last. */
    int (*paragraph)(int x, int y, int width, uint8_t font, rv_color color, const char *utf8,
                     uint32_t lines);
    /* A list row RV_GFX_ROW_HEIGHT tall at `y` across `width`: the label left,
     * `value` (may be NULL) right, on the stock highlight when `highlighted`. */
    void (*row)(int y, int width, const char *label, const char *value, int highlighted);
    /* The top `rows` of a BMap at (x, y); a coverage mask draws in `tint`. */
    void (*bmap_rows)(int x, int y, const void *bmap, int rows, rv_color tint);
    /* bmap_rows for a picture that fades, at `alpha`/255 of its opacity; a
     * coverage mask draws nothing. */
    void (*bmap_rows_blend)(int x, int y, const void *bmap, int rows, uint8_t alpha);
    /* A BMap repeated across `width` columns from (x, y), the last copy cut to fit. */
    void (*bmap_tile)(int x, int y, int width, const void *bmap);
    /* A page in the style of Settings > About: the background, `icon` (a menu
     * bitmap of the extension, NULL for none) on the box of the Settings icons,
     * and `left` and `right` on either side of it. */
    void (*card)(int width, int height, const char *left, const char *right,
                 const struct rv_bitmap *icon);
    /* `utf8` on one line in `rect`: vertically centred, across it by `align`
     * (rv_align). Nothing is clipped to the rectangle. */
    void (*text_in)(const struct rv_rect *rect, uint8_t font, rv_color color, const char *utf8,
                    int8_t align);
    /* Each channel of `from` moved toward `to` by weight/255, rounded to
     * nearest; opaque. For the greys and tints between two roles. */
    rv_color (*mix)(rv_color from, rv_color to, uint8_t weight);
    /* The part of the canvas this draw covers, in canvas coordinates: the
     * whole canvas, or what ui_invalidate_rect asked for cut to it. Drawing
     * outside it is clipped away, so a canvas may skip it. */
    const struct rv_rect *(*clip)(void);
    /* The stock selection bar across `rect`: the RV_COLOR_BAR_* top line, the
     * gradient and the bottom line; under three rows, the gradient alone. */
    void (*bar)(const struct rv_rect *rect);
};

/* RV_INPUT_HOLD: the hold switch moved; `delta` is 1 when it locked, 0 when it
 * unlocked. What the input callback returns for it is ignored.
 * RV_INPUT_LONG_PRESS: `button` has been held for about a second; it comes once,
 * and no release follows it. A long press of Menu is the system's. */
enum rv_input_kind { RV_INPUT_WHEEL, RV_INPUT_PRESS, RV_INPUT_RELEASE, RV_INPUT_HOLD, RV_INPUT_LONG_PRESS };
enum rv_button { RV_BUTTON_CENTER, RV_BUTTON_MENU, RV_BUTTON_PREVIOUS, RV_BUTTON_NEXT, RV_BUTTON_PLAY };

struct rv_input {
    uint8_t kind;
    uint8_t button; /* rv_button for presses and releases */
    int16_t delta;  /* wheel steps, signed */
};

/* Where a dynamic list row shows its detail. */
enum rv_detail {
    RV_DETAIL_BESIDE,        /* on the right of the label, as a setting's value */
    RV_DETAIL_BELOW,         /* a second line under the label */
    RV_DETAIL_BELOW_SUBMENU, /* a second line, and the row shows it opens a screen */
};

/* A stock status symbol at the trailing edge of a dynamic-list row. */
enum rv_row_status {
    RV_ROW_STATUS_NONE,
    RV_ROW_STATUS_PLAYING,
};

/* What a pane shows while a row is highlighted: a caption and a bitmap, or
 * from the callbacks as they are now. An entry's pane is the menu's pane
 * (docs/sdk/menus.md); a list screen's per-row pane is the Settings preview
 * pane beside it, a caption line and an icon on the system's box. */
struct rv_pane {
    const char *caption;
    const struct rv_bitmap *bitmap;
    const char *(*caption_text)(const struct rv_context *ctx);
    const struct rv_bitmap *(*bitmap_for)(const struct rv_context *ctx);
};

/* The parts of a menu pane only some shapes have: the 130 x 128 pane's two
 * more lines, static or computed for line 0 and 1, and the 20 x 16 badge
 * (`pane = "badge"`) before the 146 x 110 pane's count line. */
struct rv_pane_extra {
    const char *lines[2];
    const char *(*line_text)(const struct rv_context *ctx, uint32_t line);
    const struct rv_bitmap *badge;
    const struct rv_bitmap *(*badge_for)(const struct rv_context *ctx);
};

/* A screen the core registers for the extension. `id` is what RV_ROW_PUSH,
 * ui_push and a module's manifest trigger call it, local to the extension.
 * Labels and text are read from the module while the screen is open; nothing
 * is copied. */
struct rv_screen {
    const char *id;
    const char *title;
    uint8_t kind;
    uint8_t layout;     /* rv_layout, list kinds only */
    uint8_t transition; /* rv_transition, slider and canvas only */
    uint8_t reserved[1];
    union {
        struct {
            const struct rv_row *rows;
            uint32_t count;
            const struct rv_pane *panes; /* one per row for the split layout, or NULL */
        } list;
        struct {
            uint16_t key;
            uint16_t reserved;
            const char *const *labels;
            uint32_t count;
        } choice;
        struct {
            const char *const *paragraphs;
            uint32_t count;
        } text;
        struct {
            /* Draws the canvas, `width` by `height` pixels; input returns 1 when
             * consumed, 0 to leave the key to the system. A release goes
             * where its press went whatever input returns for it. */
            void (*draw)(const struct rv_context *ctx, const struct rv_gfx *gfx, int width,
                         int height);
            int (*input)(const struct rv_context *ctx, const struct rv_input *input);
        } canvas;
        struct {
            uint32_t (*count)(const struct rv_context *ctx);
            /* Row `index`; a detail text goes to *detail (NULL for none). */
            const char *(*row)(const struct rv_context *ctx, uint32_t index,
                               const char **detail);
            int (*select)(const struct rv_context *ctx, uint32_t index); /* 1 pops the screen */
            /* The row highlighted when the screen opens; NULL keeps the list's
             * own, which may be a row of the last list shown. */
            uint32_t (*initial)(const struct rv_context *ctx);
            uint8_t detail;                /* rv_detail: where the detail goes */
            /* NULL shows no status; normal and selected artwork follows the row. */
            enum rv_row_status (*status)(const struct rv_context *ctx, uint32_t index);
            const char *empty; /* centered text while count is zero, NULL for none */
        } dynamic;
        struct {
            uint8_t screen; /* rv_stock_screen */
        } stock;
    };
    /* A list in the split layout shows these in the preview pane beside the
     * title: a line of text and an icon on the system's box; NULL shows
     * nothing. A choice screen's line names the highlighted choice instead,
     * as Settings > EQ does. */
    const char *caption;
    const struct rv_bitmap *bitmap;
    /* After the screen was pushed, and after it closed for good: for a canvas
     * after the last draw of its slide-out, so what it draws with can be freed
     * there. Either may be NULL. Not called when the extension unloads under
     * an open screen; unload covers that. */
    void (*opened)(const struct rv_context *ctx);
    void (*closed)(const struct rv_context *ctx);
    /* List and dynamic-list screens: the rows a long press of Select offers,
     * at most 16, before the system's Cancel row. */
    const struct rv_context_row *context;
    uint32_t context_count;
};

/* The menus an entry can add a row to, as bits of its `menus` (RV_MENU_BIT):
 * Main, Music, Extras, Settings and Videos, and the context menus, the ones a
 * long press of Select opens on Now Playing or on a row of a media list: songs
 * (song, playlist and Genius lists), albums, artists, podcast and iTunes U
 * episodes, audiobooks, movies, TV episodes and video playlist entries. Movies
 * and TV episodes have no stock context menu; the core opens one for the
 * extensions' rows. Every menu but album and artist hands its action the
 * item (ctx->item). */
enum rv_menu {
    RV_MENU_MAIN,
    RV_MENU_MUSIC,
    RV_MENU_EXTRAS,
    RV_MENU_SETTINGS,
    RV_MENU_VIDEOS,
    RV_MENU_NOW_PLAYING_CONTEXT,
    RV_MENU_SONG_CONTEXT,
    RV_MENU_ALBUM_CONTEXT,
    RV_MENU_ARTIST_CONTEXT,
    RV_MENU_PODCAST_EPISODE_CONTEXT,
    RV_MENU_ITUNES_U_EPISODE_CONTEXT,
    RV_MENU_AUDIOBOOK_CONTEXT,
    RV_MENU_MOVIE_CONTEXT,
    RV_MENU_TV_EPISODE_CONTEXT,
    RV_MENU_VIDEO_PLAYLIST_CONTEXT,
};
#define RV_MENU_BIT(menu) ((uint16_t)1 << (menu))

/* The stock pane a music or video item shows while its row is highlighted,
 * as the stock Music and Videos rows do: the note or the film picture with the
 * library's song or video count beneath, the artwork when Menu Covers shows it
 * and "No Music" or "No Videos" for an empty library. A Music row is a music
 * item and a Videos row a video item; a Main row says which it is in `preview`. */
enum rv_preview {
    RV_PREVIEW_NONE,
    RV_PREVIEW_MUSIC,
    RV_PREVIEW_VIDEOS,
};

/* A row the extension adds to every menu in `menus`, at least one. `after` is
 * a stock row of that menu from rv_menus.h (RV_SETTINGS_LEGAL), 0 for the end
 * and the only value for several menus. While the row is highlighted the
 * menu's pane shows `pane`: under Settings the label, the caption as the line
 * below and the bitmap, an icon, on the box the system draws behind stock
 * Settings icons unless `plain_icon` is set; under Main, Music, Videos and
 * Extras the picture with the caption, or without one the label, in the pane
 * shape the picture's size picks (docs/sdk/menus.md), with `extra` where the
 * shape has room for it. A music or video item (a Music or Videos row, a Main
 * row with `preview`) with none of `pane`, `extra` and `caption_setting`
 * shows the stock pane of its kind instead (rv_preview). With `shown_by`, the
 * key of a declared bool setting, the row shows while that setting is on;
 * under Main and Music, Settings > Main Menu and Music Menu list the row and
 * flip it. Without, the row always shows. */
struct rv_menu_entry {
    uint16_t menus;    /* RV_MENU_BIT of each rv_menu */
    uint16_t shown_by; /* a bool setting key, 0 for always shown */
    uint32_t after;
    struct rv_row row;
    const struct rv_pane *pane;
    const struct rv_pane_extra *extra;
    /* Context menus: asked each time the menu opens, with the item and row it
     * opened on; 0 leaves the row out. NULL always shows it. */
    int (*available)(const struct rv_context *ctx);
    /* With no caption in `pane`, the key of a declared setting whose value
     * the caption shows: its label, or On or Off for a bool or an unlabelled
     * int, following the setting when it changes. */
    uint16_t caption_setting;
    /* Main rows only: the stock pane the row shows without a pane of its own.
     * A Music or Videos row has its menu's. */
    uint8_t preview;    /* rv_preview */
    uint8_t plain_icon; /* 1: the pane shows its bitmap without the system's box behind it */
};

/* Returned by a module's entry; everything it points to lives as long as the
 * module is loaded. The main module's declares everything. A screen or
 * manual module's declares only its own hooks, screens and unload: the core
 * binds and registers them when it loads and drops them when it unloads, so
 * a screen module's screens, and the code they call, take no memory until one
 * of them is opened. Its screen ids are the ones its manifest entry lists. */
struct rv_descriptor {
    uint32_t size; /* sizeof(struct rv_descriptor) when built */
    const struct rv_hook *hooks;
    uint32_t hook_count;
    const struct rv_setting *settings;
    uint32_t setting_count;
    void (*unload)(void); /* before the hooks are unbound; may be NULL */
    /* After a setting changed, whether from a row, a choice or slider screen or
     * the extension's own write; RV_SETTING_KEY_ALL once the main module has
     * loaded and after Reset All Settings. UI task; may be NULL. */
    void (*setting_changed)(uint16_t key);
    const struct rv_screen *screens;
    uint32_t screen_count;
    const struct rv_menu_entry *entries;
    uint32_t entry_count;
    /* A table of the module's own functions for the extension's other modules,
     * read with module_service; may be NULL. */
    const void *service;
};

struct rv_api {
    uint32_t size;    /* bytes of this table */
    uint32_t version; /* RV_API_VERSION of the core */

    /* Bind beyond the descriptor; unbind only what hook_bind bound (the
     * descriptor's hooks are unbound by the loader). A handler the extension
     * already has at the site returns RV_ALREADY_BOUND and binds nothing. A
     * handler may unbind itself once its hook_next has returned: the core
     * reads nothing of the hook after the call. */
    int (*hook_bind)(rv_extension_handle self, const struct rv_hook *hook);
    int (*hook_unbind)(rv_extension_handle self, const struct rv_hook *hook);
    /* Continue a filter chain; unused trailing arguments are 0. */
    uint32_t (*hook_next)(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     uint32_t e, uint32_t f);

    /* A bool or int setting: its value, the declared fallback when the store
     * has none. setting_set_int clamps to the declared minimum and maximum (0
     * and 1 for a bool; a multi-choice keeps only its labels' bits) and stores;
     * when the value changed it redraws the rows that show it and calls
     * setting_changed. Inside the main module's entry the declarations are
     * not known yet: no fallback, no clamp, no callback. */
    int32_t (*setting_get_int)(rv_extension_handle self, uint16_t key);
    int (*setting_set_int)(rv_extension_handle self, uint16_t key, int32_t value);
    /* Any setting as bytes, for strings and blobs: setting_read copies at most
     * `capacity` bytes and returns how many, 0 when absent; setting_write
     * stores at most 255 bytes and calls setting_changed when they changed.
     * Neither applies a fallback. The core's keys are refused. */
    uint32_t (*setting_read)(rv_extension_handle self, uint16_t key, void *out, uint32_t capacity);
    int (*setting_write)(rv_extension_handle self, uint16_t key, uint8_t type, const void *data,
                         uint32_t bytes);
    void (*settings_flush)(void);

    /* Zeroed memory from the Apple heap, counted against the extension. What
     * is not freed when the extension unloads is freed after its unload
     * callbacks, so nothing outside it may point there by then. */
    void *(*alloc)(rv_extension_handle self, uint32_t bytes);
    void (*free)(rv_extension_handle self, void *block);

    /* Manual-trigger modules by their index in the manifest. module_load
     * returns RV_OK at once for a loaded module; module_unload releases it,
     * and it unloads once no screen of it is open and its preload class under
     * Settings > Extensions > Optimize does not keep it (docs/sdk/modules.md). */
    int (*module_load)(rv_extension_handle self, uint32_t module);
    void (*module_unload)(rv_extension_handle self, uint32_t module);

    /* Kept for the emulator; nothing on the device shows it. */
    void (*log)(rv_extension_handle self, const char *text);

    /* Screens by id, from code on the UI task: rows, timers, hooks there.
     * ui_push opens one above the top screen, loading the screen's module
     * first; `arg` reaches the screen's callbacks as ctx->arg. RV_NOT_FOUND
     * for an unknown id. ui_pop closes the top screen when it is the
     * extension's; refresh redraws rows whose text changed; invalidate redraws
     * the canvas on top when it is the extension's. */
    int (*ui_push)(rv_extension_handle self, const char *screen, void *arg);
    void (*ui_pop)(rv_extension_handle self);
    void (*ui_refresh)(rv_extension_handle self, const char *screen);
    void (*ui_invalidate)(rv_extension_handle self);

    const struct rv_debug_info *(*debug_info)(void); /* rv_debug.h */

    /* Assets from manifest.toml by name. asset_load returns the decoded asset in
     * a block from alloc, *bytes its size, or NULL when it is missing, damaged or
     * does not fit; free it with free. A preloaded asset is copied from RAM.
     * asset_read copies up to `bytes` from `offset` of an asset stored raw
     * (compress = false), *got the count; RV_BAD_ARGUMENT for a compressed one,
     * RV_NOT_FOUND when it is missing or unreadable. */
    void *(*asset_load)(rv_extension_handle self, const char *name, uint32_t *bytes);
    int (*asset_read)(rv_extension_handle self, const char *name, uint32_t offset, void *out,
                      uint32_t bytes, uint32_t *got);

    /* Text into `out`, NUL-terminated within `capacity`; the length. A number
     * with thousands separators ("12,345"); bytes as KB or MB with one decimal
     * ("6.0 KB"). */
    uint32_t (*format_number)(char *out, uint32_t capacity, uint32_t value);
    uint32_t (*format_size)(char *out, uint32_t capacity, uint32_t bytes);

    /* Calls `callback(arg)` on the UI task after `ms`, and every `ms` after
     * that when `repeat` is set; NULL when the heap is full. timer_stop ends
     * it; a one-shot timer ends by itself once it fired, and must not be
     * stopped after that. The core stops a module's timers before the module
     * unloads. Both are for the UI task. */
    struct rv_timer *(*timer_start)(rv_extension_handle self, uint32_t ms, int repeat,
                                    void (*callback)(void *arg), void *arg);
    void (*timer_stop)(rv_extension_handle self, struct rv_timer *timer);

    /* ui_invalidate for `rect` of the canvas, in its coordinates; the draw
     * callback runs as for ui_invalidate. */
    void (*ui_invalidate_rect)(rv_extension_handle self, const struct rv_rect *rect);
    /* Reads the panes that last showed a row of the extension again, and the
     * per-row pane of its list screen on screen: caption_text, line_text,
     * bitmap_for, badge_for and caption_setting. Nothing happens while no pane
     * has shown one. */
    void (*ui_pane_refresh)(rv_extension_handle self);

    /* Files in the extension's data folder, /Revola/Data/Extensions/<id>/.
     * `name` is a path inside it ("presets/rock.bin") without "..". file_open
     * with RV_FILE_WRITE creates the folders above it and a missing file; to
     * replace a file, delete it first. file_read sets *got, short at the end.
     * folder_list calls `each` for every entry of a folder ("" for the data
     * folder), nothing when it is missing. The core closes what is still open
     * when the extension unloads. RV_BAD_ARGUMENT for a bad name or handle,
     * RV_NOT_FOUND when the file layer refuses. UI task. */
    int (*file_open)(rv_extension_handle self, const char *name, enum rv_file_mode mode,
                     struct rv_file **file);
    int (*file_read)(rv_extension_handle self, struct rv_file *file, void *out, uint32_t bytes,
                     uint32_t *got);
    int (*file_write)(rv_extension_handle self, struct rv_file *file, const void *data,
                      uint32_t bytes);
    int (*file_seek)(rv_extension_handle self, struct rv_file *file, uint32_t offset);
    void (*file_close)(rv_extension_handle self, struct rv_file *file);
    int (*file_delete)(rv_extension_handle self, const char *name);
    int (*folder_list)(rv_extension_handle self, const char *name,
                       void (*each)(const char *entry, int is_folder, void *arg), void *arg);

    /* Masks IRQ and FIQ and returns the state before; irq_restore puts that
     * state back. For a handoff to an interrupt or audio handler that must not
     * see it half done, such as swapping a coefficient table; keep the masked
     * stretch to a few instructions. Any task. */
    uint32_t (*irq_save)(void);
    void (*irq_restore)(uint32_t state);

    /* Highlight a dynamic-list row, clamped to its current count. If covered
     * by another screen, apply on return. An empty list has no selection. UI task. */
    void (*ui_select)(rv_extension_handle self, const char *screen, uint32_t row);

    /* A string resource id that resolves to a copy of `text`, for a place in
     * Apple's UI that names its text by resource id, such as a row of a table a
     * hook extends. One of the extension's 4,096 ids; the copy is freed with
     * the extension. 0 when the ids or the heap run out. UI task. */
    uint32_t (*string_resource)(rv_extension_handle self, const char *text);

    /* Files of the content folders the manifest names under /Revola/
     * (`content = "Themes"`): what a listener copies onto the iPod for the
     * extension to find. `name` starts with the folder ("Themes/example.rvt");
     * content_open opens for reading, and file_read, file_seek and file_close
     * work on the handle, closed on unload like a data file. content_list is
     * folder_list for a folder there ("Themes" for the folder itself). A folder
     * has one owner: an extension naming one another holds does not load.
     * RV_BAD_ARGUMENT for a bad name or a folder the manifest does not name,
     * RV_NOT_FOUND when the file layer refuses. UI task. */
    int (*content_open)(rv_extension_handle self, const char *name, struct rv_file **file);
    int (*content_list)(rv_extension_handle self, const char *name,
                        void (*each)(const char *entry, int is_folder, void *arg), void *arg);

    /* An asset stored raw (compress = false) as a file handle for file_read,
     * file_seek and file_close, with offsets from the asset's start, for
     * streaming through it without reopening the extension's file on every
     * read as asset_read does. RV_BAD_ARGUMENT for a compressed asset or one
     * whose preload is not never (asset_load, asset_read), RV_NOT_FOUND when it
     * is missing or unreadable. UI task. */
    int (*asset_open)(rv_extension_handle self, const char *name, struct rv_file **file);

    /* The colour roles canvases draw with (rv_gfx.color), RV_COLOR_ROLE_COUNT
     * values that stay valid for as long as they are set: in the main module
     * or the heap, since the core clears them when the module holding them
     * unloads. NULL restores the stock colours. One extension at a time:
     * RV_ALREADY_BOUND while another holds them. UI task. */
    int (*ui_set_colors)(rv_extension_handle self, const rv_color *roles);
    /* Redraws everything on screen, for a change that touched colours or
     * artwork the stock views already drew. UI task. */
    void (*ui_redraw)(rv_extension_handle self);

    /* hook_next for a site with seven values: four in registers, two Apple
     * passes on the stack and one the site adds after them, such as the view
     * a blit handler needs; the handler receives it as its last argument. A
     * site with fewer stack values passes the added one through hook_next. */
    uint32_t (*hook_next7)(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t e, uint32_t f, uint32_t g);

    /* The `service` of the module at `module` in the manifest, 0 for main, or
     * NULL while it is not loaded. Modules share no symbols, so this is how
     * one calls another: main reaches a manual module between its module_load
     * and module_unload, and that module reaches main's table, valid for as
     * long as the extension is loaded, from its own entry. One module owns
     * each load and unload pair. */
    const void *(*module_service)(rv_extension_handle self, uint32_t module);

    /* Before the core decodes a bitmap asset for a Settings pane or a canvas
     * it asks `provider` (rv_theme.h; any function may be NULL, and a
     * replacement needs replacement and replacement_read) for a replacement,
     * and offers what it decoded itself for adjustment. The table and its
     * functions live in the main module or the heap: the core releases the
     * provider when the module or block holding the table goes.
     * A replacement is counted to the holder's memory. One extension at a
     * time: RV_ALREADY_BOUND while another holds it; NULL releases. Setting
     * or releasing it decodes the bitmaps on screen again. UI task. */
    int (*asset_set_provider)(rv_extension_handle self,
                              const struct rv_asset_provider *provider);

    /* The booted firmware image, /Revola/Firmware/revola.bin, open for reading
     * as a handle for file_read, file_seek and file_close, closed on unload
     * like a data file: where a theme reads the stock artwork from.
     * RV_NOT_FOUND when the file layer refuses. UI task. */
    int (*firmware_open)(rv_extension_handle self, struct rv_file **file);
};

/* Stops `*timer` when it runs and clears it; for a timer the extension keeps. */
static inline void rv_timer_cancel(const struct rv_api *api, rv_extension_handle self,
                                   struct rv_timer **timer)
{
    if (*timer)
        api->timer_stop(self, *timer);
    *timer = NULL;
}

/* Every module exports this as rv_module_entry. The main module returns its
 * descriptor; another module may return NULL when it declares nothing. */
typedef const struct rv_descriptor *(*rv_module_entry_fn)(const struct rv_api *api, rv_extension_handle self);

#endif
