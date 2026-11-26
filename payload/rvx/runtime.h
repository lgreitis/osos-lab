/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

void cfw_rvx_boot(void);
void cfw_rvx_flush(void);
void cfw_rvx_register_ui(void *bank);
void cfw_rvx_menu_row(void *list, uint32_t table, uint32_t item, const void *row,
                      uint32_t bytes, uint32_t metadata);
void cfw_rvx_menu_events(void *map, uint32_t screen);

/* Appends the extensions' rows to a movie or TV episode menu; returns how many. */
unsigned cfw_rvx_video_menu(void *menu, void *controller, int row, const void *item,
                            int tv);

int cfw_rvx_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                             uintptr_t *out);
int cfw_rvx_property(uint32_t type, uint32_t property, uintptr_t *out);
/* The stock view a view of a template the runtime copied was copied from;
 * any other view id unchanged. Code that treats a view specially by its id
 * sees the copy as the original. */
uint32_t cfw_rvx_stock_view(uint32_t view);
void cfw_rvx_reset(void);

unsigned cfw_rvx_visible_rows(unsigned menu);
int cfw_rvx_visible_property(unsigned menu, unsigned row, uint32_t type, int selected,
                             uintptr_t *out);
void cfw_rvx_toggle_row(unsigned menu, unsigned row);

extern uint8_t cfw_rvx_has_canvas;
