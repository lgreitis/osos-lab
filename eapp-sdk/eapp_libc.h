/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/include/game_libc.h (8fc840d) by lgreitis. */

#ifndef EAPP_LIBC_H
#define EAPP_LIBC_H

#include <stddef.h>

/* The runtime's, under their standard names: GCC emits calls to them for
 * struct copies and initialisers even without a C library, and apps call
 * them for their own copies. An app that links newlib gets the same
 * declarations from <string.h>. */
void *memcpy(void *to, const void *from, size_t bytes);
void *memmove(void *to, const void *from, size_t bytes);
void *memset(void *to, int value, size_t bytes);
int memcmp(const void *a, const void *b, size_t bytes);

/* An app that links runtime/newlib_glue.c supplies these. The exit hook must
 * unwind to the current app callback: the host owns the task and its stack. */
void eapp_libc_write(const char *text, size_t size);
_Noreturn void eapp_libc_exit(int status);

#endif
