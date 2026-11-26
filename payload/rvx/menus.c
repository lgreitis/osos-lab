/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_ui.h"
#include "runtime.h"
#include "osos.h"
#include "cfw.h"
#include "screen_ui.h"

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
static struct installed *highlight;
static void *highlight_controller, *preview_image;
static const char *preview_text, *preview_lines[2];
static void *preview_badge;
static u32 preview_bytes, badge_bytes;

/* The native image view retains its BMap between property notifications. */
enum { ICON_WORDS = (28 + 80 * 121) / 4 };

static u32 *native_icon;
const u32 rvx_clear_bitmap[] = {0x1888, 0x00200004, 0, 0, 1, 1, 4, 0};

int rvx_is_pane_icon(const u32 *image, u32 bytes)
{
    return image && bytes == ICON_WORDS * 4 && image[0] == 8 &&
           image[1] == 0x00080050 && image[2] == 0 && image[3] == 0 &&
           image[4] == 121 && image[5] == 78 && image[6] == bytes - 28;
}

static int update_menu_icon(const u32 *image, u32 bytes)
{
    if (!rvx_is_pane_icon(image, bytes))
        return 0;
    if (!native_icon)
        native_icon = osos_import_alloc(bytes);
    if (native_icon)
        for (unsigned j = 0; j < ICON_WORDS; j++)
            native_icon[j] = image[j];
    return native_icon != NULL;
}
static struct context media;
static struct osos_media_item media_item;

