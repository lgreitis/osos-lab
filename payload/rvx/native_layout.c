/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors,
 * based on RepriseOS UI resources by lgreitis.
 */
#include "native_ui.h"

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return osos_resource_get(rvx_bank, type, id, bytes);
}

static void set(uint32_t type, uint32_t id, const void *data, uint32_t bytes)
{
    native_set(type, id, data, bytes);
}

uint32_t core_clone_ids(const struct screen *d)
{
    switch (d->kind) {
    case RV_SCREEN_KIND_LIST:
        if (d->list.count > 0x100000)
            return 0;
        /* screen, layout, title, items, 4 for layout clones, 2 per row, the
         * stock rows' layout clones, the long-press menu, then the split
         * layout's ids (native_ui.h) */
        return 8 + 2 * d->list.count + CORE_LIST_STOCK_IDS * core_lists_stock_count(d) +
               core_lists_context_ids(d) + CORE_LIST_SPLIT_IDS;
    case RV_SCREEN_KIND_TEXT:
        if (d->text.count > 0x100000)
            return 0;
        /* screen, layout, title, 5 for layout clones, one source per paragraph */
        return 8 + d->text.count;
    case RV_SCREEN_KIND_SLIDER:
        /* screen, layout, title, 5 for layout clones, the bar's and the value's source */
        return 10;
    case RV_SCREEN_KIND_STOCK:
        /* screen, layout, title, 5 for layout clones */
        return 8;
    case RV_SCREEN_KIND_DIALOG:
        /* screen, layout, title, 4 for layout clones, the VLyt, the message's,
         * detail's and buttons' sources, a property per button */
        return 11 + d->dialog.count;
    case RV_SCREEN_KIND_CHOICE:
    case RV_SCREEN_KIND_MULTI_CHOICE:
    case RV_SCREEN_KIND_DYNAMIC_LIST:
        /* screen, layout, title, items, 4 for layout clones, the row and its source,
         * the long-press menu, then the split layout's ids */
        return 10 + core_lists_context_ids(d) + CORE_LIST_SPLIT_IDS;
    default:
        return 8;
    }
}

static void put_word(uint8_t *at, uint32_t value)
{
    for (unsigned b = 0; b < 4; b++)
        at[b] = (uint8_t)(value >> (8 * b));
}

/* 1 for a stock binding block the clone keeps: every one unless the instance's
 * template is swapped for another (`swapped`), when only the item table and the
 * `extra` views stay. Apple applies each block to the view with its id and
 * does not check that the template has one. */
static int kept(const struct core_block *view, const struct view_sources *extra,
                int swapped)
{
    return !swapped || view->kind == ViewBinding_ItemTable ||
           view->kind - extra->view < extra->count;
}

/* The VCvs of one instance with the title view's source and the item table
 * rebound and `extra` sources applied: a block per view holding option blocks,
 * or one ViewBinding_ItemTable block naming the ITEM table. `vcvs` may be NULL
 * when the instance has none. Returns the bytes written, 0 when nothing changed. */
