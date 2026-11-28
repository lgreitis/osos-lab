/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors,
 * based on RepriseOS UI resources by lgreitis.
 */

/* The preview panes of the menus with a sidebar. Every stock row of Main,
 * Music, Videos, Extras and Settings has a layout of its own whose pane shows
 * that row's picture and caption, and the row's delayedselected binding
 * switches to it. Rows extensions add share layouts instead, one per menu and
 * pane kind: their binding names the row's pane handler and switches to the
 * menu's pane name, which the handler points at the layout of the kind the
 * row's picture asks for, having made the row its current one; the layout's
 * pane views are bound to properties answered from the row (label, caption,
 * bitmap). The kind follows the picture's size, as Apple's panes do (Games on
 * Main shows the Extras pane): a 78 x 121 mask is a Settings icon, the other
 * sizes are the pictures of the kinds below. A menu's layout of a kind is
 * built on the first highlight of a row whose picture asks for it and added to
 * the menu's running controller, so a menu nobody highlights an extension row
 * on costs nothing. A music or video item without pane fields binds the stock
 * highlight handler instead (menus.c): Apple's handler emits the controller
 * event that switches to the stock count, artwork or empty pane, and refreshes
 * it when the artwork changes. */

#include "native_ui.h"

/* The pane kinds, each with the picture its pane shows, the stock template
 * that shows it and what each of the template's views shows, in the View
 * resource's order from `first` on (the icon pane's in the order
 * core_panes_write binds its VLyt). A kind every row of a menu uses (icon,
 * square) binds the stock template itself through the row layout's VLyt or
 * VCvs; a kind whose stock rows each have a template of their own gets a copy
 * of one of them (with a second template's views after it when the stock pane
 * is two instances), so the stock views stay untouched. */
enum {
    PANE_ICON,       /* a 78 x 121 mask on the Settings box, the label above */
    PANE_ICON_PLAIN, /* the same without the box: a plain icon, or no picture;
                        chosen by the row, never by a picture's size */
    PANE_SQUARE,     /* 119 x 119, the caption under it */
    PANE_WIDE,       /* 142 x 87, the caption under it */
    PANE_LINES_OVER, /* 130 x 128, the label above, a line and a number over
                        it, the caption under */
    PANE_BADGED,     /* 146 x 110, a count line with a badge before it under */
    PANE_CLOCK_BOX,  /* 132 x 145 in the World Clock pane's clock box, the
                        caption under */
    PANE_KINDS
};

/* What a view shows: CAPTION falls back to the label, CAPTION_ONLY stays empty
 * without one, for the kinds whose title line shows the label and the
 * 132 x 145 kind, which shows none. */
enum { PICTURE, CAPTION, CAPTION_ONLY, LABEL, LINE, NUMBER, BADGE, BOX };

#define MAX_VIEWS 5

static const struct kind {
    uint8_t width, height;
    uint8_t views;
    uint8_t roles[MAX_VIEWS];
    uint32_t template;
    uint32_t second;   /* template whose views the copy gets after `template`'s */
    uint32_t bindings; /* the shared template's VLyt; 0 for a kind that copies its
                          template */
    uint32_t layout;   /* stock row layout cloned in place of the menu's, for its
                          other instances */
    int8_t replaced;   /* view of the copy that becomes the picture view, 0 for none */
    uint8_t first;     /* view of the copy the roles start at */
} kinds[PANE_KINDS] = {
    /* The box is shown or hidden by the layout, as the stock row layouts do:
     * its view is shared by every Settings pane and keeps the last bitmap
     * bound to it, which the stock layouts never rebind. */
    [PANE_ICON] = {78,
                   121,
                   4,
                   {PICTURE, CAPTION_ONLY, LABEL, BOX},
                   .template = RES_SettingsPreview_Template},
    [PANE_ICON_PLAIN] = {0,
                         0,
                         3,
                         {PICTURE, CAPTION_ONLY, LABEL},
                         .template = RES_SettingsPreview_Template},
    [PANE_SQUARE] = {119,
                     119,
                     2,
                     {PICTURE, CAPTION},
                     .template = RES_MediaMenuPreview_Template,
                     .bindings = RES_MediaMenuPreview_Bindings},
    [PANE_WIDE] =
        {142, 87, 2, {PICTURE, CAPTION}, .template = RES_ExtrasPreview_Games_Template},
    [PANE_LINES_OVER] = {130,
                         128,
                         5,
                         {PICTURE, LINE, NUMBER, LABEL, CAPTION_ONLY},
                         .template = RES_ExtrasPreview_Calendars_Template,
                         .second = RES_ExtrasPreview_Calendars_Lines_Template},
    [PANE_BADGED] = {146,
                     110,
                     3,
                     {PICTURE, CAPTION, BADGE},
                     .template = RES_MainMenuPreview_Radio_Template},
    /* The date line and the clock view, which the copy makes an image view in
     * the clock's box. The stock Clocks pane draws neither the city nor the
     * region line before them, so the roles start after them. */
    [PANE_CLOCK_BOX] = {132,
                        145,
                        2,
                        {CAPTION_ONLY, PICTURE},
                        .template = RES_ExtrasPreview_Clocks_Template,
                        .layout = RES_ExtrasMenu_Clocks_Layout,
                        .replaced = 3,
                        .first = 2},
};

