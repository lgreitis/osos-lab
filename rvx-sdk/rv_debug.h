/* SPDX-License-Identifier: GPL-3.0-only */

/* What api->debug_info hands a debugger: where the core's state is in memory
 * and what a debugger may do that an extension may not. */

#ifndef RV_DEBUG_H
#define RV_DEBUG_H
#include "rv_api.h"

/* What the core reports to a debugger's trace probe, with the argument. */
enum rv_trace_kind {
    RV_TRACE_FLUSH_BEGIN = 1, /* the settings store's bytes; the write starts */
    RV_TRACE_FLUSH_END,       /* 1 when written, 0 when the store stays dirty */
    RV_TRACE_LOAD_BEGIN,      /* an extension's owner; one of its modules loads */
    RV_TRACE_LOAD_END,        /* the owner; its record's failure says how it went */
    RV_TRACE_UNLOAD,          /* the owner; one of its modules unloads */
    RV_TRACE_ASSET_BEGIN,     /* the owner; an asset is read and decoded */
    RV_TRACE_ASSET_END,       /* the owner */
    RV_TRACE_DECODE_BEGIN, /* bytes the lz decoder produces, for an asset or a core icon */
    RV_TRACE_DECODE_END, /* 1 when the stream was whole */
};

/* The record offsets are into the loader's per-extension record; `file` is
 * the NUL-terminated file name without .rvx, at most 32 bytes, `failure` a
 * byte, the others words. Grows like rv_api: test a newer entry with RV_HAS. */
struct rv_debug_info {
    uint32_t size;          /* bytes of this record */
    uint32_t core_build;    /* the core's git revision, 8 hex digits */
    const void *extensions; /* address of the pointer to the first record */
    uint16_t next, owner, file, live, peak, failure;
    const void *heap; /* Apple's MainHeap */
    /* Turns the extension in /Revola/Extensions/<file>.rvx on or off as Settings >
     * Extensions does. UI task. */
    int (*set_enabled)(const char *file, int enabled);
    /* The arguments a filter on core site `site` (an RV_SITE_* id) receives:
     * registers in bits 0-3, stack words in bits 4-5. -1 for an unknown id and
     * for sites that take no filter (word, inserted and no-return sites). */
    int (*site_argc)(uint32_t site);
    /* The core's trace probe slot. A debugger stores a probe here, which the core
     * calls at its tracepoints with an rv_trace_kind and its argument, and clears
     * it before its module unloads; a tracepoint reads the slot once. */
    void (*volatile *trace)(uint32_t kind, uint32_t argument);
};

#endif
