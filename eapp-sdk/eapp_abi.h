/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/include/osos_game_abi.h (8fc840d) by lgreitis. */

#ifndef EAPP_ABI_H
#define EAPP_ABI_H

#include "osos-2.0.5.h"
#include <stddef.h>
#include <stdint.h>

/* What Apple's eApp host reads from an image and hands to its frame entry. The
 * values are the symbols header's; the structs give them C names. */

enum eapp_abi_slot {
    EAPP_ABI_SLOT_INIT = 0,
    EAPP_ABI_SLOT_SHUTDOWN = 1,
    EAPP_ABI_SLOT_FRAME = 4,
    EAPP_ABI_SLOT_COUNT = GameImage_MaxEntries,
};

struct eapp_abi_header {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t reserved;
    uint32_t imports;
    uint32_t entries[EAPP_ABI_SLOT_COUNT];
};

struct eapp_abi_input_event {
    uint8_t button;
    uint8_t type;
    uint8_t reserved[2];
    uint32_t timestamp;
    uint32_t next;
};

struct eapp_abi_frame_input {
    uint8_t state;
    uint8_t reserved_01[GameFrameInput_completed_requests - 1];
    uint32_t completed_requests; /* The file requests the worker finished; not read. */
    uint32_t events;
    uint8_t reserved_34[GameFrameInput_pixels - GameFrameInput_events - 4];
    uint32_t pixels;
    uint8_t reserved_3c[GameFrameInput_bytes - GameFrameInput_pixels - 4];
};

struct eapp_abi_frame_output {
    uint8_t state;
    uint8_t reserved[GameFrameOutput_bytes - 1];
};

/* A request to the host's file worker. The caller sets type and the inputs
 * and zeroes next; the worker writes result, transferred, size and handle and
 * links the request into the completed list of a later frame through the
 * previous request's next. The words after next are the game's own; the host
 * never touches them or anything past handle. */
struct GameFileRequest {
    uint32_t next;
    uint32_t reserved_04[(GameFileRequest_type - 4) / 4];
    uint32_t type;
    uint32_t completion;
    int32_t offset;
    uint32_t whence;
    uint32_t buffer;
    uint32_t length;
    uint32_t result;
    uint32_t transferred;
    uint32_t size;
    uint32_t handle;
};

_Static_assert(sizeof(struct eapp_abi_header) == GameImageHeader_bytes, "eapp header");
_Static_assert(offsetof(struct eapp_abi_header, imports) == GameImageHeader_imports,
               "eapp imports");
_Static_assert(offsetof(struct eapp_abi_header, entries) == GameImageHeader_entries,
               "eapp entries");
_Static_assert(sizeof(struct eapp_abi_input_event) == GameInputEvent_bytes, "event");
_Static_assert(offsetof(struct eapp_abi_input_event, timestamp) == GameInputEvent_timestamp,
               "event timestamp");
_Static_assert(offsetof(struct eapp_abi_input_event, next) == GameInputEvent_next,
               "event next");
_Static_assert(sizeof(struct eapp_abi_frame_input) == GameFrameInput_bytes, "frame in");
_Static_assert(offsetof(struct eapp_abi_frame_input, completed_requests) ==
                   GameFrameInput_completed_requests,
               "frame completed requests");
_Static_assert(offsetof(struct eapp_abi_frame_input, events) == GameFrameInput_events,
               "frame events");
_Static_assert(offsetof(struct eapp_abi_frame_input, pixels) == GameFrameInput_pixels,
               "frame pixels");
_Static_assert(sizeof(struct GameFileRequest) == GameFileRequest_handle + 4, "request");
_Static_assert(offsetof(struct GameFileRequest, type) == GameFileRequest_type &&
                   offsetof(struct GameFileRequest, completion) ==
                       GameFileRequest_completion &&
                   offsetof(struct GameFileRequest, offset) == GameFileRequest_offset &&
                   offsetof(struct GameFileRequest, whence) == GameFileRequest_whence &&
                   offsetof(struct GameFileRequest, buffer) == GameFileRequest_buffer &&
                   offsetof(struct GameFileRequest, length) == GameFileRequest_length &&
                   offsetof(struct GameFileRequest, result) == GameFileRequest_result &&
                   offsetof(struct GameFileRequest, transferred) ==
                       GameFileRequest_transferred &&
                   offsetof(struct GameFileRequest, size) == GameFileRequest_size &&
                   offsetof(struct GameFileRequest, handle) == GameFileRequest_handle,
               "request fields");
_Static_assert(sizeof(struct eapp_abi_frame_output) == GameFrameOutput_bytes,
               "frame out");

#endif
