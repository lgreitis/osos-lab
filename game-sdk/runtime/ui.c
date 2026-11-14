/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_ui.h"

void game_clear(const struct game_framebuffer *fb, uint16_t color)
{
    if (!fb || !fb->pixels)
        return;
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = color;
}

static void move(struct game_menu *menu, int direction)
{
    if (direction < 0 && menu->selected)
        --menu->selected;
    else if (direction > 0 && menu->selected + 1 < menu->count)
        ++menu->selected;
}

enum game_menu_action game_menu_input(struct game_menu *menu,
                                      const struct game_input_event *event)
{
    if (event->type != GAME_INPUT_PRESS)
        return GAME_MENU_NONE;
    switch (event->button) {
    case GAME_BUTTON_MENU:
        return GAME_MENU_BACK;
    case GAME_BUTTON_CENTER:
        return menu->selected < menu->count ? GAME_MENU_SELECT : GAME_MENU_NONE;
    case GAME_BUTTON_PREVIOUS:
        move(menu, -1);
        break;
    case GAME_BUTTON_NEXT:
    case GAME_BUTTON_PLAY:
        move(menu, 1);
        break;
    }
    return GAME_MENU_NONE;
}

void game_menu_scroll(struct game_menu *menu, const struct game_wheel *wheel)
{
    move(menu, wheel->delta);
}

void game_menu_draw(const struct game_menu *menu, const struct game_framebuffer *fb)
{
    game_clear(fb, 0);
    if (!fb || !fb->pixels || fb->height < 70)
        return;
    game_draw_text(fb, 12, 12, menu->title, 0xffff);
    unsigned rows = (fb->height - 70) / 16;
    if (!rows)
        return;
    unsigned first = menu->selected / rows * rows;
    for (unsigned i = first; i < menu->count && i - first < rows; ++i) {
        unsigned y = 42 + (i - first) * 16;
        if (i == menu->selected)
            game_draw_text(fb, 12, y, "+", 0xffff);
        game_draw_text(fb, 24, y, menu->items[i].label,
                       i == menu->selected ? 0xffff : 0x8410);
    }
    if (!menu->count)
        game_draw_text(fb, 12, 55, "NO FILES", 0xffff);
    game_draw_text(fb, 12, fb->height - 20, "CENTER: SELECT   MENU: BACK", 0x8410);
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

int game_file_picker_open(struct game_file_picker *picker, const char *title,
                          const char *directory, const char *extension)
{
    picker->menu = (struct game_menu){title, picker->items, 0, 0};
    if (!directory || !extension || extension[0] != '.' || !extension[1])
        return -1;
    struct game_directory *dir = game_directory_open(directory, 1);
    if (!dir)
        return -1;
    unsigned prefix = text_length(directory), suffix = text_length(extension);
    struct game_directory_entry entry;
    int result;
    while ((result = game_directory_read(dir, &entry)) > 0) {
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
        char *path = game_alloc(path_length + 1 + label_length + 1);
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
            --i;
        }
        picker->items[i] = (struct game_menu_item){label, path};
    }
    game_directory_close(dir);
    if (result < 0)
        game_file_picker_close(picker);
    return result;
}

void game_file_picker_close(struct game_file_picker *picker)
{
    for (unsigned i = 0; i < picker->menu.count; ++i)
        game_free((void *)picker->items[i].value);
    picker->menu.count = picker->menu.selected = 0;
}
