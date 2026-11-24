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

/* The empty string the text views of the text page and the empty message are
 * bound to until a screen rebinds them; made for the first template that
 * needs it. */
static uint32_t empty_string(void)
{
    static uint32_t id;
    if (!id && (id = core_resource_take(1)) != 0) {
        uint32_t source[6] = {
            1, 12, Resource_SORC, Service_String, id, SourceDestination_Title};
        set(Resource_SORC, id, source, sizeof source);
        set(Resource_String, id, "", 1);
    }
    return id;
}

/* Writes the View resource of a template whose first id is `id`; 0 when a
 * stock view it copies is missing. */
typedef int (*template_writer)(struct core_writer *w, uint32_t id, const void *views);

/* A TMLT's record for layout kind `kind`, or NULL. */
static uint8_t *style_record(const void *tmlt, uint32_t kind)
{
    struct core_block block;
    if (!tmlt || !core_block_at(tmlt, 0, &block) ||
        block.bytes < TemplateResource_styles + (kind + 1) * TemplateStyle_bytes)
        return NULL;
    return (uint8_t *)block.data + TemplateResource_styles + kind * TemplateStyle_bytes;
}

int16_t core_templates_enter_style(uint32_t template, uint32_t kind)
{
    uint32_t bytes;
    const uint8_t *record = style_record(stock(Resource_TMLT, template, &bytes), kind);
    if (!record)
        return -1;
    return (int16_t)(record[TemplateStyle_enter] | record[TemplateStyle_enter + 1]
                                                       << 8);
}

/* Stock firmware gives the Brightness template enter and leave styles only for
 * a split list (the window slides in from the right and back out); template
 * `id`'s copy gets them for every list kind a core screen opens from. The
 * bank's copy is changed in place, before the template is built from it. */
static void slide_like_brightness(uint32_t id)
{
    static const uint8_t kinds[] = {LayoutKind_Menu, LayoutKind_SplitList,
                                    LayoutKind_List};
    uint32_t bytes;
    const uint8_t *from =
        style_record(stock(Resource_TMLT, RES_Settings_Brightness_Template, &bytes),
                     LayoutKind_SplitList);
    const void *copy = stock(Resource_TMLT, id, &bytes);
    struct core_block block;
    if (!from || !copy || !core_block_at(copy, 0, &block))
        return;
    for (uint32_t i = 0; i < sizeof kinds; i++) {
        uint8_t *to = style_record(copy, kinds[i]);
        if (!to)
            return;
        for (uint32_t b = 0; b < 2; b++) {
            to[TemplateStyle_enter + b] = from[TemplateStyle_enter + b];
            to[TemplateStyle_leave + b] = from[TemplateStyle_leave + b];
        }
    }
    ((uint8_t *)block.data)[TemplateResource_has_styles] = 1;
}

/* Builds the template of View `id` as UI_InitializeViewTemplates builds each
 * stock one: a zeroed options block whose edge offsets are the owner's bounds
 * (its parent's when it has one), the owner as event target when it has one,
 * and the standard kind. The owner's children are walked in order when a
 * template is looked up, so one appended now is found. 0 when Apple hands no
 * template view back.
 *
 * A template's views attach while their parent is still unknown to them, so
 * they wait for the owner's enable and show passes, which at UI start follow
 * the enumeration; the first layout then hides every template it does not
 * show. A template built now gets the same three steps, or its views never
 * leave the waiting state and a layout showing them finds nothing to show. */
