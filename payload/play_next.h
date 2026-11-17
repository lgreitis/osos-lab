/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_PLAY_NEXT_H
#define CFW_PLAY_NEXT_H
#include "osos.h"

void cfw_play_next_menu(struct osos_context_menu *menu);
/* Queue edits hold the player lock and stop artwork until every mutation is complete. */
void cfw_queue_edit_begin(struct osos_player *player);
void cfw_queue_edit_end(struct osos_player *player, int changed);
/* Insert inside an edit already bracketed by cfw_queue_edit_begin/end. */
int cfw_play_next_insert(struct osos_player *player, struct osos_media_item *item,
                         int last);
/* Caller holds the player lock. Returns nonzero only after insertion succeeds. */
int cfw_play_next_add(struct osos_player *player, struct osos_media_item *item,
                      int last);
/* Caller holds the player lock. Queues every song selected by the browser's filters. */
void cfw_play_next_add_browser(struct osos_player *player, void *browser, int last);
#endif
