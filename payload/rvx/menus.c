/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_ui.h"
#include "runtime.h"
#include "osos.h"
#include "cfw.h"

void *rvx_bank;

static struct installed {
    struct installed *next;
    const struct menu_entry *entry;
    Pkg *pkg;
    u32 item, source_id;
    void *row[5];
    char action[16];
    /* A hosted entry's place: the host, its main-module list screen and the
     * declared row the guest follows; host NULL while the row is in its stock
     * menu. host_row is the copy in the host's table, built once. */
    Pkg *host;
    const struct screen *host_screen;
    u32 host_index;
    void *host_row;
} *installed;

enum {
    MENU_MAIN = 1u << 0,
    MENU_MUSIC = 1u << 1,
    MENU_EXTRAS = 1u << 2,
    MENU_SETTINGS = 1u << 3,
    MENU_VIDEOS = 1u << 4,
    MENU_PLAYING = 1u << 5,
    MENU_SONG = 1u << 6,
    MENU_ALBUM = 1u << 7,
    MENU_ARTIST = 1u << 8,
    MENU_PODCAST = 1u << 9,
    MENU_UNIVERSITY = 1u << 10,
    MENU_BOOK = 1u << 11,
    MENU_MOVIE = 1u << 12,
    MENU_TV = 1u << 13,
    MENU_VIDEO_PLAYLIST = 1u << 14,
    MENU_CONTEXT = 0x7fe0
};

static const u32 tables[] = {0x1a, 0x1b, 2, 0x41, 0x33};
static u32 last[5], menu_mask;
static const void *sound_check_row;
static u32 sound_check_size;
const u32 rvx_clear_bitmap[] = {0x1888, 0x00200004, 0, 0, 1, 1, 4, 0};

int rvx_is_pane_icon(const u32 *image, u32 bytes)
{
    return image && bytes == 28 + 80 * 121 && image[0] == 8 && image[1] == 0x00080050 &&
           image[2] == 0 && image[3] == 0 && image[4] == 121 && image[5] == 78 &&
           image[6] == bytes - 28;
}

static struct context media;
static struct osos_media_item media_item;

static void install(Pkg *p, const struct menu_entry *e, u32 id)
{
    struct installed *i = osos_import_alloc(sizeof *i);
    if (!i)
        return;
    menu_mask |= e->menus;
    i->pkg = p;
    i->entry = e;
    i->item = id;
    i->source_id = id + 1;
    u32 source[] = {1,
                    12,
                    Resource_SORC,
                    Service_String,
                    id + 2,
                    SourceDestination_Title,
                    12,
                    Resource_SORC,
                    Service_Settings,
                    i->source_id,
                    SourceDestination_InlineValue};
    if ((e->host || (e->menus & MENU_SETTINGS)) && e->row.kind == RV_ROW_KIND_SETTING)
        source[0] = 2;
    const char *label = e->row.label ? e->row.label : "";
    unsigned len = 0;
    while (label[len])
        len++;
    osos_resource_set(rvx_bank, 0x53747220, id + 2, label, len + 1);
    osos_resource_set(rvx_bank, 0x534f5243, i->source_id, source,
                      (1 + 5 * source[0]) * sizeof(u32));
    i->action[0] = 'R';
    i->action[1] = 'V';
    i->action[2] = 'X';
    core_put_number(i->action + 3, i->action + sizeof i->action, id);
    i->host = NULL;
    i->host_row = NULL;
    i->next = NULL;
    struct installed **tail = &installed;
    while (*tail)
        tail = &(*tail)->next;
    *tail = i; /* rows keep their declaration order */
}

static unsigned menu_events(const struct installed *i, unsigned slot, uint8_t *buffer)
{
    char name[48];
    struct core_writer w = {buffer, 0};
    core_write_word(&w, 2);
    core_row_event_name(name, sizeof name, i->item, ".chosen");
    core_write_event(&w, name, i->action, NULL, NULL, 0);
    core_row_event_name(name, sizeof name, i->item, ".delayedselected");
    const struct menu_entry *entry = i->entry;
    if (!entry->pane && !entry->extra && !entry->caption &&
        (slot == 1 || slot == 4 || (slot == 0 && entry->preview))) {
        const char *handler = slot == 4 || (slot == 0 && entry->preview == 2)
                                  ? "HandleVideosHilited"
                                  : "HandleMusicHilited";
        core_write_event(&w, name, handler, NULL, NULL, 0);
    } else {
        char handler[16] = "PR";
        core_put_number(handler + 2, handler + sizeof handler, i->item);
        const char *layout = core_panes_name(slot);
        core_write_event(&w, name, handler, "navigator.SwitchLayout", &layout, 1);
    }
    return w.at;
}

static int setting_on(const struct installed *i)
{
    return !i->entry->shown || rvx_get_int(i->pkg, i->entry->shown);
}

/* Whether `i` is hosted by the list screen `s`. */
static int guest_of(const struct installed *i, const struct core_screen *s)
{
    return i->host && i->host == s->owner &&
           cfw_string_equal(s->name, i->host_screen->id);
}

