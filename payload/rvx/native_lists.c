/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors,
 * based on RepriseOS UI resources by lgreitis.
 */
#include "native_ui.h"

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return osos_resource_get(rvx_bank, type, id, bytes);
}

static uint32_t instance_word(uint32_t layout_id, uint32_t template, uint32_t field)
{
    uint32_t bytes;
    const void *slyt = stock(Resource_SLyt, layout_id, &bytes);
    for (uint32_t l = 0; slyt && l < core_blocks_count(slyt); l++) {
        struct core_block list, instance;
        core_block_at(slyt, l, &list);
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0), &bytes);
        for (uint32_t i = 0; ssin && core_block_at(ssin, i, &instance); i++)
            if (core_word_at(instance.data, SsinInstance_template) == template)
                return core_word_at(instance.data, field);
    }
    return 0;
}

/* The VLyt a stock layout's instance of `template` names, 0 for none. */
static uint32_t pane_layout(uint32_t layout_id, uint32_t template)
{
    return instance_word(layout_id, template, SsinInstance_layout_events_id);
}

uint32_t core_panes_write(struct core_writer *w, uint32_t title, uint32_t line,
                          uint32_t icon, uint32_t box)
{
    uint32_t bytes, stock_pane = pane_layout(RES_Settings_MainMenu_Layout,
                                             RES_SettingsPreview_Template);
    const void *vlyt = stock_pane ? stock(Resource_VLyt, stock_pane, &bytes) : NULL;
    if (!vlyt)
        return 0;
    uint32_t views = core_blocks_count(vlyt);
    core_write_word(w, views);
    for (uint32_t v = 0; v < views; v++) {
        struct core_block view, option;
        core_block_at(vlyt, v, &view);
        uint32_t source = view.kind == RES_SettingsPreview_Caption_Text      ? title
                          : view.kind == RES_Settings_InfoDetail_View        ? line
                          : view.kind == RES_Settings_Icon_View              ? icon
                          : view.kind == RES_SettingsPreview_Background_View ? box
                                                                             : 0;
        /* The box's bitmap comes from the template; the stock layouts only show
         * or hide it, so a source for it is an option of its own. */
        int add_source = view.kind == RES_SettingsPreview_Background_View && box;
        uint32_t begin = core_write_block_begin(w, view.kind);
        core_write_word(w, core_blocks_count(view.data) + (uint32_t)add_source);
        for (uint32_t o = 0; core_block_at(view.data, o, &option); o++) {
            uint32_t b = core_write_block_begin(w, option.kind);
            if (option.kind == Resource_SORC && source)
                core_write_word(w, source);
            else if (option.kind == ViewOption_Visible)
                core_write_bytes(w, source ? "\1" : "", 1);
            else
                core_write_bytes(w, option.data, option.bytes);
            core_write_block_end(w, b);
        }
        if (add_source) {
            uint32_t b = core_write_block_begin(w, Resource_SORC);
            core_write_word(w, box);
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    return stock_pane;
}

void core_panes_store(uint32_t id, const void *data, uint32_t bytes,
                      uint32_t stock_pane)
{
    uint32_t events_bytes;
    const void *events = stock(Resource_TEVT, stock_pane, &events_bytes);
    native_set(Resource_VLyt, id, data, bytes);
    if (events) /* the layout's event tables are loaded by this id too */
        native_set(Resource_TEVT, id, events, events_bytes);
}

static const uint8_t *stock_row(uint32_t table, uint32_t id)
{
    uint32_t bytes, row_bytes;
    const void *items = stock(Resource_ITEM, table, &bytes);
    const uint8_t *row =
        items ? core_block_with_word(items, MenuRow_id, id, &row_bytes) : NULL;
    return row && row_bytes == ROW_BYTES ? row : NULL;
}

static int indexed(const struct core_screen *s)
{
    return core_lists_indexed(s->declaration->kind);
}

/* A chosen event per row, and when the pane follows the highlight a
 * delayed-selected event per row that names the pane's handler. */
static void write_row_events(struct core_writer *w, uint32_t first_item, uint32_t count,
                             uint32_t pane, uint32_t more)
{
    char name[48], handler[11];
    core_write_word(w, count + (pane ? count : 0) + more);
    for (uint32_t i = 0; i < count; i++) {
        core_row_event_name(name, sizeof name, first_item + 2 * i, ".chosen");
        core_screens_handler_name(handler, first_item + 2 * i);
        core_write_event(w, name, handler, NULL, NULL, 0);
    }
    for (uint32_t i = 0; pane && i < count; i++) {
        core_row_event_name(name, sizeof name, first_item + 2 * i, ".delayedselected");
        core_screens_handler_name(handler, pane);
        core_write_event(w, name, handler, NULL, NULL, 0);
    }
}

/* Whether the pane changes with the highlighted row: a choice screen names the
 * choice, a list with a pane per row shows that row's. */
static int pane_follows(const struct screen *d)
{
    return d->kind == RV_SCREEN_KIND_CHOICE || core_lists_per_row_pane(d);
}

static int pane_has_icon(const struct screen *d)
{
    return d->bitmap || core_lists_per_row_pane(d);
}

uint32_t core_lists_screen_id(const struct core_screen *s, int split)
{
    return split ? s->id + s->ids - CORE_LIST_SPLIT_IDS : s->id;
}

/* A split layout: the Settings > Main Menu layout with the title and the pane
 * rebound (the caption shows the title, the other pane views are hidden), and
 * the list bound by the screen's VSlt (the stock split screens bind their list
 * there, not in the list instance). Takes one id from *next for the pane;
 * the declaration's caption and icon, when set, fill the pane's detail line
 * and icon; a choice screen's line follows the highlight instead, and a list
 * with a pane per row shows the highlighted row's title, caption and icon. */
static int clone_split(struct core_screen *s, uint32_t screen, uint32_t *next,
                       uint8_t *scratch, uint32_t bytes)
{
    struct core_writer w = {scratch, 0};
    const struct screen *d = s->declaration;
    uint32_t caption = CORE_LIST_PANE_CAPTION(screen);
    uint32_t pane_id = (*next)++;
    uint32_t icon = pane_has_icon(d) ? caption + 1 : 0,
             box = pane_has_icon(d) ? caption + 2 : 0;
    uint32_t line = d->caption || pane_follows(d) ? caption : 0;
    uint32_t stock_pane =
        core_panes_write(&w, CORE_LIST_PANE_TITLE(screen), line, icon, box);
    uint32_t template = core_pane_template();
    if (!stock_pane || !template)
        return 0;
    for (uint32_t i = 0; i < 4; i++)
        core_set_source(caption + i, caption + i, SourceDestination_Title);
    core_panes_store(pane_id, scratch, w.at, stock_pane);
    uint32_t vslt[5] = {1, 8, ViewBinding_ItemTable, RES_SettingsList_View, s->id + 3};
    native_set(Resource_VSlt, screen, vslt, sizeof vslt);
    struct layout_clone c = {RES_Settings_MainMenu_Layout,
                             screen + 1,
                             RES_StatusBarWhite_Text,
                             s->id + 2,
                             0,
                             RES_SettingsPreview_Template,
                             template,
                             NULL,
                             pane_id};
    return core_clone_layout(&c, next, scratch, bytes);
}

const uint8_t *core_lists_prototype(int value)
{
    return value ? stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_SoundCheck_Item)
                 : stock_row(RES_SettingsMenu_Items, RES_SettingsMenu_Legal_Item);
}

/* The row a list screen's rows are copied from. */
static const uint8_t *prototype(const struct core_screen *s, uint32_t i, int *value)
{
    const struct screen *d = s->declaration;
    uint8_t row_kind = d->kind == RV_SCREEN_KIND_LIST ? d->list.rows[i].kind : 0;
    *value = d->kind == RV_SCREEN_KIND_DYNAMIC_LIST ||
             (d->kind == RV_SCREEN_KIND_LIST &&
              (row_kind == RV_ROW_KIND_SETTING || row_kind == RV_ROW_KIND_INFO));
    if (*value)
        return core_lists_prototype(1);
    switch (d->kind) {
    case RV_SCREEN_KIND_CHOICE:
    case RV_SCREEN_KIND_MULTI_CHOICE:
        return stock_row(RES_Settings_MainMenu_Items, RES_Settings_MainMenu_Item);
    default:
        return core_lists_prototype(0);
    }
}

/* The shared part of a list: the title source and, per row, the item id (its
 * title property) and the source id (its value property) in the ITEM table. */
static int write_rows(struct core_screen *s, uint8_t *scratch)
{
    const struct screen *d = s->declaration;
    uint32_t rows = CORE_SCREEN_ROWS(s), count = indexed(s) ? 1 : d->list.count;
    core_set_source(s->id + 2, s->id + 2, 1);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t item = rows + 2 * i, source = item + 1;
        int value;
        const uint8_t *row = prototype(s, i, &value);
        if (!row)
            return 0;
        /* A dynamic list with the detail below draws the inline value under the
         * label; with a chevron the model gives the style (indexed.c), as the
         * chevron shows only for a style the model answers. */
        uint8_t detail = d->kind == RV_SCREEN_KIND_DYNAMIC_LIST ? d->dynamic.detail
                                                                : RV_DETAIL_BESIDE;
        int action = d->kind == RV_SCREEN_KIND_LIST &&
                     d->list.rows[i].kind == RV_ROW_KIND_ACTION;
        int checked =
            d->kind == RV_SCREEN_KIND_CHOICE || d->kind == RV_SCREEN_KIND_MULTI_CHOICE;
        uint32_t words[6] = {MenuRow_id,
                             item,
                             MenuRow_title,
                             source,
                             MenuRow_style,
                             checked  ? MenuRowStyle_CheckIndented
                             : action ? MenuRowStyle_Text
                             : detail == RV_DETAIL_BELOW_SUBMENU
                                 ? MenuRowStyle_ModelLayout
                                 : MenuRowStyle_TwoLine};
        uint32_t begin = core_write_block_begin(&w, 0);
        uint32_t at = w.at;
        core_write_copy(&w, row, ROW_BYTES, words,
                        checked || action || detail != RV_DETAIL_BESIDE ? 3 : 2);
        scratch[at + MenuRow_title_from_model] = 1;
        core_write_block_end(&w, begin);
        if (d->kind == RV_SCREEN_KIND_DYNAMIC_LIST && d->dynamic.status) {
            uint32_t bindings[] = {
                4,
                12,
                Resource_SORC,
                Service_Settings,
                item,
                SourceDestination_Title,
                12,
                Resource_SORC,
                Service_Settings,
                source,
                SourceDestination_InlineValue,
                12,
                Resource_SORC,
                Service_Settings,
                item,
                SourceDestination_RowStatusBitmap,
                12,
                Resource_SORC,
                Service_Settings,
                item,
                SourceDestination_RowStatusSelectedBitmap,
            };
            native_set(Resource_SORC, source, bindings, sizeof bindings);
        } else if (value)
            core_set_source2(source, item, source, SourceDestination_InlineValue);
        else if (d->kind == RV_SCREEN_KIND_MULTI_CHOICE ||
                 d->kind == RV_SCREEN_KIND_CHOICE)
            core_set_source2(source, item, item, SourceDestination_RowBitmap);
        else
            core_set_source(source, item, 1);
    }
    /* A table left from the owner's earlier load stays: a list view that still
     * holds it would read a freed copy if it were replaced. */
    uint32_t bytes;
    if (!stock(Resource_ITEM, s->id + 3, &bytes))
        native_set(Resource_ITEM, s->id + 3, scratch, w.at);
    return 1;
}

