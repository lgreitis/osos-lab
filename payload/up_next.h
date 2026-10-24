/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_UP_NEXT_H
#define CFW_UP_NEXT_H
#include "osos.h"

/* Main menu "Up Next" row actions. */
int cfw_up_next_action(void *controller, const char *action, uint32_t argument);
/* Called before a media model is destroyed: forgets the Up Next browser if it is its. */
void cfw_up_next_model_destroyed(void *model);
/* Song-list actions on the Up Next screen (hold menu and selection). */
int cfw_up_next_song_action(void *controller, const char *action, uint32_t argument);
/* Settings-model property behind the preview's "n Songs" line. */
int cfw_up_next_property(void *model, uint32_t type, uint32_t property, uintptr_t *out);
/* Re-reads the queue length: updates the row, its preview and an open Up Next list. */
void cfw_up_next_refresh(void);
#endif