/* The events of a guest row on a layout of its host: chosen, and on the split
 * layout of a list with a pane per row the delayed selection that names the
 * pane's caption handler and switches to the list's layout with or without
 * the box, as the host's own rows do (native_lists.c). */
static unsigned host_events(const struct installed *i, const struct core_screen *host,
                            u32 screen, int split, uint8_t *buffer)
{
    char name[48], handler[11], layout[11];
    const char *const arguments[] = {layout};
    struct core_writer w = {buffer, 0};
    int pane = split && core_lists_per_row_pane(i->host_screen);
    core_write_word(&w, pane ? 2 : 1);
    core_row_event_name(name, sizeof name, i->item, ".chosen");
    core_write_event(&w, name, i->action, NULL, NULL, 0);
    if (pane) {
        core_row_event_name(name, sizeof name, i->item, ".delayedselected");
        core_screens_handler_name(handler, CORE_LIST_PANE_CAPTION(screen));
        core_screens_handler_name(
            layout, core_lists_pane_layout(host, i->host_screen,
                                           core_lists_plain_entry(i->entry)));
        core_write_event(&w, name, handler, "navigator.SwitchLayout", arguments, 1);
    }
    return w.at;
}

/* The id a row appended after row `index` of a host list follows: the host
 * row's item id, or a claimed stock row's own, which its copy keeps. */
static u32 host_anchor(const struct core_screen *s, const struct screen *d, u32 index)
{
    const struct row *row = d->list.rows + index;
    return row->kind == RV_ROW_KIND_STOCK ? row->stock
                                          : CORE_SCREEN_ROWS(s) + 2 * index;
}

/* Gives a hosted row its place when the extension its entry names started
 * and its main module declares the list screen and the row: after the row
 * with the id named, or the last. Without, the row stays in its stock menu.
 * Extensions start at boot and stay, so this is decided once. */
static void attach(struct installed *i)
{
    const struct menu_entry *e = i->entry;
    Pkg *h = rvx_packages;
    while (h && (h->state != 2 || !cfw_string_equal(h->id, e->host)))
        h = h->next;
    const struct rvx_descriptor *d = h ? h->modules[0].desc : NULL;
    for (u32 n = 0; d && n < d->screen_count; n++) {
        const struct screen *s = (const struct screen *)d->screens + n;
        if (s->kind != RV_SCREEN_KIND_LIST || !s->id || !s->list.count ||
            !cfw_string_equal(s->id, e->host_screen))
            continue;
        u32 count = s->list.count, index = e->host_after ? count : count - 1;
        for (u32 k = 0; e->host_after && k < count && index == count; k++)
            if (s->list.rows[k].id &&
                cfw_string_equal(s->list.rows[k].id, e->host_after))
                index = k;
        if (index < count) {
            i->host = h;
            i->host_screen = s;
            i->host_index = index;
        }
        return;
    }
}

int rvx_entries_valid(const Pkg *p, const struct menu_entry *e, u32 count)
{
    for (u32 n = 0; n < count; n++, e++)
        if (e->host ? !e->host_screen || cfw_string_equal(e->host, p->id) ||
                          (e->menus & (e->menus - 1))
                    : e->host_screen || e->host_after || !e->menus)
            return 0;
    return 1;
}

/* 1 for a declared row Apple shows: every row but a stock row it hides. */
static int row_shown(const struct screen *d, u32 index)
{
    const struct row *row = d->list.rows + index;
    return row->kind != RV_ROW_KIND_STOCK || rvx_stock_shown(row->stock);
}

const struct installed *rvx_guest_at(const struct core_screen *s,
                                     const struct screen *d, u32 position, u32 *index)
{
    for (*index = 0; *index < d->list.count; (*index)++) {
        if (row_shown(d, *index) && position-- == 0)
            return NULL;
        /* The guests appended after this row, in load order; one without a
         * row is not in the table yet. */
        for (const struct installed *i = installed; i; i = i->next)
            if (guest_of(i, s) && i->host_index == *index && i->host_row &&
                setting_on(i) && position-- == 0)
                return i;
    }
    return NULL;
}

u32 rvx_guest_count(const struct core_screen *s)
{
    u32 n = 0;
    for (const struct installed *i = installed; i; i = i->next)
        n += guest_of(i, s) && i->host_row && setting_on(i);
    return n;
}

u32 rvx_guest_position(const struct core_screen *s, const struct screen *d, u32 index)
{
    u32 position = index;
    for (u32 k = 0; k < index && k < d->list.count; k++)
        position -= !row_shown(d, k);
    for (const struct installed *i = installed; i; i = i->next)
        position +=
            guest_of(i, s) && i->host_row && setting_on(i) && i->host_index < index;
    return position;
}

int rvx_guest_other_needed(const struct core_screen *s, int main_plain)
{
    for (const struct installed *i = installed; i; i = i->next)
        if (guest_of(i, s) && core_lists_plain_entry(i->entry) != main_plain)
            return 1;
    return 0;
}

