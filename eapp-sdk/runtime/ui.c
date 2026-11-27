/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/ui.c (8fc840d) by lgreitis. */
#include "eapp_ui.h"

/* The host's list accumulator's figures: a slow turn is under one turn in
 * four seconds. */
#define DOUBLING_MS 350
#define REST_MS 400
#define MAX_MULTIPLIER 32
#define SLOW_TURN_MS_PER_96TH 42

int32_t eapp_wheel_steps_consume(struct eapp_wheel_steps *steps, const struct eapp_frame *frame,
                                 uint32_t threshold)
{
    if (!steps || !frame || !threshold)
        return 0;
    int32_t delta = frame->wheel.delta;
    if (!frame->wheel.touched && !delta) {
        steps->remainder = 0;
        return 0;
    }
    if (!delta)
        return 0;
    uint32_t now = frame->ticks_ms;
    uint32_t since = now - steps->last_motion_ms;
    int reversed = (delta < 0) != (steps->last_delta < 0);
    int slow = since > (uint32_t)(delta < 0 ? -delta : delta) * SLOW_TURN_MS_PER_96TH;
    if (!steps->multiplier || reversed || since > REST_MS || slow) {
        steps->multiplier = 1;
        steps->doubling_ms = now + DOUBLING_MS;
    } else if ((int32_t)(now - steps->doubling_ms) >= 0 && steps->multiplier < MAX_MULTIPLIER) {
        steps->multiplier *= 2;
        steps->doubling_ms = now + DOUBLING_MS;
    }
    steps->last_motion_ms = now;
    steps->last_delta = delta;
    steps->remainder += delta * (int32_t)steps->multiplier;
    int32_t result = steps->remainder / (int32_t)threshold;
    if (slow && result > 1)
        result = 1;
    else if (slow && result < -1)
        result = -1;
    steps->remainder -= result * (int32_t)threshold;
    return result;
}

static void move(struct eapp_menu *menu, int32_t by)
{
    if (!menu->count)
        return;
    int32_t target = (int32_t)menu->selected + by;
    menu->selected = target < 0 ? 0 : target >= (int32_t)menu->count ? menu->count - 1 : (unsigned)target;
}

enum eapp_menu_action eapp_menu_input(struct eapp_menu *menu,
                                      const struct eapp_input_event *event)
{
    if (!menu || !event || event->type != EAPP_INPUT_PRESS)
        return EAPP_MENU_NONE;
    switch (event->button) {
    case EAPP_BUTTON_MENU:
        return EAPP_MENU_BACK;
    case EAPP_BUTTON_CENTER:
        return menu->selected < menu->count ? EAPP_MENU_SELECT : EAPP_MENU_NONE;
    case EAPP_BUTTON_PREVIOUS:
        move(menu, -1);
        break;
    case EAPP_BUTTON_NEXT:
        move(menu, 1);
        break;
    }
    return EAPP_MENU_NONE;
}

void eapp_menu_scroll(struct eapp_menu *menu, const struct eapp_frame *frame)
{
    if (menu && frame)
        move(menu, eapp_wheel_steps_consume(&menu->steps, frame, 4));
}

static uint16_t colour(uint16_t set, uint16_t fallback)
{
    return set ? set : fallback;
}

/* The inset of a screen's content from its region's edges, in font pixels. */
#define SCREEN_INSET 4

/* The region at x, y of width by height (0 reaches the framebuffer's edge),
 * cut to the framebuffer, as a framebuffer of its own so drawing clips at
 * its edges; 0 when nothing of it is left. */
static int clip_region(struct eapp_framebuffer *region, const struct eapp_framebuffer *fb, int x,
                       int y, unsigned width, unsigned height)
{
    int left = x, top = y;
    int right = width ? left + (int)width : (int)fb->width;
    int bottom = height ? top + (int)height : (int)fb->height;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > (int)fb->width)
        right = (int)fb->width;
    if (bottom > (int)fb->height)
        bottom = (int)fb->height;
    if (right <= left || bottom <= top)
        return 0;
    *region = (struct eapp_framebuffer){fb->pixels + (size_t)top * fb->stride_pixels + left,
                                        (uint32_t)(right - left), (uint32_t)(bottom - top),
                                        fb->stride_pixels};
    return 1;
}