static uint32_t rebind_views(struct core_writer *w, const void *vcvs,
                             const struct layout_clone *c,
                             const struct view_sources *extra)
{
    uint32_t start = w->at, changed = 0, count = vcvs ? core_blocks_count(vcvs) : 0;
    uint32_t bound = 0, kept_count = 0,
             added = 0; /* extra views the stock VCvs gives a source, as a bit each */
    int swapped = extra && c->to != c->from;
    struct core_block view, option;
    for (uint32_t i = 0; i < count && core_block_at(vcvs, i, &view); i++) {
        kept_count += kept(&view, extra, swapped);
        for (uint32_t o = 0; extra && view.kind - extra->view < extra->count &&
                             core_block_at(view.data, o, &option);
             o++)
            if (option.kind == Resource_SORC)
                bound |= 1u << (view.kind - extra->view);
    }
    for (uint32_t i = 0; extra && i < extra->count; i++)
        added += !(bound >> i & 1);
    changed = kept_count != count;
    core_write_word(w, kept_count + added);
    for (uint32_t i = 0; i < count; i++) {
        if (!core_block_at(vcvs, i, &view))
            break;
        if (!kept(&view, extra, swapped))
            continue;
        uint32_t begin = core_write_block_begin(w, view.kind);
        if (view.kind == ViewBinding_ItemTable) {
            uint32_t words[2] = {ItemTableBinding_item_table, c->items};
            core_write_copy(w, view.data, view.bytes, words,
                            c->items && view.bytes >= 8);
            changed |= c->items != 0;
            core_write_block_end(w, begin);
            continue;
        }
        uint32_t source = view.kind == c->title_view ? c->title : 0;
        if (extra && view.kind - extra->view < extra->count)
            source = extra->source + (view.kind - extra->view);
        core_write_word(w, core_blocks_count(view.data));
        for (uint32_t o = 0;; o++) {
            struct core_block option;
            if (!core_block_at(view.data, o, &option))
                break;
            uint32_t b = core_write_block_begin(w, option.kind);
            if (option.kind == Resource_SORC && source) {
                core_write_word(w, source);
                changed = 1;
            } else {
                core_write_bytes(w, option.data, option.bytes);
            }
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    for (uint32_t i = 0; extra && i < extra->count; i++) {
        if (bound >> i & 1)
            continue;
        uint32_t begin = core_write_block_begin(w, extra->view + i);
        core_write_word(w, 1);
        uint32_t option = core_write_block_begin(w, Resource_SORC);
        core_write_word(w, extra->source + i);
        core_write_block_end(w, option);
        core_write_block_end(w, begin);
        changed = 1;
    }
    return changed ? w->at - start : 0;
}

/* Clones a stock layout: every SSin list and the VCvs of instances whose
 * bindings change. Uses ids from `next`. The clone is stored as it is built, so
 * the scratch holds one layout, one SSin and one VCvs. */
int core_clone_layout(const struct layout_clone *c, uint32_t *next, uint8_t *scratch,
                      uint32_t scratch_bytes)
{
    uint32_t bytes, lists;
    const void *slyt = stock(Resource_SLyt, c->stock, &bytes);
    if (!slyt || bytes > scratch_bytes)
        return 0;
    uint8_t *layout_copy = scratch;
    for (uint32_t i = 0; i < bytes; i++)
        layout_copy[i] = ((const uint8_t *)slyt)[i];
    lists = core_blocks_count(slyt);
    for (uint32_t l = 0; l < lists; l++) {
        struct core_block list;
        if (!core_block_at(layout_copy, l, &list))
            return 0;
        uint32_t ssin_bytes;
        const void *ssin =
            stock(Resource_SSin, core_word_at(list.data, 0), &ssin_bytes);
        if (!ssin || bytes + ssin_bytes > scratch_bytes)
            return 0;
        uint8_t *ssin_copy = scratch + bytes;
        for (uint32_t i = 0; i < ssin_bytes; i++)
            ssin_copy[i] = ((const uint8_t *)ssin)[i];
        for (uint32_t n = 0;; n++) {
            struct core_block instance;
            if (!core_block_at(ssin_copy, n, &instance))
                break;
            uint8_t *words = (uint8_t *)instance.data;
            uint32_t template = core_word_at(words, SsinInstance_template);
            uint32_t view_id = core_word_at(words, SsinInstance_view_bindings),
                     vcvs_bytes;
            const void *vcvs =
                view_id ? stock(Resource_VCvs, view_id, &vcvs_bytes) : NULL;
            const struct view_sources *sources =
                c->from && template == c->from ? c->extra : NULL;
            if (vcvs || sources) {
                struct core_writer measure = {NULL, 0};
                if (rebind_views(&measure, vcvs, c, sources)) {
                    uint8_t *out = scratch + bytes + ssin_bytes;
                    if (bytes + ssin_bytes + measure.at > scratch_bytes)
                        return 0;
                    struct core_writer w = {out, 0};
                    rebind_views(&w, vcvs, c, sources);
                    uint32_t id = (*next)++;
                    set(Resource_VCvs, id, out, w.at);
                    /* The stock instance's frame, keyed by its VCvs id. */
                    uint32_t vcrv_bytes;
                    const void *vcrv =
                        view_id ? stock(Resource_VCrv, view_id, &vcrv_bytes) : NULL;
                    if (vcrv)
                        set(Resource_VCrv, id, vcrv, vcrv_bytes);
                    put_word(words + SsinInstance_view_bindings, id);
                }
            }
            for (uint32_t offset = SsinInstance_template;
                 offset <= SsinInstance_layout_events_id; offset += 4)
                if (c->from && core_word_at(words, offset) == c->from)
                    put_word(words + offset, c->to);
            if (c->bindings && template == c->from)
                put_word(words + SsinInstance_layout_events_id, c->bindings);
        }
        uint32_t list_id = (*next)++;
        set(Resource_SSin, list_id, ssin_copy, ssin_bytes);
        put_word((uint8_t *)list.data, list_id);
    }
    set(Resource_SLyt, c->layout, layout_copy, bytes);
    return 1;
}

int core_clone_screen(uint32_t screen, uint32_t layout, uint32_t controller_screen,
                      uint32_t layouts_screen, uint32_t entries_screen,
                      const struct core_layout_entry *layouts, uint32_t count,
                      const void *events, uint32_t event_bytes, uint8_t *scratch,
                      uint32_t scratch_bytes)
{
    uint32_t bytes, entry_bytes;
    const void *scst = stock(Resource_SCST, controller_screen, &bytes);
    if (!scst)
        return 0;
    set(Resource_SCST, screen, scst, bytes);
    const void *slst = stock(Resource_SLst, layouts_screen, &bytes);
    const void *entries =
        count ? stock(Resource_SLst, entries_screen, &entry_bytes) : NULL;
    if (!slst || bytes + count * (8 + ScreenLayoutEntry_bytes) > scratch_bytes)
        return 0;
    struct core_writer w = {scratch, 0};
    uint32_t words[2] = {
        12, layout}; /* word 0 of the first block: count, bytes, kind before it */
    core_write_copy(&w, slst, bytes, words, 1);
    /* A clone's entry is its stock layout's, which carries the layout kind,
     * with the id replaced. */
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry =
            entries ? core_block_with_word(entries, ScreenLayoutEntry_layout,
                                           layouts[i].stock, &entry_bytes)
                    : NULL;
        if (!entry || entry_bytes != ScreenLayoutEntry_bytes)
            return 0;
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t id[2] = {ScreenLayoutEntry_layout, layouts[i].clone};
        core_write_copy(&w, entry, entry_bytes, id, 1);
        core_write_block_end(&w, begin);
    }
    core_write_word_at(&w, 0, core_blocks_count(slst) + count);
    set(Resource_SLst, screen, scratch, w.at);
    set(Resource_CEVT, screen, events, event_bytes);
    return 1;
}

void core_clone_set_style(uint32_t layout, uint32_t template, int16_t style)
{
    uint32_t bytes;
    const void *slyt = stock(Resource_SLyt, layout, &bytes);
    for (uint32_t l = 0; slyt && l < core_blocks_count(slyt); l++) {
        struct core_block list, instance;
        if (!core_block_at(slyt, l, &list))
            return;
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0), &bytes);
        for (uint32_t n = 0; ssin && core_block_at(ssin, n, &instance); n++) {
            uint8_t *words = (uint8_t *)instance.data;
            if (core_word_at(words, SsinInstance_template) != template)
                continue;
            for (uint32_t b = 0; b < 2; b++) {
                words[SsinInstance_enter_style + b] =
                    (uint8_t)((uint16_t)style >> (8 * b));
                words[SsinInstance_leave_style + b] =
                    (uint8_t)((uint16_t)style >> (8 * b));
            }
        }
    }
}

