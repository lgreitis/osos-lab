/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Theme ids are [a-z0-9][a-z0-9_-]{0,30}; "default", "dark" and "classic" are
 * reserved. The empty id denotes Classic and is accepted here; callers that need a
 * custom package check id[0] themselves. */
#define CFW_THEME_ID_BYTES 32u
/* NUL-terminated id of any length. */
int cfw_theme_id_valid(const char *id);
/* 32-byte field: a valid id followed only by NUL padding. */
int cfw_theme_id_field_valid(const char id[CFW_THEME_ID_BYTES]);