/* The layouts and the screen of a list whose rows are in place: the layout
 * clones, the layout events (Menu pops) and the screen
 * with `events` as its CEVT. Ids: id screen, +1 layout, +2 title, +3 items, +4..
 * layout clones, then per row the item id and its source id (an indexed list has
 * one such row), then the split layout's screen, layout, pane and layout clones. */
static int clone_layouts(struct core_screen *s, int split, const void *events,
                         uint32_t event_bytes, uint8_t *scratch, uint32_t scratch_bytes)
{
    uint32_t screen = core_lists_screen_id(s, split),
             next = split ? screen + 2 : s->id + 4;
    uint32_t last = split ? CORE_LIST_PANE_CAPTION(screen) : CORE_SCREEN_ROWS(s);
    struct layout_clone c = {RES_Notes_List_Screen_Alt_Default,
                             screen + 1,
                             RES_StatusBarWhite_Text,
                             s->id + 2,
                             s->id + 3,
                             0,
                             0,
                             NULL,
                             0};
    if (!(split ? clone_split(s, screen, &next, scratch, scratch_bytes)
                : core_clone_layout(&c, &next, scratch, scratch_bytes)) ||
        next > last)
        return 0;
    if (s->declaration->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
        s->declaration->dynamic.empty &&
        (!core_list_empty(s, screen + 1, &next, scratch, scratch_bytes) || next > last))
        return 0;
    struct core_writer w = {scratch, 0};
    core_clone_layout_events(&w);
    native_set(Resource_SEVT, screen + 1, scratch, w.at);
    return core_clone_screen(screen, screen + 1, RES_SettingsMenu_Main_Screen,
                             split ? RES_Settings_MainMenu_Screen
                                   : RES_Notes_List_Screen_Alt,
                             events, event_bytes, scratch, scratch_bytes);
}