/* Menu pops the screen, holding it returns to the main menu. */
void core_clone_layout_events(struct core_writer *w)
{
    core_write_word(w, 2);
    core_write_event(w, "button.menu.up", "", "navigator.PopTopScreen", NULL, 0);
    core_write_event(w, "button.menu.pressandhold", "", "navigator.PopToMainScreen",
                     NULL, 0);
}

/* An event of a stream: a count, then per event a length-prefixed name, the
 * byte '1', a length-prefixed handler, a word that says whether an action
 * follows, and the length-prefixed action with a count of length-prefixed
 * arguments. Words are unaligned. */
struct event {
    const uint8_t *name;
    uint32_t name_bytes;
    struct core_event binding;
};

static const uint8_t *skip_string(const uint8_t *at)
{
    return at + 4 + core_word_at(at, 0);
}

static const uint8_t *read_event(const uint8_t *at, struct event *e)
{
    e->name_bytes = core_word_at(at, 0);
    e->name = at + 4;
    at = skip_string(at) + 1;
    e->binding.handler_bytes = core_word_at(at, 0);
    e->binding.handler = at + 4;
    at = skip_string(at);
    e->binding.argument = NULL;
    e->binding.argument_bytes = 0;
    if (core_word_at(at, 0)) {
        at = skip_string(at + 4);
        uint32_t arguments = core_word_at(at, 0);
        at += 4;
        if (arguments) {
            e->binding.argument_bytes = core_word_at(at, 0);
            e->binding.argument = at + 4;
        }
        while (arguments--)
            at = skip_string(at);
    } else {
        at += 4;
    }
    return at;
}

