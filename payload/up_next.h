/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_UP_NEXT_H
#define CFW_UP_NEXT_H
#include "osos.h"

/* Main menu "Up Next" row actions. */
int cfw_up_next_action(void *controller, const char *action, uint32_t argument);
int cfw_up_next_owns_model(void *model);
/* Called before the owning model is destroyed. Borrowed wrappers do not own the browser. */
void cfw_up_next_model_destroyed(void *model);
/* Song-list actions on the Up Next screen (hold menu and selection). */
int cfw_up_next_song_action(void *controller, const char *action, uint32_t argument);
/* Settings-model property behind the preview's "n Songs" line. */
int cfw_up_next_property(void *model, uint32_t type, uint32_t property, uintptr_t *out);
/* Updates the menu preview and invalidates the list after queue changes. */
void cfw_up_next_refresh(void);
#endif