int core_clone_list(struct core_screen *s, int split)
{
    uint32_t count = indexed(s) ? 1 : s->declaration->list.count;
    /* The ITEM table, the layout clones and the SLst copy each fit the scratch
     * or the table's own size, whichever is larger; the row events go first. */
    uint32_t menu = core_screen_menu_events(&(struct core_writer){NULL, 0}, s);
    uint32_t pane = split && pane_follows(s->declaration)
                        ? CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1))
                        : 0;
    uint32_t bytes = 4 + count * (8 + ROW_BYTES);
    struct core_writer measure = {NULL, 0};
    write_row_events(&measure, CORE_SCREEN_ROWS(s), count, pane, menu);
    core_screen_menu_events(&measure, s);
    uint32_t events = measure.at;
    if (bytes < CORE_CLONE_SCRATCH_BYTES)
        bytes = CORE_CLONE_SCRATCH_BYTES;
    uint8_t *scratch = osos_import_alloc(events + bytes);
    if (!scratch)
        return 0;
    int built = s->built || write_rows(s, scratch);
    if (!s->built && menu)
        built = built && core_screen_menu_build(s, scratch, bytes);
    struct core_writer w = {scratch, 0};
    write_row_events(&w, CORE_SCREEN_ROWS(s), count, pane, menu);
    core_screen_menu_events(&w, s);
    built = built && clone_layouts(s, split, scratch, w.at, scratch + events, bytes);
    osos_import_free(scratch);
    return built;
}

