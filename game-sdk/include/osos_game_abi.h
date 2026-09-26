/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef OSOS_GAME_ABI_H
#define OSOS_GAME_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Recovered Classic 7G FW2.0.4 ABI; all address fields are 32-bit ARM addresses. */
#define OSOS_EAPP_MAGIC UINT32_C(0x70706165)
#define OSOS_EAPP_MAX_VERSION UINT32_C(0x10001000)
#define OSOS_EAPP_LOAD_ADDRESS UINT32_C(0x18000000)
#define OSOS_EAPP_MAX_IMAGE_BYTES UINT32_C(0x00100000)

enum osos_game_entry_slot {
    OSOS_GAME_ENTRY_INIT = 0,
    OSOS_GAME_ENTRY_SHUTDOWN = 1,
    OSOS_GAME_ENTRY_FRAME = 4,
    OSOS_GAME_ENTRY_COUNT = 5,
};

struct osos_eapp_header {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t unknown_0c;
    uint32_t imports_address;
    uint32_t entry_address[OSOS_GAME_ENTRY_COUNT];
};

struct osos_game_input_event {
    uint8_t button;
    uint8_t type;
    uint8_t unknown_02[2];
    uint32_t value;
    uint32_t next_address;
};

struct osos_game_frame_input {
    uint8_t state;
    uint8_t unknown_01[0x2f];
    uint32_t events_address;
    uint8_t unknown_34[4];
    uint32_t pixels_address;
    uint8_t unknown_3c[0x100 - 0x3c];
};

/* State 6 ends both active frames and the subsequent shutdown-drain loop. */
#define OSOS_GAME_FRAME_DONE 6

struct osos_game_frame_output {
    uint8_t state;
    uint8_t unknown_01[0xf7];
};

_Static_assert(sizeof(struct osos_eapp_header) == 0x28, "eapp header size");
_Static_assert(offsetof(struct osos_eapp_header, imports_address) == 0x10,
               "eapp imports offset");
_Static_assert(offsetof(struct osos_eapp_header, entry_address) == 0x14,
               "eapp entries offset");
_Static_assert(sizeof(struct osos_game_input_event) == 12, "input event size");
_Static_assert(offsetof(struct osos_game_input_event, value) == 4,
               "input value offset");
_Static_assert(offsetof(struct osos_game_input_event, next_address) == 8,
               "input next offset");
_Static_assert(sizeof(struct osos_game_frame_input) == 0x100, "frame input size");
_Static_assert(offsetof(struct osos_game_frame_input, events_address) == 0x30,
               "frame events offset");
_Static_assert(offsetof(struct osos_game_frame_input, pixels_address) == 0x38,
               "frame pixels offset");
_Static_assert(sizeof(struct osos_game_frame_output) == 0xf8, "frame output size");

#endif