static uint32_t copies[PANE_KINDS]; /* the template copies made so far */

/* A menu with a pane, in the order of menus.c's slots: the stock row layout
 * its shared layouts clone, the screen whose SLst lists it, the pane
 * instance's template in that layout, the kind a row without a picture gets,
 * the layout name the rows' bindings switch to and the offset at which the
 * menu's controller class embeds its screen definition. The icon pane is
 * rebound through a VLyt of its views, the others through the pane instance's
 * VCvs, binding the template's views from its first image view on. */
struct menu {
    uint8_t kind;
    uint16_t definition_offset;
    uint32_t stock;
    uint32_t screen;
    uint32_t template;
    const char *name;
};

static const struct menu menus[] = {
    {PANE_SQUARE, MainMediaMenuController_screen_definition,
     RES_MainMenus_Main_Radio_Layout, RES_MainMenus_Main_Screen,
     RES_MainMenuPreview_Radio_Template, "RVX_MainPane"},
    {PANE_SQUARE, MainMediaMenuController_screen_definition, RES_MusicMenu_Songs_Layout,
     RES_MainMenus_Music_Screen, RES_MediaMenuPreview_Template, "RVX_MusicPane"},
    {PANE_WIDE, ExtrasMenuController_screen_definition, RES_ExtrasMenu_Games_Layout,
     RES_ExtrasMenu_Screen, RES_ExtrasPreview_Games_Template, "RVX_ExtrasPane"},
    {PANE_ICON, SettingsMenuListController_screen_definition,
     RES_SettingsMenus_Legal_Layout, RES_SettingsMenu_Main_Screen,
     RES_SettingsPreview_Template, "RVX_SettingsPane"},
    {PANE_SQUARE, MainMediaMenuController_screen_definition,
     RES_VideosMenu_Movies_Layout, RES_VideosMenu_Screen, RES_MediaMenuPreview_Template,
     "RVX_VideosPane"},
};
#define MENUS (sizeof menus / sizeof menus[0])
#define SETTINGS_SLOT 3

/* A shared layout built so far, one per menu and kind. */
struct pane_layout {
    struct pane_layout *next;
    uint8_t menu, kind;
    uint32_t layout;  /* the layout's ids follow it */
    uint32_t current; /* item id of the row the pane shows */
    /* The stock row layout's SLst entry with the clone's id: every Layout
     * built for the clone keeps a pointer to it. */
    uint32_t record[ScreenLayoutEntry_bytes / 4];
};

static struct pane_layout *panes;
static uint32_t pointed[MENUS]; /* the layout each menu's pane name points at */

/* Ids after a layout: three for its SSin, its pane VLyt or VCvs and a status
 * bar VCvs, then a source per pane view, in the kind's view order. */
#define SOURCES(p) ((p)->layout + 4)
#define IDS (4 + MAX_VIEWS)