static const void *resource(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return osos_resource_get(rvx_bank, type, id, bytes);
}

static const void *list_frame(const void *instances)
{
    struct core_block instance, view;
    for (uint32_t i = 0; core_block_at(instances, i, &instance); i++) {
        if (core_word_at(instance.data, SsinInstance_template) !=
            RES_SettingsList_Template)
            continue;
        uint32_t bytes;
        const void *layout = resource(
            Resource_VLyt, core_word_at(instance.data, SsinInstance_layout_events_id),
            &bytes);
        for (uint32_t v = 0; layout && core_block_at(layout, v, &view); v++)
            if (view.kind == RES_SettingsList_View)
                return view.data;
    }
    return NULL;
}

int core_list_empty(struct core_screen *s, uint32_t layout, uint32_t *next,
                    uint8_t *scratch, uint32_t bytes)
{
    uint32_t size, template = core_template(CORE_TEMPLATE_EMPTY);
    struct core_block list, prototype;
    const void *layouts = resource(Resource_SLyt, layout, &size);
    const void *original = resource(Resource_SSin, RES_Calendar_Empty_Instances, &size);
    if (!template || !layouts || !original || !core_block_at(layouts, 0, &list) ||
        !core_block_at(original, 1, &prototype))
        return 0;
    uint32_t instances_id = core_word_at(list.data, 0);
    const void *instances = resource(Resource_SSin, instances_id, &size);
    const void *frame_options = instances ? list_frame(instances) : NULL;
    if (!frame_options || size + prototype.bytes + 8 > bytes)
        return 0;
    uint32_t edges = 0;
    struct core_block edge;
    for (uint32_t i = 0; core_block_at(frame_options, i, &edge); i++)
        if (edge.kind >= ViewOption_TopEdge && edge.kind <= ViewOption_RightEdge)
            edges++;
    if (edges != 4)
        return 0;
    uint32_t binding = (*next)++;
    core_set_source(s->id + 3, s->id + 3, SourceDestination_Title);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, 1);
    uint32_t view = core_write_block_begin(&w, template + 1);
    core_write_word(&w, 1 + edges);
    uint32_t option = core_write_block_begin(&w, Resource_SORC);
    core_write_word(&w, s->id + 3);
    core_write_block_end(&w, option);
    for (uint32_t i = 0; core_block_at(frame_options, i, &edge); i++) {
        if (edge.kind < ViewOption_TopEdge || edge.kind > ViewOption_RightEdge)
            continue;
        option = core_write_block_begin(&w, edge.kind);
        core_write_bytes(&w, edge.data, edge.bytes);
        core_write_block_end(&w, option);
    }
    core_write_block_end(&w, view);
    native_set(Resource_VCvs, binding, scratch, w.at);
    const void *frame = resource(
        Resource_VCrv, core_word_at(prototype.data, SsinInstance_view_bindings), &size);
    if (frame)
        native_set(Resource_VCrv, binding, frame, size);
    w.at = 0;
    uint32_t count = core_blocks_count(instances);
    core_write_word(&w, count + 1);
    for (uint32_t i = 0; i < count; i++) {
        struct core_block instance;
        core_block_at(instances, i, &instance);
        uint32_t begin = core_write_block_begin(&w, instance.kind);
        core_write_bytes(&w, instance.data, instance.bytes);
        core_write_block_end(&w, begin);
    }
    uint32_t words[] = {SsinInstance_view_bindings, binding, SsinInstance_template,
                        template};
    uint32_t begin = core_write_block_begin(&w, prototype.kind);
    core_write_copy(&w, prototype.data, prototype.bytes, words, 2);
    core_write_block_end(&w, begin);
    native_set(Resource_SSin, instances_id, scratch, w.at);
    return 1;
}

