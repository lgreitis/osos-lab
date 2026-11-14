/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef OSOS_GAME_API_H
#define OSOS_GAME_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum game_button {
    GAME_BUTTON_MENU = 1,
    GAME_BUTTON_CENTER = 2,
    GAME_BUTTON_NEXT = 3,
    GAME_BUTTON_PREVIOUS = 4,
    GAME_BUTTON_PLAY = 5,
};

enum game_input_type {
    GAME_INPUT_RELEASE = 1,
    GAME_INPUT_PRESS = 2,
};

struct game_framebuffer {
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
};

struct game_input_event {
    uint8_t button;
    uint8_t type;
    uint32_t timestamp_ms;
};

/* RGB565 pixels are borrowed for one frame call and presented on return. */
struct game_frame {
    struct game_framebuffer framebuffer;
};

struct game_app {
    /* Directory name beneath iPod_Control/games_RO/, without slashes. */
    const char *package_id;
    void (*init)(void);
    void (*input)(const struct game_input_event *event);
    void (*frame)(const struct game_frame *frame);
    void (*shutdown)(void);
};

extern const struct game_app game_application;

/* Draws clipped 5x7 text using the SDK ASCII font. */
void game_draw_text(const struct game_framebuffer *framebuffer, unsigned x, unsigned y,
                    const char *text, uint16_t color);

/* Requests native shutdown after the current callback returns. */
void game_request_exit(void);

/* Allocations belong to the native game heap and expire at shutdown. */
void *game_alloc(size_t size);
void *game_realloc(void *pointer, size_t size);
void game_free(void *pointer);
uint32_t game_ticks_ms(void);
void game_sleep_ms(uint32_t milliseconds);

struct game_wheel {
    int32_t delta;    /* Positive clockwise; consumed on each read. */
    uint8_t position; /* Native angle: right=0, up=64, left=128, down=192. */
    uint8_t touched;
};

struct game_wheel game_read_wheel(void);

enum game_direction {
    GAME_DIRECTION_UP = 1,
    GAME_DIRECTION_RIGHT = 2,
    GAME_DIRECTION_DOWN = 4,
    GAME_DIRECTION_LEFT = 8,
};

#define GAME_WHEEL_TOP_POSITION 64

/* Eight touch sectors; diagonals combine two direction bits. Zero when untouched.
 * top_position sets the native wheel coordinate used as Up; use GAME_WHEEL_TOP_POSITION. */
uint8_t game_wheel_directions(const struct game_wheel *wheel, uint8_t top_position);

/* Returns 0/1 for unlocked/locked, or -1 if the native read failed. */
int game_hold_locked(void);

/* Package-relative directory traversal, including subdirectories when recursive is
 * nonzero. Use "" for the package root. Four iterators may be open at once.
 * Read returns 1 for an entry, 0 at end/unavailable, -1 for invalid input or a
 * path exceeding the buffer. Names are relative to the directory opened.
 * Close even empty/unavailable directories; shutdown closes remaining iterators. */
struct game_directory;

struct game_directory_entry {
    char name[256];
    uint8_t is_directory;
};
struct game_directory *game_directory_open(const char *path, int recursive);
int game_directory_read(struct game_directory *directory,
                        struct game_directory_entry *entry);
void game_directory_close(struct game_directory *directory);

/* Read-only package-relative paths. Traversal is rejected; up to 12 open files.
 * Open returns NULL on failure, read returns bytes/-1, seek returns 0/-1.
 * Shutdown closes remaining handles. Size/position require an open handle. */
struct game_file;
struct game_file *game_file_open(const char *path);
int game_file_read(struct game_file *file, void *buffer, size_t size);
int game_file_seek(struct game_file *file, uint32_t offset);
uint32_t game_file_size(const struct game_file *file);
uint32_t game_file_position(const struct game_file *file);
void game_file_close(struct game_file *file);

#ifdef __cplusplus
}
#endif

#endif
