/* SPDX-License-Identifier: GPL-3.0-only
 * Native UI resource construction adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */
#pragma once
#include "screen.h"
#include "osos.h"
#include "../../rvx-sdk/rv_api.h"
#include "../../rvx-sdk/osos-2.0.5.h"
#define NATIVE_CALL(name) ((__typeof__(&name))ADDR_##name)

/* screen.h mirrors the SDK's UI structs field for field. */
_Static_assert(sizeof(struct row) == sizeof(struct rv_row) &&
                   offsetof(struct row, id) == offsetof(struct rv_row, id) &&
                   offsetof(struct row, stock) == offsetof(struct rv_row, stock) &&
                   sizeof(struct pane) == sizeof(struct rv_pane) &&
                   offsetof(struct pane, plain) ==
                       offsetof(struct rv_pane, plain_icon) &&
                   sizeof(struct context_row) == sizeof(struct rv_context_row) &&
                   sizeof(struct menu_entry) == sizeof(struct rv_menu_entry) &&
                   offsetof(struct menu_entry, host) ==
                       offsetof(struct rv_menu_entry, host) &&
                   sizeof(struct screen) == sizeof(struct rv_screen),
               "RVX UI ABI");

extern const u32 rvx_clear_bitmap[];

struct core_block {
    const uint8_t *data;
    uint32_t bytes;
    uint32_t kind;
};

uint32_t core_blocks_count(const void *list);
/* Block `index` of `list`; 0 when there is none. */
int core_block_at(const void *list, uint32_t index, struct core_block *out);
/* The block whose data starts with the word `id` (menu rows keyed by MenuRow_id). */
const uint8_t *core_block_with_word(const void *list, uint32_t offset, uint32_t id,
                                    uint32_t *bytes);
uint32_t core_word_at(const void *data, uint32_t offset);

/* Writer for resources and event streams. With `out` NULL it only counts, so a
 * buffer is sized by a first pass and filled by a second. */
struct core_writer {
    uint8_t *out;
    uint32_t at;
};

void core_write_bytes(struct core_writer *w, const void *data, uint32_t bytes);
void core_write_word(struct core_writer *w, uint32_t value);
/* Overwrites the word at `at`, for a count written before its items. */
void core_write_word_at(struct core_writer *w, uint32_t at, uint32_t value);
/* A copy of `data` with words changed; `words` are {offset, value} pairs. */
void core_write_copy(struct core_writer *w, const void *data, uint32_t bytes,
                     const uint32_t *words, uint32_t count);
void core_write_string(struct core_writer *w, const char *text);
/* Starts a block; returns what core_write_block_end needs. */
uint32_t core_write_block_begin(struct core_writer *w, uint32_t kind);
void core_write_block_end(struct core_writer *w, uint32_t begin);
/* One event: name, handler (may be ""), optional action with its arguments. */
void core_write_event(struct core_writer *w, const char *name, const char *handler,
                      const char *action, const char *const *arguments, uint32_t count);

/* A property source: the title (destination 1) or inline value (10) of a row or
 * view comes from Settings model property `property`, which the core answers. */
void core_set_source(uint32_t id, uint32_t property, uint32_t destination);
/* A title source with a second binding: `second` for `destination`. */
void core_set_source2(uint32_t id, uint32_t title, uint32_t second,
                      uint32_t destination);
void core_row_event_name(char *name, unsigned bytes, uint32_t item, const char *suffix);
char *core_put_number(char *out, const char *end, uint32_t value);
char *core_put_text(char *out, const char *end, const char *value);

/* Sources for `count` consecutive views of a template, applied to the instance
 * that uses the template so the views are rebound on every display: a view the
 * stock instance binds gets the source in place of its own, the others are
 * bound anew. */
struct view_sources {
    uint32_t view;   /* first view id */
    uint32_t source; /* first source id */
    uint32_t count;
};

/* A layout clone: the stock layout's instances with the title view's source,
 * the item table and one template or VLyt replaced. */
struct layout_clone {
    uint32_t stock;
    uint32_t layout;     /* id of the clone */
    uint32_t title_view; /* status bar view whose source becomes `title` */
    uint32_t title;
    uint32_t items; /* ITEM table for the list binding, 0 to keep */
    uint32_t from,
        to; /* template or VLyt id replaced, 0 for none; a new VLyt id needs a
                            TEVT stored under it, or loading the layout's events aborts */
    const struct view_sources *extra; /* sources for the views of template `to` */
    uint32_t
        bindings; /* VLyt the instance of template `from` gets, 0 to keep its own */
};

/* 1 for the list kinds whose one row expands by index. */
static inline int core_lists_indexed(uint8_t kind)
{
    return kind == RV_SCREEN_KIND_CHOICE || kind == RV_SCREEN_KIND_MULTI_CHOICE ||
           kind == RV_SCREEN_KIND_DYNAMIC_LIST;
}

