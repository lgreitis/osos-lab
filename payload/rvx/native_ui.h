/* SPDX-License-Identifier: GPL-3.0-only
 * Native UI resource construction adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */
#pragma once
#include "screen.h"
#include "osos.h"
#include "../../rvx-sdk/rv_api.h"
#include "../../rvx-sdk/osos-2.0.5.h"
#define NATIVE_CALL(name) ((__typeof__(&name))ADDR_##name)

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
 * screen, its layout, the pane, four layout clones, and the pane's caption,
 * icon, box and title sources (the title names the highlighted row when the
 * pane follows it). */
#define CORE_LIST_SPLIT_IDS 11
#define CORE_LIST_PANE_CAPTION(screen) ((screen) + 7)
#define CORE_LIST_PANE_TITLE(screen) ((screen) + 10)

#define CORE_CLONE_SCRATCH_BYTES 1536
#define CORE_SCREEN_ROWS(s) ((s)->id + 8)

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
uint32_t core_text_template(struct core_screen *s);
uint32_t core_resource_take(uint32_t count);
uint32_t core_clone_ids(const struct screen *d);
uint32_t core_lists_screen_id(const struct core_screen *s, int split);
int core_clone_list(struct core_screen *s, int split);
int core_clone_text(struct core_screen *s);
int core_clone_slider(struct core_screen *s);
int core_clone_layout(const struct layout_clone *, uint32_t *, uint8_t *, uint32_t);
int core_clone_screen(uint32_t, uint32_t, uint32_t, uint32_t, const void *, uint32_t,
                      uint8_t *, uint32_t);
void core_clone_layout_events(struct core_writer *);
int core_list_empty(struct core_screen *, uint32_t, uint32_t *, uint8_t *, uint32_t);
uint32_t core_screen_menu_events(struct core_writer *, const struct core_screen *);
int core_screen_menu_build(const struct core_screen *, uint8_t *, uint32_t);
void core_screens_handler_name(char *, uint32_t);

static inline void native_set(uint32_t type, uint32_t id, const void *data,
                              uint32_t size)
{
    osos_resource_set(rvx_bank, type, id, data, size);
}

void core_clone_set_style(uint32_t layout, uint32_t template, int16_t style);
int16_t core_templates_enter_style(uint32_t template, uint32_t kind);