int rvx_guest_pane_property(const struct installed *i, void *controller, u32 field,
                            u32 type, uintptr_t *out)
{
    const struct menu_entry *e = i->entry;
    struct context ctx = {i->pkg, controller, NULL, NULL, -1, NULL};
    const struct pane *pane = e->pane;
    if (field == 2 && type == Property_Bitmap) {
        /* The box view is shared by every Settings pane and keeps the last
         * bitmap bound to it: the layout shows or hides it, the bitmap stays. */
        *out = osos_ui_bitmap(RES_SettingsPreview_Background_Image);
        return 1;
    }
    if (field == 1 && type == Property_Bitmap) {
        const struct bitmap *b =
            pane ? (pane->image ? pane->image(&ctx) : pane->bitmap) : NULL;
        u32 bytes = 0;
        void *image = b ? rvx_bitmap_load(i->pkg, b->name, &bytes) : NULL;
        const void *icon = rvx_native_icon(image, bytes);
        if (image)
            rvx_free(i->pkg, image);
        *out = icon ? (uintptr_t)icon : (uintptr_t)rvx_clear_bitmap;
        return 1;
    }
    if (type != Property_String || (field != 0 && field != 3))
        return 0;
    const char *text = e->row.label;
    if (field == 0) {
        text = pane ? (pane->text ? pane->text(&ctx) : pane->label) : NULL;
        if (!text && e->caption) {
            int n = rvx_get_int(i->pkg, e->caption);
            const struct rvx_setting *v = rvx_declaration(i->pkg, e->caption);
            text = v && v->labels && n >= 0 && (u32)n < v->label_count ? v->labels[n]
                   : n                                                 ? "On"
                                                                       : "Off";
        }
    }
    *out = (uintptr_t)(text ? text : "");
    return 1;
}

const struct menu_entry *rvx_menu_row(u32 item, Pkg **pkg)
{
    for (const struct installed *i = installed; i; i = i->next)
        if (i->item == item) {
            *pkg = i->pkg;
            return i->entry;
        }
    return NULL;
}

void rvx_refresh_pane(Pkg *p)
{
    if (rvx_top && rvx_top->ctx.self == p)
        osos_ui_invalidate();
    core_panes_refresh(p, 0);
}

void rvx_theme_changed(void)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        rvx_native_refresh(f);
    core_panes_refresh(NULL, 0);
    osos_ui_invalidate();
}

int cfw_rvx_property(u32 type, u32 property, uintptr_t *out)
{
    u32 title = rvx_canvas_title();
    if (type == OSOS_PROPERTY_STRING && title && property == title && rvx_top) {
        *out = (uintptr_t)(rvx_top->screen->title ? rvx_top->screen->title : "");
        return 1;
    }
    if (type == OSOS_PROPERTY_STRING && property >= 0x72500000) {
        for (struct installed *i = installed; i; i = i->next) {
            if (property != i->source_id || i->entry->row.kind != 2)
                continue;
            uint16_t key = i->entry->row.key;
            const struct rvx_setting *setting = rvx_declaration(i->pkg, key);
            int n = rvx_get_int(i->pkg, key);
            const char *label =
                setting && setting->labels && (u32)n < setting->label_count
                    ? setting->labels[n]
                    : NULL;
            if (label)
                *out = (uintptr_t)label;
            else if (setting && setting->type == 1)
                *out = osos_settings_on_off(n);
            else
                return 0;
            return 1;
        }
    }
    if (rvx_native_property(type, property, out))
        return 1;
    return core_panes_property(type, property, out);
}

static u32 action(void *controller, const char *action, u32 arg, const void *next)
{
    if (action && action[0] == 'P' && action[1] == 'R')
        for (struct installed *i = installed; i; i = i->next)
            if (cfw_string_equal(i->action + 3, action + 2)) {
                if (arg != OSOS_ACTION_SUPPORT_QUERY)
                    core_panes_show(i->item, controller);
                return 1;
            }
    if (rvx_menu_action(controller, action, arg, 1))
        return 1;
    return rvx_next(next, (u32)controller, (u32)action, arg, 0, 0, 0);
}

int rvx_menu_action(void *controller, const char *action, u32 arg, int stock)
{
    if (!action || action[0] != 'R' || action[1] != 'V' || action[2] != 'X')
        return 0;
    for (struct installed *i = installed; i; i = i->next)
        if (cfw_string_equal(i->action, action)) {
            if (arg != OSOS_ACTION_SUPPORT_QUERY) {
                struct context ctx = {i->pkg, controller, NULL, NULL, -1, NULL};
                if (stock && i->entry->menus & 0xffe0u &&
                    media.controller == controller) {
                    ctx.item = media.item;
                    ctx.row = media.row;
                    ctx.list = media.list;
                }
                const struct context *prior = rvx_origin;
                rvx_origin = &ctx;
                rvx_act(&ctx, &i->entry->row);
                rvx_origin = prior;
            }
            return 1;
        }
    return 0;
}