/* The row each menu highlighted last and what its pane shows: the decoded
 * picture and badge, the extension's own copies, kept while the menu is open
 * because an image view keeps the BMap it was handed and draws it again
 * until it is asked for another, and freed only after the views were asked
 * for their replacements. A Settings icon is copied into one buffer that
 * outlives them (rvx_native_icon). */
static struct shown {
    Pkg *pkg;
    const struct menu_entry *entry;
    void *controller;
    uint32_t item;
    void *picture, *badge; /* NULL for a Settings icon, which native_icon holds */
    int kind;              /* the kind the picture asks for, -1 for none */
} shown[MENUS];

enum { ICON_WORDS = (28 + 80 * 121) / 4 };

static u32 *native_icon;

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return osos_resource_get(rvx_bank, type, id, bytes);
}

static void notify(uint32_t id)
{
    void *model = NATIVE_CALL(SettingsModel_GetInstance)();
    if (model)
        osos_model_notify(model, OSOS_NOTIFY_ALL, id);
}

const char *core_panes_name(unsigned slot)
{
    return slot < MENUS ? menus[slot].name : NULL;
}

/* The menus the MainMediaMenu controller runs, the ones with media items. */
static int media_menu(unsigned slot)
{
    return menus[slot].definition_offset == MainMediaMenuController_screen_definition;
}

static struct pane_layout *pane_of(const struct menu *m, uint32_t kind)
{
    for (struct pane_layout *p = panes; p; p = p->next)
        if (&menus[p->menu] == m && p->kind == kind)
            return p;
    return NULL;
}

/* Word `field` of a stock layout's instance of `template`, 0 for none. */
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

/* The title source a stock layout's status bar text binds, 0 for none. */
static uint32_t title_source(uint32_t layout_id)
{
    uint32_t bytes;
    uint32_t vcvs_id = instance_word(layout_id, RES_StatusBarWhite_Template,
                                     SsinInstance_view_bindings);
    const void *vcvs = vcvs_id ? stock(Resource_VCvs, vcvs_id, &bytes) : NULL;
    struct core_block view, option;
    if (!vcvs || !core_block_at(vcvs, 0, &view) || view.kind != RES_StatusBarWhite_Text)
        return 0;
    for (uint32_t o = 0; core_block_at(view.data, o, &option); o++)
        if (option.kind == Resource_SORC)
            return core_word_at(option.data, 0);
    return 0;
}

/* The first image view of a stock template: its id, 0 for none. */
static uint32_t image_view(uint32_t template)
{
    uint32_t bytes;
    struct core_block view;
    const void *views = stock(Resource_View, template, &bytes);
    for (uint32_t i = 0; views && core_block_at(views, i, &view); i++)
        if (core_word_at(view.data, ViewDescriptor_type) == ViewType_Image)
            return core_word_at(view.data, ViewDescriptor_id);
    return 0;
}

/* The icon pane of a clone: a VLyt binding the views, whose sources follow
 * the kind's role order: the icon view, the line under it, the title line above
 * it and, when `boxed`, the box behind the icon; otherwise the box is hidden. */
static int icon_pane(const struct menu *m, uint32_t id, struct layout_clone *c,
                     struct core_writer *w, int boxed)
{
    uint32_t sources = id + 4, views = boxed ? 4 : 3;
    uint32_t stock_pane =
        core_panes_write(w, sources + 2, sources + 1, sources, boxed ? sources + 3 : 0);
    c->from = pane_layout(m->stock, m->template);
    c->to = id + 2;
    if (!stock_pane || !c->from)
        return 0;
    core_panes_store(c->to, w->out, w->at, c->from);
    for (uint32_t i = 0; i < views; i++)
        core_set_source(sources + i, sources + i, SourceDestination_Title);
    return 1;
}

/* A picture pane of a clone: the kind's template in the pane instance's place,
 * its picture and caption views bound through the instance's VCvs. */