/* 1 for every list kind: the ones with two layouts. */
static inline int core_lists_kind(uint8_t kind)
{
    return kind == RV_SCREEN_KIND_LIST || core_lists_indexed(kind);
}

/* 1 for a list whose rows each bring a pane: the pane shows the highlighted row's. */
static inline int core_lists_per_row_pane(const struct screen *d)
{
    return d->kind == RV_SCREEN_KIND_LIST && d->list.panes;
}

/* Ids the split layout of a list screen takes after the full layout's: its
 * screen, its layout, the pane, four layout clones, the pane's caption, icon,
 * box and title sources (the title names the highlighted row when the pane
 * follows it), then the other split layout (the pane's box shown or hidden,
 * the opposite of the first's), its pane and four layout clones. */
#define CORE_LIST_SPLIT_IDS 17
#define CORE_LIST_PANE_CAPTION(screen) ((screen) + 7)
#define CORE_LIST_PANE_TITLE(screen) ((screen) + 10)
#define CORE_LIST_PANE_OTHER(screen) ((screen) + 11)

/* 1 for a row pane shown without the box: a plain icon or no bitmap. */
static inline int core_lists_plain_pane(const struct pane *p)
{
    return !p || p->plain || (!p->bitmap && !p->image);
}

/* 1 for a hosted entry shown without the box: the entry's plain icon or no
 * bitmap; its pane's plain flag is for a list's own rows. */
static inline int core_lists_plain_entry(const struct menu_entry *e)
{
    return e->plain || !e->pane || (!e->pane->bitmap && !e->pane->image);
}

/* The stock Settings rows a list screen may take (RV_ROW_STOCK): one slot per
 * row of the Settings table, whose ids are contiguous. Each takes
 * CORE_LIST_STOCK_IDS ids after the rows' for the clone of its Settings
 * layout, which the split layout switches to while the row is highlighted. */
#define CORE_SETTINGS_ROWS 18
_Static_assert(RES_SettingsMenu_ResetSettings_Item - RES_SettingsMenu_About_Item ==
                   CORE_SETTINGS_ROWS - 1,
               "Settings row ids");
#define CORE_LIST_STOCK_IDS 6

#define CORE_CLONE_SCRATCH_BYTES 1536
/* First row item id (list), paragraph source id (text), bar source id (slider)
 * or message source id (dialog). */
#define CORE_SCREEN_ROWS(s) ((s)->id + 8)
#define ROW_BYTES 0x98
/* The Legal row, or with `value` Sound Check's: the ROW_BYTES rows of an
 * extension's list copy them. */
const uint8_t *core_lists_prototype(int value);
/* Apple's row `id` of the stock Settings table, the bytes its lists and menu
 * registry hold; NULL for no such row. */
const uint8_t *core_lists_settings_row(uint32_t id);
/* How many stock Settings rows (RV_ROW_STOCK) a list screen declares. */
uint32_t core_lists_stock_count(const struct screen *d);

struct core_screen {
    struct core_screen *next;
    Pkg *owner;
    uint32_t id, ids, key, kind, built, text_template;
    const struct screen *declaration;
    char name[];
};

static inline uint32_t core_lists_context_ids(const struct screen *d)
{
    return d->context_count ? 5 + (d->context_count < 16 ? d->context_count : 16) : 0;
}

#define CORE_LIST_CONTEXT(s)                                                           \
    ((s)->id + (s)->ids - CORE_LIST_SPLIT_IDS -                                        \
     core_lists_context_ids((s)->declaration))

enum core_template { CORE_TEMPLATE_SLIDER, CORE_TEMPLATE_EMPTY, CORE_TEMPLATES };

uint32_t core_template(enum core_template kind);
uint32_t core_pane_template(void);
/* A copy of a stock template's views, then of `second`'s when set, under new
 * ids and built as a template; view `replaced` (-1 for none) becomes the Games
 * pane's image view in its box. Stays for the session. 0 on failure. */
uint32_t core_templates_copy(uint32_t stock, uint32_t second, int replaced);
uint32_t core_text_template(struct core_screen *s);
uint32_t core_resource_take(uint32_t count);
uint32_t core_clone_ids(const struct screen *d);
uint32_t core_lists_screen_id(const struct core_screen *s, int split);
int core_clone_list(struct core_screen *s, int split);
int core_clone_text(struct core_screen *s);
int core_clone_slider(struct core_screen *s);
int core_clone_dialog(struct core_screen *s);
int core_clone_stock(struct core_screen *s);
int core_clone_layout(const struct layout_clone *, uint32_t *, uint8_t *, uint32_t);

/* A layout cloned from a stock one, for a screen's layout list. */
struct core_layout_entry {
    uint32_t stock, clone;
};