uint32_t core_screen_menu_events(struct core_writer *w, const struct core_screen *s)
{
    const struct screen *d = s->declaration;
    if (!d->context_count)
        return 0;
    uint32_t first = CORE_LIST_CONTEXT(s);
    char name[40], handler[11];
    core_screens_handler_name(handler, first);
    core_write_event(w, "button.select.pressandhold", handler, NULL, NULL, 0);
    for (uint32_t i = 0; i < d->context_count && i < 16; i++) {
        core_screens_handler_name(handler, first + 1 + i);
        core_put_text(core_put_text(name, name + sizeof name, "contextualMenu."),
                      name + sizeof name, handler);
        core_write_event(w, name, handler, NULL, NULL, 0);
    }
    core_write_event(w, "contextualMenu.HandleCancel", "", NULL, NULL, 0);
    return 2 + (d->context_count < 16 ? d->context_count : 16);
}

int core_screen_menu_build(const struct core_screen *s, uint8_t *scratch,
                           uint32_t bytes)
{
    const struct screen *d = s->declaration;
    uint32_t first = CORE_LIST_CONTEXT(s);
    for (uint32_t i = 0; i < d->context_count && i < 16; i++) {
        const char *label = d->context_rows[i].row.label;
        uint32_t n = 0;
        while (label[n])
            n++;
        native_set(Resource_String, first + 1 + i, label, n + 1);
    }
    uint32_t screen = first + 1 + (d->context_count < 16 ? d->context_count : 16),
             layout = screen + 1, next = screen + 2;
    struct layout_clone clone = {RES_ContextMenu_Layout,
                                 layout,
                                 RES_StatusBarWhite_Text,
                                 s->id + 2,
                                 0,
                                 0,
                                 0,
                                 NULL,
                                 0};
    if (!core_clone_layout(&clone, &next, scratch, bytes) || next > screen + 4)
        return 0;
    uint32_t event_bytes, none = 0;
    const void *events = osos_resource_get(rvx_bank, Resource_SEVT,
                                           RES_ContextMenu_Layout, &event_bytes);
    if (!events)
        return 0;
    native_set(Resource_SEVT, layout, events, event_bytes);
    return core_clone_screen(screen, layout, RES_ContextMenu_Screen,
                             RES_ContextMenu_Screen, &none, sizeof none, scratch,
                             bytes);
}