static int picture_pane(uint32_t kind, uint32_t id, struct layout_clone *c,
                        struct view_sources *views)
{
    const struct kind *k = &kinds[kind];
    uint32_t sources = id + 4;
    if (k->bindings) {
        c->to = k->template;
        c->bindings = k->bindings;
        views->view = image_view(k->template);
    } else {
        if (!copies[kind])
            copies[kind] = core_templates_copy(k->template, k->second,
                                               k->replaced ? k->replaced : -1);
        if (!copies[kind])
            return 0;
        c->to = copies[kind];
        /* The instance needs a layout events id with a TEVT, or loading the
         * layout's events aborts; Games' has one and no VLyt that would rebind
         * the copy's views, so every copy takes it. */
        c->bindings =
            pane_layout(RES_ExtrasMenu_Games_Layout, RES_ExtrasPreview_Games_Template);
        views->view = copies[kind] + 1 + k->first;
    }
    views->source = sources;
    views->count = k->views;
    c->extra = views;
    for (uint32_t i = 0; i < k->views; i++)
        core_set_source(sources + i, sources + i, SourceDestination_Title);
    return views->view != 0;
}

/* The screen definition of the menu's controller on screen, NULL when
 * `controller` runs another screen. */
static uint8_t *definition_of(const struct menu *m, void *controller)
{
    uint8_t *definition = (uint8_t *)controller + m->definition_offset;
    return core_word_at(definition, ScreenDefinition_screen) == m->screen ? definition
                                                                          : NULL;
}

/* A shared layout: the menu's stock row layout with its pane rebound for the
 * kind, listed in the menu screen's SLst. Apple reads the SLst on every push
 * and, after every layout change, builds the screen's definition from it
 * again to look the shown layout up, so a layout only a controller knows is
 * not enough. Its events are a copy of the stock layout's under its own id,
 * where cfw_ui_load_events reads them. NULL when it cannot be built. */
static struct pane_layout *build(const struct menu *m, uint32_t kind, void *controller)
{
    uint32_t slst_bytes, entry_bytes, event_bytes;
    /* A kind on another menu's row layout carries this menu's title in its
     * status bar; a menu whose status bar text binds no source (Main) cannot
     * have it. */
    uint32_t title = kinds[kind].layout ? title_source(m->stock) : 0;
    uint32_t cloned = kinds[kind].layout ? kinds[kind].layout : m->stock;
    const void *slst = stock(Resource_SLst, m->screen, &slst_bytes);
    const void *events = stock(Resource_SEVT, cloned, &event_bytes);
    const uint8_t *entry =
        slst ? core_block_with_word(slst, 0, m->stock, &entry_bytes) : NULL;
    if (!entry || !events || entry_bytes < ScreenLayoutEntry_bytes ||
        (kinds[kind].layout && !title))
        return NULL;
    uint32_t scratch_bytes = slst_bytes + 16 > CORE_CLONE_SCRATCH_BYTES
                                 ? slst_bytes + 16
                                 : CORE_CLONE_SCRATCH_BYTES;
    struct pane_layout *p = osos_import_alloc(sizeof *p);
    uint32_t id = p ? core_resource_take(IDS) : 0;
    uint8_t *scratch = id ? osos_import_alloc(scratch_bytes) : NULL;
    if (!scratch) {
        if (p)
            osos_import_free(p);
        return NULL;
    }
    uint32_t next = id + 1;
    struct core_writer w = {scratch, 0};
    struct layout_clone c = {m->stock, id, 0, 0, 0, m->template, m->template, NULL, 0};
    if (title) {
        c.stock = cloned;
        c.from = kinds[kind].template;
        c.title_view = RES_StatusBarWhite_Text;
        c.title = title;
    }
    struct view_sources views;
    int built = kind == PANE_ICON || kind == PANE_ICON_PLAIN
                    ? icon_pane(m, id, &c, &w, kind == PANE_ICON)
                    : picture_pane(kind, id, &c, &views);
    built =
        built && core_clone_layout(&c, &next, scratch, scratch_bytes) && next <= id + 4;
    if (!built) {
        osos_import_free(scratch);
        osos_import_free(p);
        return NULL;
    }
    native_set(Resource_SEVT, id, events, event_bytes);
    for (uint32_t i = 0; i < sizeof p->record / 4; i++)
        p->record[i] = core_word_at(entry, 4 * i);
    p->record[ScreenLayoutEntry_layout / 4] = id;
    /* The stock SLst with one more block: the clone's record. */
    w.at = 0;
    core_write_word(&w, core_blocks_count(slst) + 1);
    core_write_bytes(&w, (const uint8_t *)slst + 4, slst_bytes - 4);
    uint32_t begin = core_write_block_begin(&w, 0);
    core_write_bytes(&w, p->record, sizeof p->record);
    core_write_block_end(&w, begin);
    native_set(Resource_SLst, m->screen, scratch, w.at);
    osos_import_free(scratch);
    /* Storing the SLst freed its previous copy, where every Layout the
     * controller on screen built at push keeps its record. The new copy keeps
     * the blocks at their offsets, so the records move to it. */
    const uint8_t *fresh = stock(Resource_SLst, m->screen, &slst_bytes);
    uint8_t *definition = definition_of(m, controller);
    struct core_block block;
    for (uint32_t i = 0; fresh && definition && core_block_at(fresh, i, &block); i++) {
        uint32_t layout_id = core_word_at(block.data, ScreenLayoutEntry_layout);
        void **slot = NATIVE_CALL(UIntPointerMap_GetOrInsert)(
            definition + ScreenDefinition_layout_cache, &layout_id);
        if (*slot)
            RV_FIELD(*slot, Layout_screen_list_record, const void *) = block.data;
    }
    p->menu = (uint8_t)(m - menus);
    p->kind = (uint8_t)kind;
    p->layout = id;
    p->current = 0;
    p->next = panes;
    panes = p;
    return p;
}

