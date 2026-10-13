/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_SLEEP_TIMER_H
#define CFW_SLEEP_TIMER_H

#include <stdint.h>

void cfw_sleep_timer_show(void *controller);
void cfw_sleep_timer_sync(void);
/* Replacement resource while the sleep selector is open, or 0. */
const void *cfw_sleep_timer_resource(uint32_t type, uint32_t id);

#endif