/* 1 when the event's name starts with `text`, or equals it with `whole`. */
static int named(const struct event *e, const char *text, int whole)
{
    uint32_t i = 0;
    for (; text[i]; i++)
        if (i >= e->name_bytes || e->name[i] != (uint8_t)text[i])
            return 0;
    return !whole || i == e->name_bytes;
}

uint32_t core_clone_copy_events(struct core_writer *w, const void *stream,
                                const char *name, int whole)
{
    uint32_t copied = 0;
    if (!stream)
        return 0;
    const uint8_t *at = (const uint8_t *)stream + 4;
    for (uint32_t n = core_word_at(stream, 0); n--;) {
        struct event e;
        const uint8_t *start = at;
        at = read_event(at, &e);
        if (!named(&e, name, whole))
            continue;
        core_write_bytes(w, start, (uint32_t)(at - start));
        copied++;
    }
    return copied;
}

int core_clone_find_event(const void *stream, const char *name, struct core_event *out)
{
    if (!stream)
        return 0;
    const uint8_t *at = (const uint8_t *)stream + 4;
    for (uint32_t n = core_word_at(stream, 0); n--;) {
        struct event e;
        at = read_event(at, &e);
        if (!named(&e, name, 1))
            continue;
        *out = e.binding;
        return 1;
    }
    return 0;
}

void core_clone_name_layout(uint32_t layout)
{
    char name[11];
    uint32_t string[CppString_bytes / 4], allocator = 0;
    core_screens_handler_name(name, layout);
    NATIVE_CALL(CppString_InitBytes)(string, name, 10, &allocator);
    *NATIVE_CALL(ResourceNames_FindOrInsertSlot)((void *)ADDR_ResourceNameMap, string) =
        layout;
    NATIVE_CALL(CppString_Destroy)(string);
}

/* Ids of a text page: id screen, +1 layout, +2 title, +3.. layout clones, +8..
 * one source per paragraph, bound to the text template's views by the layout. */
int core_clone_text(struct core_screen *s)
{
    uint32_t next = s->id + 3, rows = CORE_SCREEN_ROWS(s);
    uint8_t *scratch = osos_import_alloc(CORE_CLONE_SCRATCH_BYTES);
    if (!scratch)
        return 0;

    for (uint32_t i = 0; i < s->declaration->text.count; i++)
        core_set_source(rows + i, rows + i, 1);
    if (!s->text_template)
        s->text_template = core_text_template(s);
    uint32_t template = s->text_template;
    if (!template) {
        osos_import_free(scratch);
        return 0;
    }
    core_set_source(s->id + 2, s->id + 2, 1);

    uint32_t none = 0; /* the Legal controller handles Menu itself: no layout events */
    struct layout_clone c = {RES_Settings_Legal_Layout,
                             s->id + 1,
                             RES_StatusBarWhite_Text,
                             s->id + 2,
                             0,
                             RES_Settings_Legal_Template,
                             template,
                             NULL,
                             0};
    int built =
        core_clone_layout(&c, &next, scratch, CORE_CLONE_SCRATCH_BYTES) && next <= rows;
    if (built) {
        set(Resource_SEVT, s->id + 1, &none, 4);
        built = core_clone_screen(s->id, s->id + 1, RES_Settings_Legal_Screen,
                                  RES_Settings_Legal_Screen, 0, NULL, 0, &none, 4,
                                  scratch, CORE_CLONE_SCRATCH_BYTES);
    }
    osos_import_free(scratch);
    return built;
}

