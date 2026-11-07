/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_FILE_INFO_H
#define CFW_FILE_INFO_H

#include "osos.h"

/* Return nonzero after pushing the screen, or zero if it could not be opened. */
int cfw_file_info_show(void *controller, struct osos_media_item *item);

int cfw_file_info_action(const char *action, uint32_t argument);
int cfw_file_info_property(uint32_t type, uint32_t property, uintptr_t *out);
int cfw_file_info_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                                   uintptr_t *out);

#endif
