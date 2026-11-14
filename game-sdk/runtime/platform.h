/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef OSOS_GAME_PLATFORM_H
#define OSOS_GAME_PLATFORM_H

#include "game_api.h"
#include "osos_game_abi.h"

uintptr_t game_platform_begin_frame(const struct osos_game_frame_input *input,
                                    struct game_framebuffer *framebuffer);
void game_platform_present(uintptr_t surface);
void game_files_close_all(void);
void game_directories_close_all(void);
struct osos_path;
int game_make_path(struct osos_path *path, const char *name, int allow_root);

#endif
