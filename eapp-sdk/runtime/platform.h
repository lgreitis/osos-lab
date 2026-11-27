/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/platform.h (8fc840d) by lgreitis. */

#ifndef EAPP_PLATFORM_H
#define EAPP_PLATFORM_H

#include "eapp.h"
#include "eapp_abi.h"
#include "eapp_libc.h"
#include "eapp_media.h"

void *eapp_platform_begin_frame(const struct eapp_abi_frame_input *input,
                                struct eapp_framebuffer *framebuffer);
void eapp_platform_present(void *surface);
/* The tick clock from the runtime's init, a host event stamp on that clock,
 * and a seed from the clock for the random stream. */
void eapp_clock_start(void);
uint32_t eapp_clock_stamp_ms(uint32_t host_ms);
uint32_t eapp_clock_seed(void);
/* The host's wheel: the clockwise motion since the last read, in 96ths of a
 * turn, and the touch. */
struct eapp_wheel eapp_input_read_wheel(void);
void eapp_files_close_all(void);
void eapp_directories_close_all(void);
void eapp_saves_close_all(void);
void eapp_sounds_free_all(void);
/* Makes `bytes` of code written at `start` visible to the instruction fetch:
 * cleans their data cache lines and invalidates the instruction cache. */
void eapp_cache_sync_code(const void *start, size_t bytes);

/* A media object of the app is the host's handle plus one, so NULL is
 * none. */
#define EAPP_MEDIA_HANDLE(object) ((int32_t)(uintptr_t)(object) - 1)
#define EAPP_MEDIA_OBJECT(type, handle) ((type *)eapp_media_object(handle))
static inline void *eapp_media_object(int32_t handle)
{
    return handle >= 0 ? (void *)(uintptr_t)(handle + 1) : NULL;
}
/* The host's library handle once entry `index` of `list` is readable for
 * the current selection, else -1. */
int32_t eapp_media_library(enum eapp_media_list list, uint32_t index);
/* Text the host's Copy entry `copy` writes for `handle`, into `out`. */
int eapp_media_copy_text(void (*copy)(int32_t, char *, uint32_t *), int32_t handle, char *out,
                         size_t capacity);

/* Terminates text the host wrote into `out` of `capacity` bytes: `written`
 * bytes landed, `length` is the whole text; a cut text ends before the
 * character the cut went through. Returns `length`. */
int eapp_text_finish(char *out, size_t capacity, size_t written, size_t length);
/* `text` into `out` of `capacity` bytes, cut the same way; returns its length. */
int eapp_text_copy(char *out, size_t capacity, const char *text);

/* `name` without leading "./", or NULL when it leaves its folder: a leading
 * slash, an empty, "." or ".." component, a trailing slash, a backslash or a
 * colon. "" is accepted only with allow_root. */
const char *eapp_check_name(const char *name, int allow_root);

/* A request of `type` for the host's file worker, with the outputs preset to
 * EAPP_REQUEST_PENDING so a wait can see them arrive. The request is spent
 * once finished. */
#define EAPP_REQUEST_PENDING 0xffffffffu
struct GameFileRequest *eapp_request_new(uint32_t type);
/* Waits for the request's result when `queued` (what the host's Queue entry
 * returned) is nonzero and then, on success, for every output in `outputs`,
 * all the fields the operation writes; returns the result, or
 * FileStatus_NoSlot when the request was not queued. After a failure with
 * EAPP_REQUEST_HANDLE among the outputs, a handle other than
 * EAPP_REQUEST_PENDING is one the host registered anyway and the caller
 * closes or releases. */
enum eapp_request_output {
    EAPP_REQUEST_SIZE = 1,
    EAPP_REQUEST_HANDLE = 2,
    EAPP_REQUEST_TRANSFERRED = 4,
};
int eapp_request_finish(struct GameFileRequest *request, int queued, unsigned outputs);
/* What eapp_file_error reports; set by a failed request and by save.c. */
extern uint32_t eapp_file_error_status;

#endif