/* The screen's own resources: SCST from `controller_screen`, SLst from
 * `layouts_screen` with the first layout replaced and then `count` more
 * `layouts`, each as a copy of its stock layout's entry in the list of
 * `entries_screen`, CEVT from `events`. */
int core_clone_screen(uint32_t screen, uint32_t layout, uint32_t controller_screen,
                      uint32_t layouts_screen, uint32_t entries_screen,
                      const struct core_layout_entry *layouts, uint32_t count,
                      const void *events, uint32_t event_bytes, uint8_t *scratch,
                      uint32_t scratch_bytes);
void core_clone_layout_events(struct core_writer *);
/* Names a layout "Rv<id>" (core_screens_handler_name) in Apple's name map, for
 * a navigator.SwitchLayout binding. The entry lives in Apple's heap for the
 * boot; naming the same id again changes nothing. */
void core_clone_name_layout(uint32_t layout);
/* Copies the events of a stock CEVT or SEVT stream named `name`, or whose
 * names start with it unless `whole`, verbatim and without a count; returns
 * how many. */
uint32_t core_clone_copy_events(struct core_writer *w, const void *stream,
                                const char *name, int whole);

/* An event of a stock stream found by name: its handler and its action's
 * first argument, as unterminated text with lengths; NULL for none. */
struct core_event {
    const uint8_t *handler, *argument;
    uint32_t handler_bytes, argument_bytes;
};

int core_clone_find_event(const void *stream, const char *name, struct core_event *out);
/* The layout a list screen declared by `d` opens on: its own, or the first
 * row's stock layout clone when a split list starts on a stock row. */
uint32_t core_lists_initial_layout(const struct core_screen *s, const struct screen *d,
                                   int split);
/* The split layout a hosted row whose pane is `plain` (no box) switches to:
 * the list's first, or the other one when the list has built it. */
uint32_t core_lists_pane_layout(const struct core_screen *s, const struct screen *d,
                                int plain);
int core_list_empty(struct core_screen *, uint32_t, uint32_t *, uint8_t *, uint32_t);
uint32_t core_screen_menu_events(struct core_writer *, const struct core_screen *);
int core_screen_menu_build(const struct core_screen *, uint8_t *, uint32_t);
void core_screens_handler_name(char *, uint32_t);

/* The screen record whose ITEM table is `table`; NULL for a stock table. */
const struct core_screen *rvx_native_table_screen(uint32_t table);
/* The list screen record one of whose layouts has the screen id `screen`, with
 * *split 1 for the split layout; NULL for a stock screen. */
const struct core_screen *rvx_native_screen_of(uint32_t screen, int *split);
/* The record of `owner`'s screen declared by `d`; NULL until first pushed. */
const struct core_screen *rvx_native_find(const Pkg *owner, const struct screen *d);

/* Hosted menu rows (menus.c). */
struct installed;
/* The row at a visible position of the list screen `s` declared by `d`: a
 * declared row, its index in *index and NULL; or the guest row there. Past
 * the last row *index is the row count. */
const struct installed *rvx_guest_at(const struct core_screen *s,
                                     const struct screen *d, uint32_t position,
                                     uint32_t *index);
/* The guests shown in the list screen `s`. */
uint32_t rvx_guest_count(const struct core_screen *s);
/* The position of declared row `index` in `s`, past the guests before it and
 * short of the hidden stock rows. */
uint32_t rvx_guest_position(const struct core_screen *s, const struct screen *d,
                            uint32_t index);
/* 1 when a guest of the list screen `s` needs the split layout whose pane
 * hides the box when the list's first shows it (`main_plain` 0), or shows it
 * when the first hides it. */
int rvx_guest_other_needed(const struct core_screen *s, int main_plain);
/* Stock Settings rows moved into list screens (menus.c): whether Apple shows
 * row `item`, and how many of the stock rows of `d` it hides. */
int rvx_stock_shown(uint32_t item);
uint32_t rvx_stock_hidden(const struct screen *d);
/* A guest's pane fields as its host's split layout asks them: 0 the caption,
 * 1 the icon, 2 the box behind it, 3 the label. */
int rvx_guest_pane_property(const struct installed *i, void *controller, uint32_t field,
                            uint32_t type, uintptr_t *out);
/* Runs the entry row the handler name `action` ("RVX<item>") names, on
 * `controller`; 1 when it names one, also for a support query. `stock` for
 * a call from a stock controller's action hook, whose context menu item the
 * row may take. */
int rvx_menu_action(void *controller, const char *action, uint32_t arg, int stock);

static inline void native_set(uint32_t type, uint32_t id, const void *data,
                              uint32_t size)
{
    osos_resource_set(rvx_bank, type, id, data, size);
}

void core_clone_set_style(uint32_t layout, uint32_t template, int16_t style);
int16_t core_templates_enter_style(uint32_t template, uint32_t kind);
