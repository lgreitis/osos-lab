/* SPDX-License-Identifier: GPL-3.0-only */
#include "rv_menus.h"
#include "rv_ui.h"

enum { MODE = 1, LEVEL, FLAGS, SWITCH, PLAYING };
static const struct rv_api *api;
static const char *const labels[] = {"First", "Second", "Third"};
static const char *const levels[] = {"Minimum", "Low", "Medium", "High", "Maximum"};
static const char *const tracks[] = {
    "Morning", "Daybreak", "Horizon",  "Sunlight", "Midday",   "Afternoon",
    "Evening", "Dusk",     "Twilight", "Night",    "Midnight", "First Light"};
static const char *const paragraphs[] = {
    "A short paragraph.",
    "This longer paragraph wraps across several lines, so you can compare line spacing "
    "and margins with the short paragraph above.",
    "Punctuation and accents: café, déjà vu, 50%, (live).",
    "The remaining paragraphs extend past the bottom of the display. Turn the wheel to "
    "read them.",
    "Morning gives way to afternoon. The light fades, and the last track on the album "
    "comes to an end.",
    "End of the text page."};

static void toggle(const struct rv_context *ctx)
{
    api->setting_set_int(ctx->self, SWITCH, !api->setting_get_int(ctx->self, SWITCH));
    api->ui_refresh(ctx->self, NULL);
}

static const char *status(const struct rv_context *ctx)
{
    return api->setting_get_int(ctx->self, SWITCH) ? "Switch is on" : "Switch is off";
}

static int is_on(const struct rv_context *ctx)
{
    return api->setting_get_int(ctx->self, SWITCH) != 0;
}

static uint32_t count(const struct rv_context *ctx)
{
    (void)ctx;
    return RV_COUNT(tracks);
}

static uint32_t empty_count(const struct rv_context *ctx)
{
    (void)ctx;
    return 0;
}

static uint32_t initial(const struct rv_context *ctx)
{
    return api->setting_get_int(ctx->self, PLAYING);
}

static enum rv_row_status playing(const struct rv_context *ctx, uint32_t index)
{
    return index == initial(ctx) ? RV_ROW_STATUS_PLAYING : RV_ROW_STATUS_NONE;
}

static const char *row(const struct rv_context *ctx, uint32_t index,
                       const char **detail)
{
    (void)ctx;
    *detail = index % 2 ? "Disc 2" : "Disc 1";
    return tracks[index];
}

static int select_track(const struct rv_context *ctx, uint32_t index)
{
    api->setting_set_int(ctx->self, PLAYING, index);
    api->ui_refresh(ctx->self, NULL);
    return 0;
}

static const char *track_name(const struct rv_context *ctx)
{
    return tracks[initial(ctx)];
}

static const struct rv_row track_detail[] = {RV_ROW_INFO("Track", track_name)};

static int open_track(const struct rv_context *ctx, uint32_t index)
{
    select_track(ctx, index);
    api->ui_push(ctx->self, "track", NULL);
    return 0;
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int w, int h)
{
    rv_color fg = g->color(RV_COLOR_TEXT);
    rv_color bg = g->color(RV_COLOR_BACKGROUND);
    int level = api->setting_get_int(ctx->self, LEVEL);
    g->fill(&(struct rv_rect){0, 0, w, h}, bg);
    g->text(8, 4, RV_FONT_LARGE, fg, "Type and color");
    g->row(34, w, "Selection bar", levels[level], 1);
    for (int i = 0; i < RV_COLOR_ROLE_COUNT; i++) {
        int left = i * w / RV_COLOR_ROLE_COUNT;
        int right = (i + 1) * w / RV_COLOR_ROLE_COUNT;
        g->fill(&(struct rv_rect){left, 65, right - left, 17}, g->color(i));
    }
    g->paragraph(
        8, 90, w - 16, RV_FONT_TEXT, fg,
        "Turn the wheel to change the level. Press Select to toggle the switch.", 3);
    g->bar(&(struct rv_rect){8, h - 52, (w - 16) * (level + 1) / RV_COUNT(levels), 8});
    g->text_in(&(struct rv_rect){8, h - 36, w - 16, 30}, RV_FONT_TITLE, fg, status(ctx),
               RV_ALIGN_CENTER);
}

static int input(const struct rv_context *ctx, const struct rv_input *event)
{
    if (event->kind == RV_INPUT_WHEEL) {
        int level = api->setting_get_int(ctx->self, LEVEL) + event->delta;
        if (level < 0)
            level = 0;
        if (level >= (int)RV_COUNT(levels))
            level = RV_COUNT(levels) - 1;
        api->setting_set_int(ctx->self, LEVEL, level);
        api->ui_refresh(ctx->self, NULL);
        return 1;
    }
    if (event->kind == RV_INPUT_RELEASE && event->button == RV_BUTTON_CENTER) {
        toggle(ctx);
        return 1;
    }
    return 0;
}