static int construct(uint32_t id)
{
    uint8_t *owner = RV_FIELD(NATIVE_CALL(UI_GetLayoutManager)(),
                              LayoutManager_template_owner, uint8_t *);
    uint8_t *parent = RV_FIELD(owner, View_parent, uint8_t *);
    const uint8_t *bounds = (parent ? parent : owner) + View_bounds;
    uint32_t *options = osos_import_alloc(ViewOptions_bytes);
    if (!options)
        return 0;
    options[ViewOptions_left_offset / 4] = RV_FIELD(bounds, Rect_left, uint32_t);
    options[ViewOptions_top_offset / 4] = RV_FIELD(bounds, Rect_top, uint32_t);
    options[ViewOptions_bottom_offset / 4] = RV_FIELD(bounds, Rect_bottom, uint32_t);
    options[ViewOptions_right_offset / 4] = RV_FIELD(bounds, Rect_right, uint32_t);
    int (*has_target)(void *) =
        RV_FIELD(RV_FIELD(owner, 0, void **), View_has_event_target_method, void *);
    void *built = NATIVE_CALL(UI_CreateTemplateFromResource)(
        RV_FIELD(owner, TemplateOwner_context, void *), owner,
        has_target(owner) ? owner : NULL, options, id, 0);
    osos_import_free(options);
    uint8_t *view =
        built ? NATIVE_CALL(Object_GetInterface)(built, Interface_Template) : NULL;
    if (!view)
        return 0;
    view[TemplateView_kind] = TemplateKind_Standard;
    uint8_t *root = RV_FIELD(view, View_parent, uint8_t *);
    void **methods = RV_FIELD(root, 0, void **);
    void (*enabled)(void *, int) =
        RV_FIELD(methods, View_parent_enabled_method, void *);
    void (*shown)(void *, int) = RV_FIELD(methods, View_parent_shown_method, void *);
    void (*show)(void *, int) = RV_FIELD(methods, View_show_method, void *);
    enabled(root, 1);
    shown(root, 1);
    show(root, 0);
    return 1;
}

/* The stock Settings pane keeps its background BMap between layouts. Give RVX
 * its own instance so hiding a row's artwork cannot clear the stock tile. */
uint32_t core_pane_template(void)
{
    static uint32_t pane;
    if (pane)
        return pane;
    uint32_t id = core_resource_take(1), size;
    if (!id)
        return 0;
    const uint32_t types[] = {Resource_View, Resource_TMLT};
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        const void *data = stock(types[i], RES_SettingsPreview_Template, &size);
        if (!data)
            return 0;
        set(types[i], id, data, size);
    }
    if (construct(id))
        pane = id;
    return pane;
}

/* Takes `ids` ids, writes the View with `write` (measured first), stores it
 * with the stock template's TMLT and builds the template from them. The bank
 * keeps the only copy of the View: the template builds its child views from
 * it. 0 when a stock resource is missing or the heap is full. */
static uint32_t build(uint32_t stock_template, uint32_t ids, template_writer write)
{
    uint32_t bytes;
    const void *views = stock(Resource_View, stock_template, &bytes);
    struct core_writer w = {NULL, 0};
    if (!views || !write(&w, 0, views))
        return 0;
    uint32_t id = core_resource_take(ids);
    if (!id)
        return 0;
    w.out = osos_import_alloc(w.at);
    if (!w.out)
        return 0;
    w.at = 0;
    write(&w, id, views);
    set(Resource_View, id, w.out, w.at);
    osos_import_free(w.out);
    const void *tmlt = stock(Resource_TMLT, stock_template, &bytes);
    if (tmlt)
        set(Resource_TMLT, id, tmlt, bytes);
    return id;
}

/* Text page: the stock Legal template's second block is a paragraph, its last
 * the bottom margin; each paragraph hangs below the one before. View offsets
 * count from the template's top edge under the title bar, so the first
 * paragraph keeps the stock page's 15 px top margin. */
#define TEXT_TOP 15
#define PARAGRAPH_GAP 12