static unsigned append_context(void *menu, unsigned kind, void *controller, int row,
                               const struct osos_media_item *item)
{
    media.controller = controller;
    media.list = kind == 5 ? NULL : controller;
    media.row = row;
    if (item && item->record) {
        media_item = *item;
        media.item = &media_item;
    } else
        media.item = NULL;
    unsigned added = 0;
    for (struct installed *i = installed; i; i = i->next) {
        const struct menu_entry *e = i->entry;
        if (!(e->menus & (1u << kind)) || (e->shown && !rvx_get_int(i->pkg, e->shown)))
            continue;
        media.self = i->pkg;
        if (!e->available || e->available(&media)) {
            osos_context_menu_append(menu, i->item + 2, i->action, 1);
            added++;
        }
    }
    return added;
}

unsigned cfw_rvx_video_menu(void *menu, void *controller, int row, const void *item,
                            int tv)
{
    return append_context(menu, tv ? 13 : 12, controller, row, item);
}

static u32 context_cancel(unsigned kind, unsigned source, void *menu, u32 label,
                          const char *name, u32 flags, const void *next)
{
    if (!(menu_mask & (1u << kind)))
        return rvx_next(next, (u32)menu, label, (u32)name, flags, 0, 0);
    void *nav = ((void *(*)(void))0x08104d00)();
    void *c = nav ? *(void **)((uint8_t *)nav + 4) : NULL;
    if (c) {
        struct osos_media_item item;
        osos_media_item_init(&item);
        void *selection = osos_ui_selection();
        int row = selection ? osos_list_view_row(selection) : -1;
        if (kind == 5) {
            osos_player_current_item(osos_player_get(), &item);
            row = -1;
        } else if (source == 2 && row >= 0) {
            void *g = osos_genius_get();
            row = osos_genius_map_row(g, row);
            if (row >= 0)
                osos_genius_get_item(g, row, &item);
        } else if (row >= 0 && (kind == 6 || kind >= 9)) {
            if (source == 1 || kind >= 9)
                row = osos_song_controller_map_row(c, row);
            void *model = osos_song_controller_model(c);
            if (model && row >= 0)
                osos_song_model_get_item(model, row, &item);
        }
        append_context(menu, kind, c, row, &item);
    }
    return rvx_next(next, (u32)menu, label, (u32)name, flags, 0, 0);
}

#define CANCEL_HANDLER(name, kind, source)                                             \
    static u32 name(void *m, u32 label, const char *a, u32 flags, const void *next)    \
    {                                                                                  \
        return context_cancel(kind, source, m, label, a, flags, next);                 \
    }
CANCEL_HANDLER(song_cancel, 6, 0)
CANCEL_HANDLER(playlist_cancel, 6, 1)
CANCEL_HANDLER(genius_cancel, 6, 2)
CANCEL_HANDLER(playing_cancel, 5, 0)
CANCEL_HANDLER(album_cancel, 7, 0)
CANCEL_HANDLER(artist_cancel, 8, 0)
CANCEL_HANDLER(episode_cancel, 9, 0)
CANCEL_HANDLER(itunes_cancel, 10, 0)
CANCEL_HANDLER(book_cancel, 11, 0)

static u32 compare(const char *a, const char *b, const void *next)
{
    for (struct installed *i = installed; i; i = i->next)
        if (i->entry->row.kind == 0 && cfw_string_equal(a, i->action))
            return 0;
    return rvx_next(next, (u32)a, (u32)b, 0, 0, 0, 0);
}

/* What MenuItem_SetVisible does after writing a row's visibility: an open
 * list holding the item rebuilds, a closed one reads the row when next built. */
static void notify_item(u32 item)
{
    void *service = osos_ui_resources();
    osos_model_notify(service, Property_ItemChanged, item);
    osos_model_notify(service, OSOS_NOTIFY_NONE, item);
}

static void set_row(uint8_t *row, u32 item, uint8_t visible, int notify)
{
    if (!row || row[MenuRow_visible] == visible)
        return;
    row[MenuRow_visible] = visible;
    if (notify)
        notify_item(item);
}

/* The stock Settings rows list screens have claimed (RV_ROW_STOCK), one slot
 * per row of the Settings table. Apple loads the table once and its lists and
 * registry point at its bytes, so a claimed row is hidden there in place. The
 * claims are made at boot and stay: a main module never unloads. */
static struct claim {
    const Pkg *owner;          /* NULL while free */
    const struct screen *list; /* the list screen holding the copy */
    uint8_t wanted;            /* Apple's visibility for the row */
} claims[CORE_SETTINGS_ROWS];

static struct claim *claim_of(u32 item)
{
    u32 i = item - RES_SettingsMenu_About_Item;
    return i < CORE_SETTINGS_ROWS && claims[i].owner ? &claims[i] : NULL;
}

/* The copy in the owner's list shows what Apple asks of the row, once the
 * list is built. */