static void install(Pkg *p, const struct menu_entry *e, u32 id)
{
    struct installed *i = osos_import_alloc(sizeof *i);
    if (!i)
        return;
    menu_mask |= e->menus;
    if ((e->menus & (MENU_MAIN | MENU_MUSIC | MENU_EXTRAS | MENU_VIDEOS)) ||
        ((e->menus & MENU_SETTINGS) && e->plain))
        cfw_rvx_has_menu_pane = 1;
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
    static const char *const layouts[] = {"RVX_MainPane", "RVX_MusicPane",
                                          "RVX_ExtrasPane", "RVX_Preview",
                                          "RVX_VideosPane"};
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
        const char *layout =
            slot == 3 && entry->plain ? "RVX_PlainPane" : layouts[slot];
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
 * pane's caption handler, as the host's own rows do (native_lists.c). */
static unsigned host_events(const struct installed *i, u32 screen, int split,
                            uint8_t *buffer)
{
    char name[48], handler[11];
    struct core_writer w = {buffer, 0};
    int pane = split && core_lists_per_row_pane(i->host_screen);
    core_write_word(&w, pane ? 2 : 1);
    core_row_event_name(name, sizeof name, i->item, ".chosen");
    core_write_event(&w, name, i->action, NULL, NULL, 0);
    if (pane) {
        core_row_event_name(name, sizeof name, i->item, ".delayedselected");
        core_screens_handler_name(handler, CORE_LIST_PANE_CAPTION(screen));
        core_write_event(&w, name, handler, NULL, NULL, 0);
    }
    return w.at;
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

const struct installed *rvx_guest_at(const struct core_screen *s,
                                     const struct screen *d, u32 position, u32 *index)
{
    for (*index = 0; *index < d->list.count; (*index)++) {
        if (position-- == 0)
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

u32 rvx_guest_position(const struct core_screen *s, u32 index)
{
    u32 position = index;
    for (const struct installed *i = installed; i; i = i->next)
        position +=
            guest_of(i, s) && i->host_row && setting_on(i) && i->host_index < index;
    return position;
}

int rvx_guest_pane_property(const struct installed *i, void *controller, u32 field,
                            u32 type, uintptr_t *out)
{
    const struct menu_entry *e = i->entry;
    struct context ctx = {i->pkg, controller, NULL, NULL, -1, NULL};
    const struct pane *pane = e->pane;
    if ((field == 1 || field == 2) && type == Property_Bitmap) {
        const struct bitmap *b =
            pane ? (pane->image ? pane->image(&ctx) : pane->bitmap) : NULL;
        u32 bytes = 0;
        void *image = b ? rvx_bitmap_load(i->pkg, b->name, &bytes) : NULL;
        int icon = update_menu_icon(image, bytes);
        if (image)
            rvx_free(i->pkg, image);
        *out = (uintptr_t)rvx_clear_bitmap;
        if (icon)
            *out = field == 1 ? (uintptr_t)native_icon
                   : e->plain ? (uintptr_t)rvx_clear_bitmap
                              : osos_ui_bitmap(RES_SettingsPreview_Background_Image);
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

static void refresh_menu_pane(Pkg *p, int caption_change)
{
    if (rvx_top && rvx_top->ctx.self == p)
        osos_ui_invalidate();
    if (!highlight || highlight->pkg != p)
        return;
    const struct menu_entry *entry = highlight->entry;
    struct context ctx = {p, highlight_controller, NULL, NULL, -1, NULL};
    const struct pane *pane = entry->pane;
    const char *caption = pane ? (pane->text ? pane->text(&ctx) : pane->label) : NULL;
    if (!caption && entry->caption) {
        int n = rvx_get_int(p, entry->caption);
        const struct rvx_setting *v = rvx_declaration(p, entry->caption);
        caption = v && v->labels && n >= 0 && (u32)n < v->label_count
                      ? v->labels[n]
                      : (n ? "On" : "Off");
    }
    preview_text = caption ? caption : "";
    int refresh_image = !caption_change || (pane && pane->image) ||
                        (pane && pane->bitmap && !preview_image);
    if (refresh_image) {
        const struct bitmap *b =
            pane ? (pane->image ? pane->image(&ctx) : pane->bitmap) : NULL;
        void *old = preview_image;
        preview_image = b ? rvx_bitmap_load(p, b->name, &preview_bytes) : NULL;
        if (update_menu_icon(preview_image, preview_bytes)) {
            rvx_free(p, preview_image);
            preview_image = native_icon;
        }
        if (old && old != native_icon)
            rvx_free(p, old);
    }
    const struct pane_extra *extra = entry->extra;
    for (unsigned j = 0; j < 2; j++)
        preview_lines[j] =
            extra ? (extra->text ? extra->text(&ctx, j) : extra->lines[j]) : NULL;
    if (!caption_change ||
        (extra && (extra->image || (extra->badge && !preview_badge)))) {
        const struct bitmap *b =
            extra ? (extra->image ? extra->image(&ctx) : extra->badge) : NULL;
        if (preview_badge)
            rvx_free(p, preview_badge);
        preview_badge = b ? rvx_bitmap_load(p, b->name, &badge_bytes) : NULL;
    }
    void *model = ((void *(*)(void))0x081e0ff4)();
    if (model) {
        if (refresh_image)
            osos_model_notify(model, OSOS_NOTIFY_ALL, RVX_PreviewImage);
        osos_model_notify(model, OSOS_NOTIFY_ALL, RVX_PreviewText);
    }
}

void rvx_refresh_pane(Pkg *p)
{
    refresh_menu_pane(p, 0);
}

void rvx_theme_changed(void)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        rvx_native_refresh(f);
    if (highlight)
        rvx_refresh_pane(highlight->pkg);
    osos_ui_invalidate();
}

void rvx_draw_menu_pane(int width, int height)
{
    if (!highlight)
        return;
    const struct graphics *g = &rvx_graphics;
    g->fill(&(struct rect){0, 0, width, height}, g->color(0));
    const u32 *image = preview_image;
    int w = image && preview_bytes >= 28 ? (int)image[5] - (int)image[3] : 0;
    int h = image && preview_bytes >= 28 ? (int)image[4] - (int)image[2] : 0;
    int top = (height - h) / 2;
    if ((w == 119 && h == 119) || (w == 142 && h == 87) || (w == 130 && h == 128) ||
        (w == 146 && h == 110) || (w == 132 && h == 145) ||
        (w == 78 && h == 121 && highlight->entry->plain))
        g->bmap((width - w) / 2, top, image);
    else
        h = 0;
    const char *label = highlight->entry->row.label;
    if (w == 130 || w == 132 || w == 78)
        g->text_in(&(struct rect){0, 0, width, 24}, 1, g->color(1), label, 0);
    if (w == 130)
        for (unsigned j = 0; j < 2; j++)
            g->text(12, top + 6 + j * 20, 2, g->color(1), preview_lines[j]);
    if (w == 146 && preview_badge && badge_bytes >= 28)
        g->bmap(8, top + h + 3, preview_badge);
    g->text_in(&(struct rect){w == 146 ? 20 : 0, h ? top + h + 1 : height / 2,
                              width - (w == 146 ? 20 : 0), 26},
               2, g->color(4), preview_text && *preview_text ? preview_text : label, 0);
}

int cfw_rvx_property(u32 type, u32 property, uintptr_t *out)
{
    if (type == OSOS_PROPERTY_STRING && property == RVX_Title && rvx_top) {
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
    if (type == OSOS_RESOURCE_BMAP && property == RVX_PreviewImage) {
        *out = (uintptr_t)(preview_image && preview_image == native_icon
                               ? native_icon
                               : rvx_clear_bitmap);
        return 1;
    }
    if (type == OSOS_PROPERTY_STRING && property == RVX_PreviewText) {
        *out = (uintptr_t)(preview_text ? preview_text : "");
        return 1;
    }
    return 0;
}

static u32 action(void *controller, const char *action, u32 arg, const void *next)
{
    if (action && action[0] == 'P' && action[1] == 'R')
        for (struct installed *i = installed; i; i = i->next)
            if (cfw_string_equal(i->action + 3, action + 2)) {
                if (arg != OSOS_ACTION_SUPPORT_QUERY) {
                    if (highlight && preview_image) {
                        if (preview_image != native_icon)
                            rvx_free(highlight->pkg, preview_image);
                        preview_image = NULL;
                    }
                    if (highlight && preview_badge) {
                        rvx_free(highlight->pkg, preview_badge);
                        preview_badge = NULL;
                    }
                    highlight = i;
                    highlight_controller = controller;
                    const char *title = i->entry->row.label;
                    unsigned n = 0;
                    while (title && title[n])
                        n++;
                    osos_resource_set(rvx_bank, 0x53747220, RVX_PaneTitle,
                                      title ? title : "", n + 1);
                    rvx_refresh_pane(i->pkg);
                }
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

static void append_context(void *menu, unsigned kind, void *controller, int row,
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
    for (struct installed *i = installed; i; i = i->next) {
        const struct menu_entry *e = i->entry;
        if (!(e->menus & (1u << kind)) || (e->shown && !rvx_get_int(i->pkg, e->shown)))
            continue;
        media.self = i->pkg;
        if (!e->available || e->available(&media))
            osos_context_menu_append(menu, i->item + 2, i->action, 1);
    }
}

void cfw_rvx_video_menu(void *menu, void *controller, int row, const void *item, int tv)
{
    append_context(menu, tv ? 13 : 12, controller, row, item);
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
    u32 id = 0x72500000;
    for (Pkg *p = rvx_packages; p; p = p->next) {
        if (p->state != 2)
            continue;
        const struct rvx_descriptor *d = p->modules[0].desc;
        if (!d)
            continue;
        for (u32 i = 0; i < d->entry_count; i++)
            install(p, (const struct menu_entry *)d->entries + i, id += 3);
    }
    if (!installed)
        return;
    for (struct installed *i = installed; i; i = i->next)
        if (i->entry->host)
            attach(i);
    for (unsigned j = 0; j < sizeof actions / sizeof actions[0]; j++)
        if (menu_mask & actions[j].menus)
            rvx_bind_menu(&actions[j].hook);
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
        if (!guest_of(i, s) || item != CORE_SCREEN_ROWS(s) + 2 * i->host_index)
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

void cfw_rvx_menu_events(void *map, u32 screen)
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
        struct osos_string bytes;
        u32 allocator = 0, stream[31];
        uint8_t event[192];
        unsigned size = host       ? host_events(i, screen, split, event)
                        : slot < 5 ? menu_events(i, slot, event)
                                   : context_events(i, event);
        osos_string_init_bytes(&bytes, event, size, &allocator);
        osos_string_stream_init(stream, &bytes, 0xc);
        osos_string_destroy(&bytes);
        osos_ui_event_map_read(stream, map);
        osos_string_stream_destroy(stream, 2, 0);
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
                if (built) {
                    void *service = osos_ui_resources();
                    osos_model_notify(service, 0x4c697445, i->item); /* LitE */
                    osos_model_notify(service, OSOS_NOTIFY_NONE, i->item);
                }
            }
            if (i->entry->row.kind == 2 && i->entry->row.key == key &&
                (i->host || (i->entry->menus & MENU_SETTINGS))) {
                void *model = ((void *(*)(void))0x081e0ff4)();
                if (model)
                    osos_model_notify(model, OSOS_NOTIFY_ALL, i->source_id);
            }
        }
    if (highlight && highlight->pkg == p && highlight->entry->caption == key)
        refresh_menu_pane(p, 1);
}
