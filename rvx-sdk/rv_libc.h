/* SPDX-License-Identifier: GPL-3.0-only */

/* The C library of an extension: rv.mk links rv_libc.c into every module and
 * drops what a module does not call. GCC emits calls to memcpy, memmove,
 * memset and memcmp for struct copies and initialisers even in freestanding
 * code, so those four are always available under their standard names. */

#ifndef RV_LIBC_H
#define RV_LIBC_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* On a declaration of code rv.mk links into the module itself (rv_libc,
 * rv_canvas): the call is a plain bl, since the module is one segment,
 * instead of the long call a module makes into Apple's code or a library
 * extension. */
#ifdef __arm__
#define RV_MODULE_CALL __attribute__((short_call))
#else
#define RV_MODULE_CALL /* a host build of the tests */
#endif

void *memcpy(void *to, const void *from, size_t bytes);
void *memmove(void *to, const void *from, size_t bytes);
void *memset(void *to, int value, size_t bytes);
int memcmp(const void *a, const void *b, size_t bytes);
RV_MODULE_CALL size_t strlen(const char *text);
RV_MODULE_CALL int strcmp(const char *a, const char *b);

/* printf into `out`, NUL-terminated within `capacity`, cut to fit; the length
 * written. Conversions: %d %i %u %x %X %c %s %%, with the flags - and 0, a
 * width and, for %s, a precision (either may be *). int and long are both 32
 * bits; uint32_t is unsigned long here, so it takes %lu or %lx. Anything else
 * is copied as written. */
RV_MODULE_CALL uint32_t rv_format(char *out, uint32_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
RV_MODULE_CALL uint32_t rv_vformat(char *out, uint32_t capacity, const char *format,
                                   va_list args);

/* A duration as a track time: "3:07" under an hour, "1:02:03" from one. */
RV_MODULE_CALL uint32_t rv_format_duration(char *out, uint32_t capacity,
                                           uint32_t seconds);

/* A whole value with `unit` after it: "-3 dB", "100 %". */
RV_MODULE_CALL uint32_t rv_format_int(char *out, uint32_t capacity, int value,
                                      const char *unit);

/* A value in tenths with one decimal and `unit` after it: "-0.5 dB", "0.7"
 * with "". A value above zero gets a plus when `plus` is set: "+12.0 dB". */
RV_MODULE_CALL uint32_t rv_format_tenths(char *out, uint32_t capacity, int tenths,
                                         int plus, const char *unit);

/* A frequency: "60 Hz" below a kilohertz, then "1k Hz", "3.15k Hz". */
RV_MODULE_CALL uint32_t rv_format_hz(char *out, uint32_t capacity, uint32_t hz);

/* `text` into `out`, NUL-terminated within `capacity` and never cut inside a
 * UTF-8 sequence; the length copied. */
RV_MODULE_CALL uint32_t rv_copy_utf8(char *out, uint32_t capacity, const char *text);

static inline int rv_clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

#endif