static void follow(const struct claim *c, u32 item)
{
    const struct core_screen *s = rvx_native_find(c->owner, c->list);
    u32 bytes;
    const void *items =
        s ? osos_resource_get(rvx_bank, Resource_ITEM, s->id + 3, &bytes) : NULL;
    set_row(items ? (uint8_t *)core_block_with_word(items, MenuRow_id, item, &bytes)
                  : NULL,
            item, c->wanted, 1);
}

int rvx_stock_shown(u32 item)
{
    const struct claim *c = claim_of(item);
    return !c || c->wanted;
}

u32 rvx_stock_hidden(const struct screen *d)
{
    u32 n = 0;
    for (u32 i = 0; d->kind == RV_SCREEN_KIND_LIST && i < d->list.count; i++)
        n += !row_shown(d, i);
    return n;
}

/* Hides Apple's row `item` in place for the list `d` of `p`. Apple's lists
 * are not built at boot, so nothing is notified. */
static int claim(const Pkg *p, const struct screen *d, u32 item)
{
    uint8_t *row = (uint8_t *)core_lists_settings_row(item);
    u32 i = item - RES_SettingsMenu_About_Item;
    if (!row || i >= CORE_SETTINGS_ROWS || claims[i].owner)
        return 0;
    claims[i].owner = p;
    claims[i].list = d;
    claims[i].wanted = row[MenuRow_visible];
    set_row(row, item, 0, 0);
    return 1;
}

static u32 claim_rows(const Pkg *p, const struct rvx_descriptor *d)
{
    u32 n = 0;
    for (u32 j = 0; j < d->screen_count; j++) {
        const struct screen *s = (const struct screen *)d->screens + j;
        for (u32 k = 0; s->kind == RV_SCREEN_KIND_LIST && k < s->list.count; k++)
            if (s->list.rows[k].kind == RV_ROW_KIND_STOCK)
                n += claim(p, s, s->list.rows[k].stock);
    }
    return n;
}

/* 1 when `item` is a stock row of a list screen of a started extension's
 * main module. */
static int declared_elsewhere(const Pkg *p, u32 item)
{
    for (const Pkg *o = rvx_packages; o; o = o->next) {
        const struct rvx_descriptor *d =
            o->state == 2 && o != p ? o->modules[0].desc : NULL;
        for (u32 j = 0; d && j < d->screen_count; j++) {
            const struct screen *s = (const struct screen *)d->screens + j;
            for (u32 k = 0; s->kind == RV_SCREEN_KIND_LIST && k < s->list.count; k++)
                if (s->list.rows[k].kind == RV_ROW_KIND_STOCK &&
                    s->list.rows[k].stock == item)
                    return 1;
        }
    }
    return 0;
}

int rvx_stock_rows_valid(const Pkg *p, const struct screen *screens, u32 count,
                         int main_module)
{
    for (u32 n = 0; n < count; n++) {
        const struct screen *s = screens + n;
        for (u32 i = 0; s->kind == RV_SCREEN_KIND_LIST && i < s->list.count; i++) {
            if (s->list.rows[i].kind != RV_ROW_KIND_STOCK)
                continue;
            u32 item = s->list.rows[i].stock;
            /* The list's id finds its record when Apple changes the row. */
            if (!main_module || !s->id ||
                item - RES_SettingsMenu_About_Item >= CORE_SETTINGS_ROWS ||
                (rvx_bank && !core_lists_settings_row(item)) ||
                declared_elsewhere(p, item))
                return 0;
            /* Each row once per extension. */
            for (u32 j = 0; j <= n; j++) {
                const struct screen *o = screens + j;
                u32 rows = o->kind != RV_SCREEN_KIND_LIST ? 0
                           : j < n                        ? o->list.count
                                                          : i;
                for (u32 k = 0; k < rows; k++)
                    if (o->list.rows[k].kind == RV_ROW_KIND_STOCK &&
                        o->list.rows[k].stock == item)
                        return 0;
            }
        }
    }
    return 1;
}

/* MenuItem_SetVisible: (item, visible). Apple's claimed row stays hidden. */
static u32 set_visible(u32 item, u32 visible, const void *next)
{
    struct claim *c = claim_of(item);
    if (!c)
        return rvx_next(next, item, visible, 0, 0, 0, 0);
    c->wanted = visible != 0;
    follow(c, item);
    return 1;
}

/* MenuItem_SetVisible_branch and NavigatorAction_PopToScreen_call. */
static const struct rvx_hook stock_hooks[] = {
    {0x9c27e8b1, (u32)set_visible, -32768, 0, 0},
    {0xc8e9acee, (u32)rvx_pop_to_screen, -32768, 0, 0},
};

