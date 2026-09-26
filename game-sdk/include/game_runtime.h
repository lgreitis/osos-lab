/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef OSOS_GAME_RUNTIME_H
#define OSOS_GAME_RUNTIME_H

#include "osos_game_abi.h"

/* Entry bodies for eapp slots 0, 1, 4; game_start clears BSS before init. */
void game_runtime_init(void);
void game_runtime_shutdown(void);
void game_runtime_frame(const struct osos_game_frame_input *input,
                        struct osos_game_frame_output *output);

#endif
