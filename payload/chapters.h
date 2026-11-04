/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_CHAPTERS_H
#define CFW_CHAPTERS_H
#include <stdint.h>
struct osos_context_menu;
struct osos_media_item;
int cfw_chapters_same_item(struct osos_media_item *a, struct osos_media_item *b);
uint32_t cfw_chapters_count(struct osos_media_item *item);
void cfw_chapters_show_episode(void *controller, struct osos_media_item *item, int row);
void cfw_episode_play_chapter(void *controller, int row, struct osos_media_item *item,
                              uint32_t index);
void cfw_chapters_menu(struct osos_context_menu *menu);
int cfw_chapters_action(void *controller, const char *action, uint32_t argument);
int cfw_chapters_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_chapters_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                                  uintptr_t *out);
#endif