static const struct {
    uint16_t menus;
    struct rvx_hook hook;
} actions[] = {

#define MENU_HOOK(menu, site, handler) {MENU_##menu, {site, (u32)handler, -32768, 0, 0}}
    MENU_HOOK(SETTINGS, 0x1a6513fc, action),
    MENU_HOOK(MAIN, 0x76bd65fc, action),
    MENU_HOOK(MUSIC, 0xde5a9f88, action),
    MENU_HOOK(MAIN, 0x53b9c0f8, action),
    MENU_HOOK(MUSIC, 0x49e85afc, action),
    MENU_HOOK(VIDEOS, 0x0fb2993d, action),
    MENU_HOOK(VIDEOS, 0x183a8f9b, action),
    MENU_HOOK(EXTRAS, 0x9c5686ee, action),
    MENU_HOOK(PLAYING, 0x9815797d, action),
    MENU_HOOK(PLAYING, 0x91f302df, action),
    MENU_HOOK(ARTIST, 0x403d833b, action),
    MENU_HOOK(ALBUM, 0xc6434f3d, action),
    MENU_HOOK(SONG, 0x509b4626, action),
    MENU_HOOK(SONG, 0xc90851b9, action),
    MENU_HOOK(SONG, 0x05db3232, action),
    MENU_HOOK(PODCAST, 0x82cd107d, action),
    MENU_HOOK(UNIVERSITY, 0x63559f18, action),
    MENU_HOOK(BOOK, 0x14751dad, action),
    MENU_HOOK(MOVIE, 0x4e0364e8, action),
    MENU_HOOK(TV, 0xfeb04f69, action),
    MENU_HOOK(VIDEO_PLAYLIST, 0x81c25998, action),
    MENU_HOOK(SONG, 0x28d91140, song_cancel),
    MENU_HOOK(SONG, 0x4a6e84ad, playlist_cancel),
    MENU_HOOK(SONG, 0xa7e35c24, genius_cancel),
    MENU_HOOK(PLAYING, 0xf257ea9f, playing_cancel),
    MENU_HOOK(ALBUM, 0x6213dc3d, album_cancel),
    MENU_HOOK(ARTIST, 0x7d1d2afb, artist_cancel),
    MENU_HOOK(PODCAST, 0x5289ae7d, episode_cancel),
    MENU_HOOK(UNIVERSITY, 0x0226bcd8, itunes_cancel),
    MENU_HOOK(BOOK, 0x887dcf8d, book_cancel),
    MENU_HOOK(CONTEXT, 0x1e4c792e, compare),
#undef MENU_HOOK
};

void rvx_install_menus(void)
{
    if (!rvx_bank)
        return;
    u32 id = 0x72500000, claimed = 0;
    for (Pkg *p = rvx_packages; p; p = p->next) {
        if (p->state != 2)
            continue;
        const struct rvx_descriptor *d = p->modules[0].desc;
        if (!d)
            continue;
        for (u32 i = 0; i < d->entry_count; i++)
            install(p, (const struct menu_entry *)d->entries + i, id += 3);
        claimed += claim_rows(p, d);
    }
    for (unsigned j = 0; claimed && j < sizeof stock_hooks / sizeof stock_hooks[0]; j++)
        rvx_bind_core(&stock_hooks[j]);
    if (!installed)
        return;
    for (struct installed *i = installed; i; i = i->next)
        if (i->entry->host)
            attach(i);
    for (unsigned j = 0; j < sizeof actions / sizeof actions[0]; j++)
        if (menu_mask & actions[j].menus)
            rvx_bind_core(&actions[j].hook);
    for (unsigned j = 0; j < 5; j++) {
        u32 n;
        const u32 *src = osos_resource_get(rvx_bank, 0x4954454d, tables[j], &n);
        if (!src || n < 4)
            continue;
        u32 count = *src++;
        n -= 4;
        while (count-- && n >= 8) {
            u32 size = src[0];
            src += 2;
            n -= 8;
            if (size < 0x34 || size > n)
                break;
            last[j] = src[12];
            if (j == 3 && src[12] == 0x0dad0c76 && size >= 0x6c) {
                sound_check_row = src;
                sound_check_size = size;
            }
            size = (size + 3) & ~3u;
            if (size > n)
                break;
            src += size / 4;
            n -= size;
        }
    }
}

void cfw_rvx_register_ui(void *bank)
{
    rvx_bank = bank;
    rvx_register_controller();
}

/* Appends the guests that follow `item`, a declared row of an extension's list
 * whose table Apple is building. A guest's row is made on the first build: a
 * copy of the Legal row, or of Sound Check's for a row with a value. */
static void host_rows(void *list, u32 table, u32 item, u32 metadata)
{
    const struct core_screen *s = rvx_native_table_screen(table);
    if (!s)
        return;
    for (struct installed *i = installed; i; i = i->next) {
        const struct menu_entry *e = i->entry;
        if (!guest_of(i, s) || item != host_anchor(s, i->host_screen, i->host_index))
            continue;
        if (!i->host_row) {
            const uint8_t *prototype =
                core_lists_prototype(e->row.kind == RV_ROW_KIND_SETTING);
            uint8_t *copy = prototype ? osos_import_alloc(ROW_BYTES) : NULL;
            if (!copy)
                continue;
            for (unsigned j = 0; j < ROW_BYTES; j++)
                copy[j] = prototype[j];
            u32 *words = (void *)copy;
            words[MenuRow_id / 4] = i->item;
            words[MenuRow_title / 4] = i->source_id;
            words[MenuRow_visible / 4] = setting_on(i);
            i->host_row = copy;
        }
        osos_resource_block_append(list, i->host_row, ROW_BYTES, metadata);
    }
}