/* The menu's layout of a kind, built on first use. */
static struct pane_layout *pane_for(const struct menu *m, uint32_t kind,
                                    void *controller)
{
    struct pane_layout *p = pane_of(m, kind);
    return p ? p : build(m, kind, controller);
}

/* Adds the layout to the menu's running controller the way UI_LoadScreenLayouts
 * adds a screen's own at push: the controller's definition keeps a Layout per
 * id, freed with the controller, and navigator.SwitchLayout looks the id up
 * there. The lookup inserts an empty entry, which is filled here. 1 when the
 * controller knows the layout. */
static int attach(const struct pane_layout *p, void *controller)
{
    uint8_t *definition = definition_of(&menus[p->menu], controller);
    if (!definition)
        return 0;
    uint32_t layout_id = p->layout;
    void **slot = NATIVE_CALL(UIntPointerMap_GetOrInsert)(
        definition + ScreenDefinition_layout_cache, &layout_id);
    if (*slot)
        return 1;
    uint8_t *layout = NATIVE_CALL(Runtime_OperatorNew)(Layout_bytes);
    if (!layout)
        return 0;
    layout = NATIVE_CALL(UI_ConstructLayout)(
        layout, p->layout, (uint8_t)p->record[ScreenLayoutEntry_kind / 4], p->record);
    RV_FIELD(layout, Layout_screen, void *) = definition;
    *slot = layout;
    return 1;
}

static void notify_pane(const struct pane_layout *p)
{
    for (uint32_t i = 0; i < kinds[p->kind].views; i++)
        notify(SOURCES(p) + i);
}

/* The pane kind a decoded picture asks for by its size, -1 for none. */
static int kind_of(const void *bmap, uint32_t bytes)
{
    if (!bmap || bytes < BMap_bounds + 16)
        return -1;
    const int32_t *bounds = (const int32_t *)((const uint8_t *)bmap + BMap_bounds);
    for (int k = 0; k < PANE_KINDS; k++)
        if (bounds[2] - bounds[0] == kinds[k].height &&
            bounds[3] - bounds[1] == kinds[k].width)
            return k;
    return -1;
}

const void *rvx_native_icon(const u32 *image, u32 bytes)
{
    if (!rvx_is_pane_icon(image, bytes))
        return NULL;
    if (!native_icon)
        native_icon = osos_import_alloc(bytes);
    if (native_icon)
        for (unsigned j = 0; j < ICON_WORDS; j++)
            native_icon[j] = image[j];
    return native_icon;
}

