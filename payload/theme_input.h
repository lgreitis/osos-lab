/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* UI-task only. Keep theme actions latched until its pending messages drain,
 * including presses queued while synchronous loading blocks dispatch. */
int cfw_theme_input_begin(void);
void cfw_theme_input_finish(void);
void cfw_theme_input_drained(void);
