/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef REPRISE_MEMORY_H
#define REPRISE_MEMORY_H

#ifdef UPLOAD_TEST_MEMORY
#include "test_memory.h"
#else
#define UPLOAD_CONFIG_QUALIFIER const volatile
#define UPLOAD_DATA ((uint8_t *)0x09800000u)
#define UPLOAD_RESULT ((struct upload_result *)0x2201fa80u)
#define STORAGE_RESULT ((struct storage_result *)0x2201fbc0u)
#endif

#endif
