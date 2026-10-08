/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_PLAY_NEXT_H
#define CFW_PLAY_NEXT_H
#include "osos.h"

void cfw_play_next_menu(struct osos_context_menu *menu);
/* Caller holds the player lock while resolving the item and inserting it. */
void cfw_play_next_add(struct osos_player *player, struct osos_media_item *item, int last);
#endif