void eapp_menu_draw(struct eapp_menu *menu, const struct eapp_framebuffer *fb)
{
    if (!menu || !fb || !fb->pixels)
        return;
    unsigned scale = menu->scale ? menu->scale : 1;
    int row_height = EAPP_MENU_ROW_HEIGHT * (int)scale;
    int text_y = (row_height - (int)eapp_text_height(scale)) / 2;
    int inset = SCREEN_INSET * (int)scale;
    uint16_t background = menu->background;
    uint16_t text = colour(menu->text, 0xffff);
    uint16_t selected_background = colour(menu->selected_background, 0xffff);
    uint16_t selected_text = menu->selected_text;
    uint16_t title_text = colour(menu->title_text, 0x8410);
    struct eapp_framebuffer region;
    if (!clip_region(&region, fb, menu->x, menu->y, menu->width, menu->height))
        return;
    fb = &region;
    int y = 0;
    eapp_clear(fb, background);
    if (menu->title) {
        eapp_draw_text(fb, inset, y + text_y, menu->title, title_text, scale);
        y += row_height;
    }
    int room = (int)fb->height - y;
    unsigned rows = room > 0 ? (unsigned)(room / row_height) : 0;
    if (!rows)
        return;
    unsigned first = menu->first;
    if (menu->selected < first)
        first = menu->selected;
    else if (menu->selected >= first + rows)
        first = menu->selected - rows + 1;
    if (first + rows > menu->count)
        first = menu->count > rows ? menu->count - rows : 0;
    menu->first = first;
    for (unsigned i = first; i < menu->count && i - first < rows; ++i, y += row_height) {
        int selected = i == menu->selected;
        if (selected)
            eapp_fill_rect(fb, 0, y, (int)fb->width, row_height, selected_background);
        uint16_t ink = selected ? selected_text : text;
        eapp_draw_text(fb, inset, y + text_y, menu->items[i].label, ink, scale);
        if (menu->items[i].value && (int)fb->width > inset)
            eapp_draw_text_in(fb, 0, y + text_y, (unsigned)((int)fb->width - inset),
                              menu->items[i].value, ink, scale, EAPP_ALIGN_RIGHT);
    }
}

static unsigned text_length(const char *text)
{
    unsigned length = 0;
    while (text[length])
        ++length;
    return length;
}

