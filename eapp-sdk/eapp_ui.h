/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/include/game_ui.h (8fc840d) by lgreitis. */

#ifndef EAPP_UI_H
#define EAPP_UI_H

#include "eapp_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turns wheel motion into steps the way the iPod's own lists do: a step per
 * `threshold` 96ths of a turn (4 for a list row, 16 for a slider), the
 * remainder carried until the finger lifts, one step at most per frame on a
 * slow turn, and on a fast turn in one direction the motion doubled every
 * 350 ms, to at most 32 times the motion, until the direction changes or the
 * wheel rests for 400 ms. Zero the struct before the first call. */
struct eapp_wheel_steps {
    int32_t remainder;
    int32_t last_delta;
    uint32_t last_motion_ms;
    uint32_t doubling_ms;
    uint32_t multiplier;
};

/* The steps for this frame's wheel: positive clockwise. */
int32_t eapp_wheel_steps_consume(struct eapp_wheel_steps *steps, const struct eapp_frame *frame,
                                 uint32_t threshold);

struct eapp_menu_item {
    const char *label;
    const char *value; /* Drawn at the right of the row, or NULL. */
};

/* A list of rows with one selected, drawn in the SDK font. The rows scroll
 * so the selection stays in view; a title takes the first row when set. The
 * menu fills the framebuffer, or the region at x, y of width by height when
 * those are set (a width or height of 0 reaches the framebuffer's edge). The
 * colours and the scale are the defaults when 0: a dark list with a light
 * selection bar, scale 1, rows EAPP_MENU_ROW_HEIGHT * scale high; 0x0001 is
 * black where the default is not. Zero the struct, then set what differs. */
struct eapp_menu {
    const char *title;
    const struct eapp_menu_item *items;
    unsigned count;
    unsigned selected;
    unsigned first; /* The row at the top; the menu moves it. */
    struct eapp_wheel_steps steps;
    int x, y;
    unsigned width, height;
    unsigned scale;
    uint16_t background, text, selected_background, selected_text, title_text;
};

#define EAPP_MENU_ROW_HEIGHT 12

enum eapp_menu_action {
    EAPP_MENU_NONE,
    EAPP_MENU_SELECT,
    EAPP_MENU_BACK,
};

/* Center selects, Menu goes back, Previous and Next move the selection. */
enum eapp_menu_action eapp_menu_input(struct eapp_menu *menu,
                                      const struct eapp_input_event *event);
/* Moves the selection by the frame's wheel. */
void eapp_menu_scroll(struct eapp_menu *menu, const struct eapp_frame *frame);
/* Draws the menu over its region, which it clears to the background. */
void eapp_menu_draw(struct eapp_menu *menu, const struct eapp_framebuffer *fb);

/* The files with an extension under a folder of the package ("" for its root)
 * and its subfolders, sorted by name; the extension's case does not matter and
 * hidden paths are skipped. Each row's label is the file's name without the
 * extension and paths[row] its path inside the package, so
 * paths[menu.selected] is the chosen file. Open returns 0, -1 on failure, -2
 * for more than 64 files. The picker owns its labels and paths until it is
 * closed. Zero it first; close it before opening it again. */
struct eapp_file_picker {
    struct eapp_menu menu;
    struct eapp_menu_item items[64];
    const char *paths[64];
};

int eapp_file_picker_open(struct eapp_file_picker *picker, const char *title,
                          const char *directory, const char *extension);
void eapp_file_picker_close(struct eapp_file_picker *picker);

/* Text centred in a framed box over whatever the framebuffer holds: a frame
 * `scale` pixels wide, 8 font pixels of room either side of the text and a
 * box 20 font pixels high, centred in the framebuffer. Colours as the menu's:
 * 0 is the default (a black box, a white frame and white text), 0x0001 black. */
void eapp_message_draw(const struct eapp_framebuffer *fb, const char *text, unsigned scale,
                       uint16_t background, uint16_t frame, uint16_t color);

/* A column of a table: its heading, and where its cells go, `align`ed
 * across the `width` pixels from `x` of the table's region. */
