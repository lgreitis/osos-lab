/* SPDX-License-Identifier: GPL-3.0-only */

#include "compat/nor/dispatch.h"

#define REC ((volatile uint32_t *)0x0bb30000)
extern void native_image_entry(void);
extern void native_stop(void);
extern void enter_osos(uint32_t) __attribute__((noreturn));
extern void restore_import(void) __attribute__((noreturn));

void native_prepare_image(uint32_t image)
{
    apple_prepare_driver(image);
}

void native_report(void)
{
    enter_osos(REC[0]);
}

void native_resume(uint32_t base)
{
    apple_validate_dispatch(base);
    REC[0] = base;
    REC[7] = apple_driver_resume(base);
    apple_intercept_dispatch(base, (uint32_t)native_stop, (uint32_t)native_image_entry);
    probe_flush();
    restore_import();
}