static unsigned lowercase(unsigned c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int compare(const char *a, const char *b)
{
    while (*a && lowercase((uint8_t)*a) == lowercase((uint8_t)*b)) {
        ++a;
        ++b;
    }
    return (int)lowercase((uint8_t)*a) - (int)lowercase((uint8_t)*b);
}

static const char *filename(const char *path)
{
    const char *name = path;
    for (; *path; ++path) {
        if (path == name && *path == '.')
            return NULL;
        if (*path == '/')
            name = path + 1;
    }
    return name;
}

int eapp_file_picker_open(struct eapp_file_picker *picker, const char *title,
                          const char *directory, const char *extension)
{
    picker->menu = (struct eapp_menu){.title = title, .items = picker->items};
    if (!directory || !extension || extension[0] != '.' || !extension[1])
        return -1;
    struct eapp_directory *dir = eapp_directory_open(directory, 1);
    if (!dir)
        return -1;
    unsigned prefix = text_length(directory), suffix = text_length(extension);
    struct eapp_directory_entry entry;
    int result;
    while ((result = eapp_directory_read(dir, &entry)) > 0) {
        const char *name = filename(entry.name);
        if (entry.is_directory || !name)
            continue;
        unsigned length = text_length(name);
        if (length <= suffix || compare(name + length - suffix, extension))
            continue;
        if (picker->menu.count == 64) {
            result = -2;
            break;
        }
        unsigned path_length = prefix + (prefix != 0) + text_length(entry.name);
        unsigned label_length = length - suffix;
        if (label_length > 42)
            label_length = 42;
        char *path = eapp_alloc(path_length + 1 + label_length + 1);
        if (!path) {
            result = -1;
            break;
        }
        unsigned i = 0;
        for (; i < prefix; ++i)
            path[i] = directory[i];
        if (prefix)
            path[i++] = '/';
        for (unsigned j = 0; entry.name[j]; ++j)
            path[i++] = entry.name[j];
        path[i] = 0;
        char *label = path + path_length + 1;
        for (i = 0; i < label_length; ++i)
            label[i] = name[i];
        label[i] = 0;
        i = picker->menu.count++;
        while (i && compare(picker->items[i - 1].label, label) > 0) {
            picker->items[i] = picker->items[i - 1];
            picker->paths[i] = picker->paths[i - 1];
            --i;
        }
        picker->items[i] = (struct eapp_menu_item){label, NULL};
        picker->paths[i] = path;
    }
    eapp_directory_close(dir);
    if (result < 0)
        eapp_file_picker_close(picker);
    return result;
}

void eapp_file_picker_close(struct eapp_file_picker *picker)
{
    for (unsigned i = 0; i < picker->menu.count; ++i)
        eapp_free((void *)picker->paths[i]);
    picker->menu.count = picker->menu.selected = 0;
}

/* Text pixels either side of a message, and the box's height, in font pixels. */
#define MESSAGE_PAD 8
#define MESSAGE_HEIGHT 20

void eapp_message_draw(const struct eapp_framebuffer *fb, const char *text, unsigned scale,
                       uint16_t background, uint16_t frame, uint16_t color)
{
    if (!fb || !fb->pixels || !text)
        return;
    if (!scale)
        scale = 1;
    int width = (int)eapp_text_width(text, scale) + 2 * MESSAGE_PAD * (int)scale;
    int height = MESSAGE_HEIGHT * (int)scale;
    int x = ((int)fb->width - width) / 2, y = ((int)fb->height - height) / 2;
    eapp_fill_rect(fb, x, y, width, height, background);
    for (int i = 0; i < (int)scale; ++i)
        eapp_draw_rect(fb, x + i, y + i, width - 2 * i, height - 2 * i, colour(frame, 0xffff));
    eapp_draw_text_in(fb, x, y + (height - (int)eapp_text_height(scale)) / 2, (unsigned)width,
                      text, colour(color, 0xffff), scale, EAPP_ALIGN_CENTER);
}

/* A screen's title, one size larger than its body, at the top of the
 * region; the y below it, or the inset when there is none. */
static int draw_title(const struct eapp_framebuffer *fb, const char *title, unsigned scale,
                      uint16_t color)
{
    int inset = SCREEN_INSET * (int)scale;
    if (!title)
        return inset;
    eapp_draw_text(fb, inset, inset, title, colour(color, 0xffff), scale + 1);
    return inset + (int)eapp_text_height(scale + 1) + inset;
}

static void draw_cell(const struct eapp_framebuffer *fb, const struct eapp_table_column *column,
                      const char *text, int y, uint16_t color, unsigned scale)
{
    if (text && column->width)
        eapp_draw_text_in(fb, column->x, y, column->width, text, color, scale, column->align);
}

void eapp_table_draw(const struct eapp_table *table, const struct eapp_framebuffer *fb)
{
    if (!table || !fb || !fb->pixels || !table->columns || (table->rows && !table->cells))
        return;
    struct eapp_framebuffer region;
    if (!clip_region(&region, fb, table->x, table->y, table->width, table->height))
        return;
    fb = &region;
    unsigned scale = table->scale ? table->scale : 1;
    int inset = SCREEN_INSET * (int)scale;
    int row_height = EAPP_TABLE_ROW_HEIGHT * (int)scale;
    int text_y = (row_height - (int)eapp_text_height(scale)) / 2;
    uint16_t text = colour(table->text, 0xffff);
    uint16_t heading = colour(table->heading_text, 0x8410);
    eapp_clear(fb, table->background);
    int y = draw_title(fb, table->title, scale, table->title_text);
    for (unsigned c = 0; c < table->column_count; ++c)
        draw_cell(fb, &table->columns[c], table->columns[c].heading, y + text_y, heading, scale);
    y += row_height;
    for (unsigned r = 0; r < table->rows && y + row_height <= (int)fb->height;
         ++r, y += row_height) {
        int on_bar = (int)r == table->highlighted;
        if (on_bar)
            eapp_fill_rect(fb, inset, y, (int)fb->width - 2 * inset, row_height,
                           colour(table->highlight, 0xffff));
        const char *const *cells = table->cells + (size_t)r * table->column_count;
        for (unsigned c = 0; c < table->column_count; ++c)
            draw_cell(fb, &table->columns[c], cells[c], y + text_y,
                      on_bar ? table->highlight_text : text, scale);
    }
}

/* The page's lines wrapped to `width`, drawn from `y` when `fb` is set. */
static int page_lines(const struct eapp_page *page, const struct eapp_framebuffer *fb, int y,
                      unsigned width, unsigned scale, int line_height)
{
    int inset = SCREEN_INSET * (int)scale;
    int lines = 0;
    for (unsigned i = 0; i < page->count; ++i) {
        const struct eapp_page_block *block = &page->blocks[i];
        if (!block->text)
            continue;
        uint16_t color = block->heading ? colour(page->heading_text, 0x8410)
                                        : colour(page->text, 0xffff);
        lines += (int)eapp_draw_text_wrapped(fb, inset, y + lines * line_height, width,
                                             (unsigned)line_height, block->text, color, scale);
        ++lines;
    }
    return lines ? lines - 1 : 0;
}

/* Keeps the offset between 0 and the lines beyond the region, once a draw
 * has counted them. */
static void page_move(struct eapp_page *page, int32_t by)
{
    int beyond = 0;
    if (page->shown && page->lines > page->shown)
        beyond = (int)(page->lines - page->shown);
    page->offset += by;
    if ((page->lines || page->shown) && page->offset > beyond)
        page->offset = beyond;
    if (page->offset < 0)
        page->offset = 0;
}

enum eapp_menu_action eapp_page_input(struct eapp_page *page,
                                      const struct eapp_input_event *event)
{
    if (!page || !event || event->type != EAPP_INPUT_PRESS)
        return EAPP_MENU_NONE;
    switch (event->button) {
    case EAPP_BUTTON_MENU:
        return EAPP_MENU_BACK;
    case EAPP_BUTTON_PREVIOUS:
        page_move(page, -1);
        break;
    case EAPP_BUTTON_NEXT:
        page_move(page, 1);
        break;
    }
    return EAPP_MENU_NONE;
}

void eapp_page_scroll(struct eapp_page *page, const struct eapp_frame *frame)
{
    if (page && frame)
        page_move(page, eapp_wheel_steps_consume(&page->steps, frame, 4));
}

void eapp_page_draw(struct eapp_page *page, const struct eapp_framebuffer *fb)
{
    if (!page || !fb || !fb->pixels)
        return;
    struct eapp_framebuffer region;
    if (!clip_region(&region, fb, page->x, page->y, page->width, page->height))
        return;
    fb = &region;
    unsigned scale = page->scale ? page->scale : 1;
    int inset = SCREEN_INSET * (int)scale;
    int line_height = page->line_height ? (int)page->line_height
                                        : EAPP_PAGE_LINE_HEIGHT * (int)scale;
    eapp_clear(fb, page->background);
    /* The lines scroll through the whole lines below the top inset, so a
     * line scrolled off leaves no trace and none is cut at the bottom. */
    unsigned width = fb->width > 2 * (unsigned)inset ? fb->width - 2 * (unsigned)inset : 1;
    page->lines = (unsigned)page_lines(page, NULL, 0, width, scale, line_height);
    unsigned below = fb->height > (unsigned)inset ? fb->height - (unsigned)inset : 0;
    page->shown = below / (unsigned)line_height;
    page_move(page, 0);
    struct eapp_framebuffer content;
    if (page->shown &&
        clip_region(&content, fb, 0, inset, 0, page->shown * (unsigned)line_height))
        page_lines(page, &content, -page->offset * line_height, width, scale, line_height);
}

/* An option's room either side of its text, and the gap between options and
 * above them, in font pixels. */
#define OPTION_PAD 4
#define OPTION_GAP 4

static void dialog_move(struct eapp_dialog *dialog, int32_t by)
{
    if (!dialog->count)
        return;
    int32_t target = (int32_t)dialog->selected + by;
    dialog->selected =
        target < 0 ? 0 : target >= (int32_t)dialog->count ? dialog->count - 1 : (unsigned)target;
}

enum eapp_dialog_action eapp_dialog_input(struct eapp_dialog *dialog,
                                          const struct eapp_input_event *event)
{
    if (!dialog || !event || event->type != EAPP_INPUT_PRESS)
        return EAPP_DIALOG_NONE;
    switch (event->button) {
    case EAPP_BUTTON_MENU:
        return EAPP_DIALOG_CANCEL;
    case EAPP_BUTTON_CENTER:
        return dialog->selected < dialog->count ? EAPP_DIALOG_CHOOSE : EAPP_DIALOG_NONE;
    case EAPP_BUTTON_PREVIOUS:
        dialog_move(dialog, -1);
        break;
    case EAPP_BUTTON_NEXT:
        dialog_move(dialog, 1);
        break;
    }
    return EAPP_DIALOG_NONE;
}

void eapp_dialog_scroll(struct eapp_dialog *dialog, const struct eapp_frame *frame)
{
    if (dialog && frame)
        dialog_move(dialog, eapp_wheel_steps_consume(&dialog->steps, frame, 16));
}

void eapp_dialog_draw(const struct eapp_dialog *dialog, const struct eapp_framebuffer *fb)
{
    if (!dialog || !fb || !fb->pixels)
        return;
    struct eapp_framebuffer region;
    if (!clip_region(&region, fb, dialog->x, dialog->y, dialog->width, dialog->height))
        return;
    fb = &region;
    unsigned scale = dialog->scale ? dialog->scale : 1;
    int inset = SCREEN_INSET * (int)scale;
    int gap = OPTION_GAP * (int)scale;
    int height = EAPP_MENU_ROW_HEIGHT * (int)scale;
    uint16_t text = colour(dialog->text, 0xffff);
    unsigned count = dialog->options ? dialog->count : 0;
    eapp_clear(fb, dialog->background);
    /* The title and the message keep above the options' row. */
    int options_y = (int)fb->height - inset - height;
    struct eapp_framebuffer body;
    if (clip_region(&body, fb, 0, 0, 0, count ? (unsigned)(options_y - gap) : 0)) {
        int y = draw_title(&body, dialog->title, scale, dialog->title_text);
        unsigned width = body.width > 2 * (unsigned)inset ? body.width - 2 * (unsigned)inset : 1;
        if (dialog->message)
            eapp_draw_text_wrapped(&body, inset, y, width, EAPP_PAGE_LINE_HEIGHT * scale,
                                   dialog->message, text, scale);
    }
    if (!count)
        return;
    unsigned widest = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned w = eapp_text_width(dialog->options[i], scale);
        widest = w > widest ? w : widest;
    }
    int option = (int)widest + 2 * OPTION_PAD * (int)scale;
    int room = (int)fb->width - 2 * inset - ((int)count - 1) * gap;
    if (option * (int)count > room)
        option = room / (int)count;
    if (option <= 0)
        return;
    int total = option * (int)count + ((int)count - 1) * gap;
    int x = ((int)fb->width - total) / 2;
    int text_y = (height - (int)eapp_text_height(scale)) / 2;
    for (unsigned i = 0; i < count; ++i, x += option + gap) {
        int selected = i == dialog->selected;
        struct eapp_framebuffer box;
        if (!clip_region(&box, fb, x, options_y, (unsigned)option, (unsigned)height))
            continue;
        if (selected)
            eapp_clear(&box, colour(dialog->selected_background, 0xffff));
        eapp_draw_text_in(&box, 0, text_y, (unsigned)option, dialog->options[i],
                          selected ? dialog->selected_text : text, scale, EAPP_ALIGN_CENTER);
    }
}