/* The shown row's text for a line role, "" for none. */
static const char *line_of(const struct shown *s, uint32_t role)
{
    const struct menu_entry *e = s->entry;
    struct context ctx = {s->pkg, s->controller, NULL, NULL, -1, NULL};
    const struct pane *pane = e->pane;
    const struct pane_extra *extra = e->extra;
    const char *text = NULL;
    if ((role == CAPTION || role == CAPTION_ONLY) && pane && pane->text)
        text = pane->text(&ctx);
    else if ((role == CAPTION || role == CAPTION_ONLY) && pane && pane->label)
        text = pane->label;
    else if ((role == CAPTION || role == CAPTION_ONLY) && e->caption)
        text = rvx_setting_label(s->pkg, e->caption);
    else if (role == LINE && extra)
        text = extra->line_text ? extra->line_text(&ctx) : extra->line;
    else if (role == NUMBER && extra)
        text = extra->number_text ? extra->number_text(&ctx) : extra->number;
    if (!text && (role == CAPTION || role == LABEL))
        text = rvx_entry_label(e, s->pkg);
    return text ? text : "";
}

/* Decodes the shown row's picture and badge again, or only what a caption
 * change can move: the ones the entry picks at run time. A Settings icon goes
 * to native_icon and its copy is freed. The replaced copies go to `old`, two
 * entries, for the caller to free once the views were asked again. */
static void load(struct shown *s, int caption_change, void **old)
{
    const struct menu_entry *e = s->entry;
    struct context ctx = {s->pkg, s->controller, NULL, NULL, -1, NULL};
    const struct pane *pane = e->pane;
    const struct pane_extra *extra = e->extra;
    u32 bytes = 0;
    old[0] = old[1] = NULL;
    if (!caption_change || (pane && pane->image)) {
        const struct bitmap *b =
            pane ? (pane->image ? pane->image(&ctx) : pane->bitmap) : NULL;
        old[0] = s->picture;
        s->picture = b ? rvx_bitmap_load(s->pkg, b->name, &bytes) : NULL;
        s->kind = kind_of(s->picture, bytes);
        if (s->kind == PANE_ICON) {
            if (!rvx_native_icon(s->picture, bytes))
                s->kind = -1;
            rvx_free(s->pkg, s->picture);
            s->picture = NULL;
        }
    }
    if (!caption_change || (extra && extra->image)) {
        const struct bitmap *b =
            extra ? (extra->image ? extra->image(&ctx) : extra->badge) : NULL;
        old[1] = s->badge;
        s->badge = b ? rvx_bitmap_load(s->pkg, b->name, &bytes) : NULL;
    }
}

static void free_old(Pkg *pkg, void **old)
{
    for (unsigned i = 0; i < 2; i++)
        if (old[i])
            rvx_free(pkg, old[i]);
}

/* Asks the views of the panes showing `s`'s row again. */
static void notify_shown(const struct shown *s)
{
    for (struct pane_layout *l = panes; l; l = l->next)
        if (&shown[l->menu] == s && l->current == s->item)
            notify_pane(l);
}

/* The menu `controller` shows row `e` on: the one menu of the entry's mask, or
 * among several the one whose screen the controller's definition names. */
static unsigned slot_of(const struct menu_entry *e, void *controller)
{
    unsigned slot = MENUS;
    for (unsigned s = 0; s < MENUS; s++)
        if (e->menus & 1u << s)
            slot = slot == MENUS || definition_of(&menus[s], controller) ? s : slot;
    return slot;
}