void cfw_rvx_menu_row(void *list, u32 table, u32 item, const void *row, u32 bytes,
                      u32 metadata)
{
    if (!installed || bytes < 0x6c)
        return;
    unsigned slot = 0;
    while (slot < 5 && tables[slot] != table)
        slot++;
    if (slot == 5) {
        host_rows(list, table, item, metadata);
        return;
    }
    for (struct installed *i = installed; i; i = i->next) {
        const struct menu_entry *e = i->entry;
        /* A hosted row is in its host's list; its stock row is the fallback. */
        if (i->host || !(e->menus & (1u << slot)) ||
            (e->after ? e->after : last[slot]) != item)
            continue;
        int inline_value = slot == 3 && e->row.kind == 2 && sound_check_row;
        const void *prototype = inline_value ? sound_check_row : row;
        u32 size = inline_value ? sound_check_size : bytes;
        if (!i->row[slot]) {
            uint8_t *copy = osos_import_alloc(size);
            if (!copy)
                continue;
            for (unsigned j = 0; j < size; j++)
                copy[j] = ((const uint8_t *)prototype)[j];
            u32 *words = (void *)copy;
            words[12] = i->item;
            words[0x68 / 4] = i->source_id;
            words[0x64 / 4] = !e->shown || rvx_get_int(i->pkg, e->shown);
            if (slot < 2) {
                words[0x60 / 4] = 5;
                words[0x5c / 4] = (words[0x5c / 4] & 0xffff0000) | 4;
            }
            i->row[slot] = copy;
        }
        osos_resource_block_append(list, i->row[slot], size, metadata);
    }
}

/* A context menu row's chosen action reaches the list controller as the screen
 * event contextualMenu.<action>; Apple's own rows bind theirs in the screen's
 * CEVT, so a screen that opens one of the context menus gets a binding per
 * extension row. */
static unsigned context_events(const struct installed *i, uint8_t *buffer)
{
    char name[48];
    struct core_writer w = {buffer, 0};
    core_write_word(&w, 1);
    core_put_text(core_put_text(name, name + sizeof name, "contextualMenu."),
                  name + sizeof name, i->action);
    core_write_event(&w, name, i->action, NULL, NULL, 0);
    return w.at;
}

/* Reads an event stream into a layout's event map. */
static void read_events(void *map, const void *event, unsigned size)
{
    struct osos_string bytes;
    u32 allocator = 0, stream[31];
    osos_string_init_bytes(&bytes, event, size, &allocator);
    osos_string_stream_init(stream, &bytes, 0xc);
    osos_string_destroy(&bytes);
    osos_ui_event_map_read(stream, map);
    osos_string_stream_destroy(stream, 2, 0);
}

/* The media list layouts that bind no long press of Select while the other
 * layouts of their screens do: movies, TV episodes, video playlist tracks and
 * the songs of a one-album artist. They get the binding every other media list
 * has; its handler opens the stock context menu, or for the first two the one
 * media_menus.c builds from the extensions' rows. */
static void unbound_long_press(void *map, u32 layout)
{
    static const u32 layouts[] = {
        RES_MediaLists_Movies_WithTime_Layout,
        RES_MediaLists_TVEpisodes_Plain_Layout,
        RES_MediaLists_MixedVideoTracks_WithSubInfo_Layout,
        RES_MediaLists_Songs_WithArtistAndArtwork_Layout,
    };
    unsigned i = 0;
    while (i < sizeof layouts / sizeof layouts[0] && layouts[i] != layout)
        i++;
    if (i == sizeof layouts / sizeof layouts[0])
        return;
    uint8_t event[128]; /* 67 bytes: a count, the name, '1', the handler, no action */
    struct core_writer w = {event, 0};
    core_write_word(&w, 1);
    core_write_event(&w, "button.select.pressandhold", "HandleShowContextualMenu", NULL,
                     NULL, 0);
    read_events(map, event, w.at);
}