/* Ids of a slider or canvas: id screen, +1 layout, +2 title, +3.. layout clones,
 * +8 the bar's source, +9 the value text's. Both are the Brightness layout over
 * a plain resource screen controller, whose event slot canvas.c replaces. The
 * full-screen background stays under a canvas too: Settings shares it, so it
 * holds still when the screen opens or closes. Otherwise it enters into the
 * window on a pop, in front of the layer the leaving screen slides out on. */
static int clone_brightness(struct core_screen *s, uint32_t template,
                            const struct view_sources *views, const void *events,
                            uint32_t event_bytes)
{
    uint32_t next = s->id + 3;
    if (!template)
        return 0;
    uint8_t *scratch = osos_import_alloc(CORE_CLONE_SCRATCH_BYTES);
    if (!scratch)
        return 0;
    core_set_source(s->id + 2, s->id + 2, 1);
    struct layout_clone c = {RES_Settings_Brightness_Screen_Default,
                             s->id + 1,
                             RES_StatusBar_Title_Text,
                             s->id + 2,
                             0,
                             RES_Settings_Brightness_Template,
                             template,
                             views,
                             0};
    int built = core_clone_layout(&c, &next, scratch, CORE_CLONE_SCRATCH_BYTES) &&
                next <= CORE_SCREEN_ROWS(s);
    if (built) {
        uint32_t none = 0;
        set(Resource_SEVT, s->id + 1, events, event_bytes);
        built = core_clone_screen(s->id, s->id + 1, RES_Settings_Legal_Screen,
                                  RES_Settings_Brightness_Screen, 0, NULL, 0, &none, 4,
                                  scratch, CORE_CLONE_SCRATCH_BYTES);
    }
    osos_import_free(scratch);
    return built;
}

/* The stock layout's events: Select pops, as on the Brightness page. */
int core_clone_slider(struct core_screen *s)
{
    uint32_t rows = CORE_SCREEN_ROWS(s), bytes,
             template = core_template(CORE_TEMPLATE_SLIDER);
    const void *events =
        stock(Resource_SEVT, RES_Settings_Brightness_Screen_Default, &bytes);
    struct view_sources views = {template + 1, rows, 2};
    if (!events)
        return 0;
    core_set_source(rows, rows, 1);
    core_set_source(rows + 1, rows + 1, 1);
    return clone_brightness(s, template, &views, events, bytes);
}

/* Ids of a dialog: id screen, +1 layout, +2 title, +3.. layout clones, +7 the
 * dialog VLyt, +8 the message's source, +9 the detail's, +10 the buttons', +11..
 * a property per button. The stock EU Volume Limit question with its VLyt's
 * sources replaced and the stock controller class, events and screen events;
 * dialog.c gives the controller Select. The option bar draws one label per
 * entry of its source. */