void core_panes_show(uint32_t item, void *controller)
{
    Pkg *pkg;
    const struct menu_entry *e = rvx_menu_row(item, &pkg);
    unsigned slot = e ? slot_of(e, controller) : MENUS;
    if (slot >= MENUS)
        return;
    const struct menu *m = &menus[slot];
    struct shown *s = &shown[slot];
    Pkg *previous = s->pkg;
    void *old[2];
    s->pkg = pkg;
    s->entry = e;
    s->controller = controller;
    s->item = item;
    load(s, 0, old);
    /* An icon is a Settings pane: on another menu the icon pane would write the
     * Settings preview over that menu's own instance. There the menu's pane
     * shows it instead. */
    int kind = s->kind;
    if (kind == PANE_ICON && slot != SETTINGS_SLOT)
        kind = -1;
    if ((kind == PANE_ICON || (kind < 0 && m->kind == PANE_ICON)) &&
        (kind < 0 || e->plain))
        kind = PANE_ICON_PLAIN;
    struct pane_layout *p = kind >= 0 ? pane_for(m, (uint32_t)kind, controller) : NULL;
    if (!p)
        p = pane_for(m, m->kind, controller);
    if (p)
        p->current = item;
    /* Each stock highlight handler records its pane's kind here and the
     * controller re-runs the handler of the recorded kind when the artwork
     * service's model changes, which would switch the pane away from this row;
     * 0, the kind of the stock rows with a fixed pane, re-runs nothing. */
    if (media_menu(slot))
        RV_FIELD(controller, MainMediaMenu_preview_kind, uint8_t) = 0;
    /* The binding's navigator.SwitchLayout runs after this handler and looks
     * the menu's pane name up then. A layout the controller does not know
     * would abort the switch, so the stock row's layout stands in when the
     * pane could not be built or added. */
    uint32_t layout = p && attach(p, controller) ? p->layout : m->stock;
    if (pointed[slot] != layout) {
        uint32_t name[CppString_bytes / 4], allocator = 0, length = 0;
        while (m->name[length])
            length++;
        NATIVE_CALL(CppString_InitBytes)(name, m->name, length, &allocator);
        *NATIVE_CALL(ResourceNames_FindOrInsertSlot)((void *)ADDR_ResourceNameMap,
                                                     name) = layout;
        NATIVE_CALL(CppString_Destroy)(name);
        pointed[slot] = layout;
    }
    if (p)
        notify_pane(p);
    free_old(previous, old);
}

int core_panes_refresh(Pkg *p, int caption_change)
{
    int found = 0;
    for (unsigned slot = 0; slot < MENUS; slot++) {
        struct shown *s = &shown[slot];
        void *old[2];
        if (!s->entry || (p && s->pkg != p))
            continue;
        load(s, caption_change, old);
        notify_shown(s);
        free_old(s->pkg, old);
        found = 1;
    }
    return found;
}

int core_panes_setting_changed(Pkg *p, uint16_t key)
{
    for (unsigned slot = 0; slot < MENUS; slot++)
        if (shown[slot].entry && shown[slot].pkg == p &&
            shown[slot].entry->caption == key)
            return core_panes_refresh(p, 1);
    return 0;
}

int core_panes_property(uint32_t type, uint32_t id, uintptr_t *out)
{
    struct pane_layout *p = panes;
    while (p && id - SOURCES(p) >= kinds[p->kind].views)
        p = p->next;
    if (!p)
        return 0;
    uint32_t kind = p->kind, role = kinds[kind].roles[id - SOURCES(p)];
    const struct shown *s = &shown[p->menu];
    int current = s->entry && p->current == s->item;
    if (role == PICTURE && type == Property_Bitmap) {
        int icon = kind == PANE_ICON || kind == PANE_ICON_PLAIN;
        const void *picture = NULL;
        if (current && icon && s->kind == PANE_ICON)
            picture = native_icon;
        else if (current && !icon && s->kind == (int)kind)
            picture = s->picture; /* another size the view would clip: the
                                     caption stands alone */
        *out = (uintptr_t)(picture ? picture : rvx_clear_bitmap);
    } else if (role == BADGE && type == Property_Bitmap)
        *out = (uintptr_t)(current && s->badge ? s->badge : rvx_clear_bitmap);
    else if (role == BOX && type == Property_Bitmap)
        *out = osos_ui_bitmap(RES_SettingsPreview_Background_Image);
    else if (role != PICTURE && role != BADGE && role != BOX && type == Property_String)
        *out = (uintptr_t)(current ? line_of(s, role) : "");
    else
        return 0;
    return 1;
}