void cfw_rvx_menu_events(void *map, u32 screen, u32 layout)
{
    static const u32 screens[] = {0x0dad076e, 0x0dad07c8, 0x0dad01cc, 0x0dad0c6a,
                                  0x0dad0816};

    static const struct {
        u32 screen;
        uint8_t kind;
    } context_screens[] = {
        {0x0dad096c, 5},  /* NowPlaying_Screen */
        {0x0dad07e9, 6},  /* MediaLists_Songs_Screen */
        {0x0dad0808, 6},  /* MediaLists_NestedPlaylists_Screen */
        {0x0dad0844, 6},  /* MediaLists_Genius_Screen */
        {0x0dad0848, 6},  /* MediaLists_GeniusPlaylist_Screen */
        {0x0dad07ed, 7},  /* MediaLists_Albums_Screen */
        {0x0dad07f0, 8},  /* MediaLists_Artists_Screen */
        {0x0dad07f8, 9},  /* MediaLists_PodcastEpisodes_Screen */
        {0x0dad07fc, 10}, /* MediaLists_iTunesUEpisodes_Screen */
        {0x0dad07e1, 11}, /* MediaLists_Audiobooks_Screen */
        {0x0dad0804, 12}, /* MediaLists_Movies_Screen */
        {0x0dad0802, 13}, /* MediaLists_TVEpisodes_Screen */
        {0x0dad0812, 14}, /* MediaLists_NestedVideoPlaylists_Screen */
    };

    unsigned slot = 0, found = 0;
    unbound_long_press(map, layout);
    while (slot < 5 && screens[slot] != screen)
        slot++;
    if (slot < 5)
        found = 1;
    for (unsigned j = 0;
         !found && j < sizeof context_screens / sizeof context_screens[0]; j++)
        if (context_screens[j].screen == screen) {
            slot = context_screens[j].kind;
            found = 1;
        }
    int split = 0;
    const struct core_screen *host =
        found ? NULL : rvx_native_screen_of(screen, &split);
    if (!found && !host)
        return;
    for (struct installed *i = installed; i; i = i->next) {
        if (host ? !guest_of(i, host) : !(i->entry->menus & (1u << slot)))
            continue;
        uint8_t event[256];
        unsigned size = host       ? host_events(i, host, screen, split, event)
                        : slot < 5 ? menu_events(i, slot, event)
                                   : context_events(i, event);
        read_events(map, event, size);
    }
}

static struct installed *visibility_entry(unsigned menu, unsigned index)
{
    for (struct installed *i = installed; i; i = i->next) {
        if (i->entry->shown && (i->entry->menus & (1u << menu))) {
            if (!index--)
                return i;
        }
    }
    return NULL;
}

unsigned cfw_rvx_visible_rows(unsigned menu)
{
    unsigned n = 0;
    for (struct installed *i = installed; i; i = i->next)
        n += i->entry->shown && (i->entry->menus & (1u << menu));
    return n;
}

int cfw_rvx_visible_property(unsigned menu, unsigned row, uint32_t type, int selected,
                             uintptr_t *out)
{
    struct installed *i = visibility_entry(menu, row);
    if (!i)
        return 0;
    if (type == OSOS_PROPERTY_STRING)
        *out = osos_ui_string(i->item + 2);
    else if (type == OSOS_RESOURCE_BMAP)
        *out = rvx_get_int(i->pkg, i->entry->shown)
                   ? osos_ui_bitmap(OSOS_MENU_CHECKED_BITMAP + selected)
                   : 0;
    else
        return 0;
    return 1;
}

void cfw_rvx_toggle_row(unsigned menu, unsigned row)
{
    struct installed *i = visibility_entry(menu, row);
    if (i)
        rvx_set_int(i->pkg, i->entry->shown, !rvx_get_int(i->pkg, i->entry->shown));
}

/* Whether the pane of the list on top shows a guest row of `p`. */
static int guest_highlighted(Pkg *p)
{
    struct frame *f = rvx_top;
    u32 index;
    if (!f || !f->native || !f->split || f->screen->kind != RV_SCREEN_KIND_LIST)
        return 0;
    const struct installed *i = rvx_guest_at(f->native, f->screen, f->selected, &index);
    return i && i->pkg == p;
}

void rvx_setting_updated(Pkg *p, uint16_t key)
{
    if (rvx_top && (rvx_top->ctx.self == p || guest_highlighted(p)))
        rvx_native_refresh(rvx_top);
    for (struct installed *i = installed; i; i = i->next)
        if (i->pkg == p) {
            if (key == i->entry->shown && ((i->entry->menus & 0x1f) || i->host_row)) {
                /* Apple's menu registry was built before the row existed, so
                 * MenuItem_SetVisible cannot reach it: the row's own data
                 * carries the visibility on every table menu, and an open
                 * list rebuilds on the item notification. */
                u32 visible = rvx_get_int(p, key) != 0;
                int built = 0;
                for (unsigned slot = 0; slot < 5; slot++)
                    if (i->row[slot]) {
                        ((u32 *)i->row[slot])[0x64 / 4] = visible;
                        built = 1;
                    }
                if (i->host_row) {
                    ((u32 *)i->host_row)[0x64 / 4] = visible;
                    built = 1;
                }
                if (built)
                    notify_item(i->item);
            }
            if (i->entry->row.kind == 2 && i->entry->row.key == key &&
                (i->host || (i->entry->menus & MENU_SETTINGS))) {
                void *model = ((void *(*)(void))0x081e0ff4)();
                if (model)
                    osos_model_notify(model, OSOS_NOTIFY_ALL, i->source_id);
            }
        }
    core_panes_setting_changed(p, key);
}