static const struct rv_setting settings[] = {
    RV_SETTING_CHOICE(MODE, "Choice", 1, labels),
    RV_SETTING_SLIDER(LEVEL, "Slider", 2, levels),
    RV_SETTING_MULTI(FLAGS, "Multiple choice", 5, labels),
    RV_SETTING_BOOL(SWITCH, "Switch", 0),
    RV_SETTING_INT(PLAYING, "Current track", 2, 0, RV_COUNT(tracks) - 1),
};
static const struct rv_row home[] = {
    RV_ROW_PUSH("Pickers", "pickers"),   RV_ROW_PUSH("Lists and rows", "lists"),
    RV_ROW_PUSH("Text page", "text"),    RV_ROW_PUSH("Drawing canvas", "canvas"),
    RV_ROW_PUSH("Stock Games", "games"),
};
static const struct rv_pane panes[] = {
    {.caption = "Choices and sliders", .bitmap = RV_BITMAP("info")},
    {.caption = "Rows and details"},
    {.caption = "Wrapped paragraphs"},
    {.caption_text = status},
    {.caption = "Stock game browser"}};
_Static_assert(RV_COUNT(home) == RV_COUNT(panes), "one pane per row");
static const struct rv_row pickers[] = {
    RV_ROW_PUSH("Split choice", "split"),     RV_ROW_PUSH("Full choice", "full"),
    RV_ROW_SETTING("Multiple choice", FLAGS), RV_ROW_SETTING("Slider", LEVEL),
    RV_ROW_SETTING("Switch", SWITCH),         RV_ROW_ACTION("Toggle switch", toggle),
    RV_ROW_INFO("Switch state", status),
};
static const struct rv_row lists[] = {
    RV_ROW_PUSH("Beside detail", "beside"),
    RV_ROW_PUSH("Below detail", "below"),
    RV_ROW_PUSH("Submenu detail", "submenu"),
    RV_ROW_PUSH("Empty list", "empty"),
};
static const struct rv_context_row context[] = {
    RV_CONTEXT_ROW(RV_ROW_ACTION("Toggle switch", toggle)),
    RV_CONTEXT_ROW(RV_ROW_PUSH("Read text", "text"), .available = is_on),
};
static const struct rv_screen screens[] = {
    RV_SCREEN_LIST_PANES("home", "Menu Test", home, panes, .layout = RV_LAYOUT_SPLIT,
                         RV_SCREEN_CONTEXT(context)),
    RV_SCREEN_LIST("pickers", "Pickers", pickers, .layout = RV_LAYOUT_FULL,
                   RV_SCREEN_CONTEXT(context)),
    RV_SCREEN_LIST("lists", "Lists and Rows", lists, .layout = RV_LAYOUT_SPLIT,
                   .caption = "Dynamic lists", .bitmap = RV_BITMAP("info")),
    RV_SCREEN_CHOICE("split", "Split Choice", MODE, labels, .layout = RV_LAYOUT_SPLIT,
                     .bitmap = RV_BITMAP("info")),
    RV_SCREEN_CHOICE("full", "Full Choice", MODE, labels, .layout = RV_LAYOUT_FULL),
    RV_SCREEN_DYNAMIC_LIST("beside", "Beside Detail", count, row,
                           .dynamic.select = select_track, .dynamic.initial = initial,
                           .dynamic.status = playing, .layout = RV_LAYOUT_SPLIT,
                           .caption = "Select a track", RV_SCREEN_CONTEXT(context)),
    RV_SCREEN_DYNAMIC_LIST("below", "Below Detail", count, row,
                           .dynamic.select = select_track, .dynamic.initial = initial,
                           .dynamic.status = playing, .dynamic.detail = RV_DETAIL_BELOW,
                           .layout = RV_LAYOUT_FULL),
    RV_SCREEN_DYNAMIC_LIST(
        "submenu", "Submenu Detail", count, row, .dynamic.select = open_track,
        .dynamic.initial = initial, .dynamic.status = playing,
        .dynamic.detail = RV_DETAIL_BELOW_SUBMENU, .layout = RV_LAYOUT_FULL),
    RV_SCREEN_DYNAMIC_LIST("empty", "Empty List", empty_count, row,
                           .dynamic.empty = "No tracks", .layout = RV_LAYOUT_FULL),
    RV_SCREEN_LIST("track", "Track", track_detail, .layout = RV_LAYOUT_FULL),
    RV_SCREEN_TEXT("text", "Text Page", paragraphs),
    RV_SCREEN_CANVAS("canvas", "Drawing Canvas", draw, input),
    RV_SCREEN_STOCK("games", "Games", RV_STOCK_SCREEN_GAMES),
};
static const struct rv_menu_entry entries[] = {
    RV_MENUS_ENTRY((RV_MENU_BIT(RV_MENU_VIDEO_PLAYLIST_CONTEXT) * 2u - 1) &
                       ~RV_MENU_BIT(RV_MENU_SETTINGS),
                   0, RV_ROW_PUSH("Menu Test", "home")),
    RV_MENU_ENTRY(
        RV_MENU_SETTINGS, RV_SETTINGS_BRIGHTNESS, RV_ROW_PUSH("Menu Test", "home"),
        .pane = RV_PANE(.caption = "RVX screen gallery", .bitmap = RV_BITMAP("info"))),
};
static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SETTINGS(settings),
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle self)
{
    (void)self;
    api = core;
    return &extension;
}