int core_clone_dialog(struct core_screen *s)
{
    uint32_t next = s->id + 3, layout = s->id + 7, rows = CORE_SCREEN_ROWS(s), bytes,
             event_bytes;
    uint32_t count = s->declaration->dialog.count;
    const uint32_t notice = RES_SettingsMenus_DialogNotice_EULimitConfirmation_Layout;
    const void *vlyt = stock(Resource_VLyt, notice, &bytes);
    const void *tevt = stock(Resource_TEVT, notice, &event_bytes);
    if (!vlyt || !tevt || bytes > CORE_CLONE_SCRATCH_BYTES)
        return 0;
    uint8_t *scratch = osos_import_alloc(CORE_CLONE_SCRATCH_BYTES);
    if (!scratch)
        return 0;
    core_set_source(s->id + 2, s->id + 2, 1);
    core_set_source(rows, rows, 1);
    core_set_source(rows + 1, rows + 1, 1);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t b = core_write_block_begin(&w, Resource_SORC);
        core_write_word(&w, Service_Settings);
        core_write_word(&w, rows + 3 + i);
        core_write_word(&w, SourceDestination_Title);
        core_write_block_end(&w, b);
    }
    set(Resource_SORC, rows + 2, scratch, w.at);
    /* The message, detail and option bar views take the three sources in that
     * order. */
    static const struct layout_clone none;
    struct view_sources views = {RES_DialogNotice_View, rows, 3};
    w.at = 0;
    rebind_views(&w, vlyt, &none, &views);
    set(Resource_VLyt, layout, scratch, w.at);
    set(Resource_TEVT, layout, tevt, event_bytes);
    struct layout_clone c = {RES_SettingsMenus_EUVolume_Confirmation_Screen_Default,
                             s->id + 1,
                             RES_StatusBarWhite_Text,
                             s->id + 2,
                             0,
                             RES_SettingsMenus_DialogNotice_Template,
                             RES_SettingsMenus_DialogNotice_Template,
                             NULL,
                             layout};
    int built = core_clone_layout(&c, &next, scratch, CORE_CLONE_SCRATCH_BYTES) &&
                next <= layout;
    const void *events =
        stock(Resource_SEVT, RES_SettingsMenus_EUVolume_Confirmation_Screen_Default,
              &event_bytes);
    const void *cevt =
        stock(Resource_CEVT, RES_SettingsMenus_EUVolume_Confirmation_Screen, &bytes);
    if (built && events && cevt) {
        set(Resource_SEVT, s->id + 1, events, event_bytes);
        built = core_clone_screen(
            s->id, s->id + 1, RES_SettingsMenus_EUVolume_Confirmation_Screen,
            RES_SettingsMenus_EUVolume_Confirmation_Screen, 0, NULL, 0, cevt, bytes,
            scratch, CORE_CLONE_SCRATCH_BYTES);
    } else {
        built = 0;
    }
    osos_import_free(scratch);
    return built;
}

/* Ids of a stock screen: id screen, +1 layout, +2 title, +3.. layout clones. The
 * stock screen's controller class, list binding and events over a layout clone
 * whose title is the extension's; the stock layout's events are stored under the
 * clone, as the controller's handlers are bound by layout id. */
int core_clone_stock(struct core_screen *s)
{
    static const struct {
        uint32_t screen, layout, title_view;
    } stocks[] = {
        [RV_STOCK_SCREEN_GAMES] = {RES_Games_Menu_Screen, RES_Games_Menu_Layout,
                                   RES_StatusBarWhite_Text},
    };

    uint32_t which = s->declaration->stock.screen, next = s->id + 3, event_bytes,
             cevt_bytes;
    if (which >= sizeof stocks / sizeof stocks[0] || !stocks[which].screen)
        return 0;
    const void *events = stock(Resource_SEVT, stocks[which].layout, &event_bytes);
    const void *cevt = stock(Resource_CEVT, stocks[which].screen, &cevt_bytes);
    if (!events || !cevt)
        return 0;
    uint8_t *scratch = osos_import_alloc(CORE_CLONE_SCRATCH_BYTES);
    if (!scratch)
        return 0;
    core_set_source(s->id + 2, s->id + 2, 1);
    struct layout_clone c = {stocks[which].layout,
                             s->id + 1,
                             stocks[which].title_view,
                             s->id + 2,
                             0,
                             0,
                             0,
                             NULL,
                             0};
    int built = core_clone_layout(&c, &next, scratch, CORE_CLONE_SCRATCH_BYTES) &&
                next <= CORE_SCREEN_ROWS(s);
    if (built) {
        set(Resource_SEVT, s->id + 1, events, event_bytes);
        built = core_clone_screen(s->id, s->id + 1, stocks[which].screen,
                                  stocks[which].screen, 0, NULL, 0, cevt, cevt_bytes,
                                  scratch, CORE_CLONE_SCRATCH_BYTES);
    }
    osos_import_free(scratch);
    return built;
}