struct eapp_table_column {
    const char *heading;
    int x;
    unsigned width;
    enum eapp_align align;
};

/* Rows of text cells under column headings, one row highlighted on a bar.
 * `cells` holds rows * column_count strings row by row; NULL leaves a cell
 * empty. A title, when set, is drawn one size larger above the headings.
 * Rows are EAPP_TABLE_ROW_HEIGHT * scale high and only whole rows are
 * drawn. The columns place from the region's left edge; the bar and the
 * title keep 4 font pixels in from it. The region, scale and colours are
 * set as the menu's: a dark table with white text and title, dim headings
 * and a light bar with dark text by default. Zero the struct, then set what
 * differs. */
struct eapp_table {
    const char *title;
    const struct eapp_table_column *columns;
    unsigned column_count;
    const char *const *cells;
    unsigned rows;
    int highlighted; /* The row on the bar, or -1. */
    int x, y;
    unsigned width, height;
    unsigned scale;
    uint16_t background, text, title_text, heading_text, highlight, highlight_text;
};

#define EAPP_TABLE_ROW_HEIGHT 13

void eapp_table_draw(const struct eapp_table *table, const struct eapp_framebuffer *fb);

/* A block of a page: a paragraph, wrapped to the page's width, in the text
 * colour, or in the heading colour when `heading` is set. */
struct eapp_page_block {
    const char *text;
    int heading;
};

/* Blocks of wrapped text, a blank line between them, scrolled by whole
 * lines when they overflow the region. `offset` is the first line shown;
 * the page clamps it once a draw has counted the lines. Lines are
 * `line_height` pixels apart, EAPP_PAGE_LINE_HEIGHT font pixels when 0.
 * The region, scale and colours are set as the menu's: a dark page with
 * white text and dim headings by default. Zero the struct, then set what
 * differs. */
struct eapp_page {
    const struct eapp_page_block *blocks;
    unsigned count;
    int offset;
    unsigned lines, shown; /* The lines in all and the lines the region holds;
                              the page sets them as it draws. */
    struct eapp_wheel_steps steps;
    int x, y;
    unsigned width, height;
    unsigned scale;
    unsigned line_height;
    uint16_t background, text, heading_text;
};

#define EAPP_PAGE_LINE_HEIGHT 8

/* Previous and Next scroll a line; Menu answers EAPP_MENU_BACK. */
enum eapp_menu_action eapp_page_input(struct eapp_page *page,
                                      const struct eapp_input_event *event);
/* Scrolls by the frame's wheel. */
void eapp_page_scroll(struct eapp_page *page, const struct eapp_frame *frame);
/* Draws the page over its region, which it clears to the background. */
void eapp_page_draw(struct eapp_page *page, const struct eapp_framebuffer *fb);

/* A question with options in a row along the bottom, one selected on a bar.
 * The title, when set, is drawn one size larger; the message wraps below
 * it and stops above the options. The region, scale and colours are set as
 * the menu's: a dark dialog with white text and a light bar with dark text
 * by default. Zero the struct, then set what differs. */
struct eapp_dialog {
    const char *title;
    const char *message;
    const char *const *options;
    unsigned count;
    unsigned selected;
    struct eapp_wheel_steps steps;
    int x, y;
    unsigned width, height;
    unsigned scale;
    uint16_t background, text, title_text, selected_background, selected_text;
};

enum eapp_dialog_action {
    EAPP_DIALOG_NONE,
    EAPP_DIALOG_CHOOSE, /* Center: `selected` is the answer. */
    EAPP_DIALOG_CANCEL, /* Menu. */
};

/* Previous and Next move the selection. */
enum eapp_dialog_action eapp_dialog_input(struct eapp_dialog *dialog,
                                          const struct eapp_input_event *event);
/* Moves the selection by the frame's wheel, a sixth of a turn per option. */
void eapp_dialog_scroll(struct eapp_dialog *dialog, const struct eapp_frame *frame);
/* Draws the dialog over its region, which it clears to the background. */
void eapp_dialog_draw(const struct eapp_dialog *dialog, const struct eapp_framebuffer *fb);

#ifdef __cplusplus
}
#endif
#endif
