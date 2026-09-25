#ifndef OSOS_GAME_LIBC_H
#define OSOS_GAME_LIBC_H

#include <stddef.h>

/* Supply these hooks when linking runtime/newlib_glue.c. The exit hook must
 * unwind to the current app callback; osos owns the task and stack. */
void game_libc_write(const char *text, size_t size);
_Noreturn void game_libc_exit(int status);

#endif