static int write_text(struct core_writer *w, uint32_t t, const void *views,
                      uint32_t rows, uint32_t count)
{
    struct core_block paragraph, margin;
    if (!core_block_at(views, 1, &paragraph) ||
        !core_block_at(views, core_blocks_count(views) - 1, &margin))
        return 0;
    core_write_word(w, count + 1);
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = t + 1 + i;
        uint32_t words[10] = {ViewDescriptor_top_offset,
                              previous ? PARAGRAPH_GAP : TEXT_TOP,
                              ViewDescriptor_top_reference_view,
                              previous,
                              ViewDescriptor_top_anchor_mode,
                              previous ? AnchorMode_Bottom : 0,
                              ViewDescriptor_id,
                              id,
                              TextViewDescriptor_source,
                              rows + i};
        uint32_t begin = core_write_block_begin(w, paragraph.kind);
        core_write_copy(w, paragraph.data, paragraph.bytes, words, 5);
        core_write_block_end(w, begin);
        previous = id;
    }
    uint32_t words[4] = {ViewDescriptor_top_reference_view, previous, ViewDescriptor_id,
                         t + 1 + count};
    uint32_t begin = core_write_block_begin(w, margin.kind);
    core_write_copy(w, margin.data, margin.bytes, words, 2);
    core_write_block_end(w, begin);
    return 1;
}

/* The Calendar empty-day message without its background image. */
static int write_empty(struct core_writer *w, uint32_t t, const void *views)
{
    uint32_t bytes;
    const void *text =
        core_block_with_word(views, ViewDescriptor_id, RES_Calendar_Empty_Text, &bytes);
    uint32_t source = empty_string();
    if (!text || !source)
        return 0;
    core_write_word(w, 1);
    uint32_t words[] = {ViewDescriptor_id, t + 1, TextViewDescriptor_source, source};
    uint32_t begin = core_write_block_begin(w, 0);
    core_write_copy(w, text, bytes, words, 2);
    core_write_block_end(w, begin);
    return 1;
}

/* Slider: the Brightness bar and value text with new ids. They keep the stock
 * sources, the brightness properties Apple's own template binds at boot. */
static int write_slider(struct core_writer *w, uint32_t t, const void *views)
{
    static const uint32_t stock_views[2] = {RES_Settings_Brightness_Bar,
                                            RES_Settings_Brightness_Value_Text};
    static const uint32_t types[2] = {ViewType_Progress, ViewType_Text};
    core_write_word(w, 2);
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t bytes;
        const uint8_t *view =
            core_block_with_word(views, ViewDescriptor_id, stock_views[i], &bytes);
        if (!view || core_word_at(view, ViewDescriptor_type) != types[i])
            return 0;
        uint32_t words[2] = {ViewDescriptor_id, t + 1 + i};
        uint32_t begin = core_write_block_begin(w, types[i]);
        core_write_copy(w, view, bytes, words, 1);
        core_write_block_end(w, begin);
    }
    return 1;
}

uint32_t core_text_template(struct core_screen *s)
{
    uint32_t count = s->declaration->text.count, size;
    const void *views = stock(Resource_View, RES_Settings_Legal_Template, &size);
    if (!views)
        return 0;
    struct core_writer w = {0};
    uint32_t rows = CORE_SCREEN_ROWS(s);
    if (!write_text(&w, 0, views, rows, count))
        return 0;
    uint32_t id = core_resource_take(count + 2);
    if (!id || !(w.out = osos_import_alloc(w.at)))
        return 0;
    w.at = 0;
    write_text(&w, id, views, rows, count);
    set(Resource_View, id, w.out, w.at);
    osos_import_free(w.out);
    const void *tmlt = stock(Resource_TMLT, RES_Settings_Legal_Template, &size);
    if (!tmlt)
        return 0;
    set(Resource_TMLT, id, tmlt, size);
    return construct(id) ? id : 0;
}

static const struct {
    uint32_t stock, ids;
    template_writer write;
    uint8_t slides;
} templates[CORE_TEMPLATES] = {
    [CORE_TEMPLATE_SLIDER] = {RES_Settings_Brightness_Template, 3, write_slider, 1},
    [CORE_TEMPLATE_EMPTY] = {RES_Calendar_Empty_Template, 2, write_empty, 0},
};

static uint32_t built[CORE_TEMPLATES]; /* 0 until built */

uint32_t core_template(enum core_template which)
{
    if (built[which])
        return built[which];
    uint32_t id =
        build(templates[which].stock, templates[which].ids, templates[which].write);
    if (id && templates[which].slides)
        slide_like_brightness(id);
    if (id && construct(id))
        built[which] = id;
    return built[which];
}
