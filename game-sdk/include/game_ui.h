/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OSOS_GAME_UI_H
#define OSOS_GAME_UI_H

#include "game_api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_menu_item {
    const char *label;
    const char *value;
};

struct game_menu {
    const char *title;
    const struct game_menu_item *items;
    unsigned count;
    unsigned selected;
};

enum game_menu_action {
    GAME_MENU_NONE,
    GAME_MENU_SELECT,
    GAME_MENU_BACK,
};

enum game_menu_action game_menu_input(struct game_menu *menu,
                                      const struct game_input_event *event);
void game_menu_scroll(struct game_menu *menu, const struct game_wheel *wheel);
void game_menu_draw(const struct game_menu *menu, const struct game_framebuffer *fb);
void game_clear(const struct game_framebuffer *fb, uint16_t color);

/* Recursively lists matching files beneath a package-relative directory ("" for
 * root), sorted by filename. Extension matching ignores case; hidden paths are
 * skipped. Returns 0, -1 on failure, or -2 for more than 64 files. The picker owns
 * its labels and paths until close. Zero-initialize; close before reopening. */
struct game_file_picker {
    struct game_menu menu;
    struct game_menu_item items[64];
};

int game_file_picker_open(struct game_file_picker *picker, const char *title,
                          const char *directory, const char *extension);
void game_file_picker_close(struct game_file_picker *picker);

#ifdef __cplusplus
}
#endif
#endif
