/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include "../../../loader/apple/layout.h"

#undef BOOT_BODY_BYTES
static uint32_t staged_body_bytes;
#define BOOT_BODY_BYTES staged_body_bytes
#include "../../../loader/apple/handoff/osos.h"

static unsigned char source[CFW_PAYLOAD_BYTES], destination[CFW_PAYLOAD_BYTES];
static jmp_buf failure;
static uint32_t copied;

void probe_finish(uint32_t status)
{
    assert(status == 3);
    longjmp(failure, 1);
}

static void copy(void *dest, const void *data, uint32_t bytes)
{
    assert((uintptr_t)dest == CFW_PAYLOAD_BASE);
    assert((uintptr_t)data ==
           (OSOS_STAGE | 0x80000000u) + CFW_PAYLOAD_FILE_OFFSET - OSOS_HEADER_BYTES);
    assert(bytes <= sizeof(destination));
    memcpy(destination, source, bytes);
    copied = bytes;
}

int main(void)
{
    const uint32_t offset = CFW_PAYLOAD_FILE_OFFSET - OSOS_HEADER_BYTES;
    const uint32_t valid[] = {4096, 328 * 1024, CFW_PAYLOAD_BYTES};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        uint32_t size = valid[i];
        memset(source, 0x35, sizeof(source));
        memset(source + size / 2, 0, size - size / 2);
        memset(destination, 0xa5, sizeof(destination));
        staged_body_bytes = offset + size;
        copied = 0;
        assert(!setjmp(failure));
        osos_prepare_entry();
        assert(copied == size);
        assert(!memcmp(destination, source, size));
        for (uint32_t at = size; at < sizeof(destination); at++)
            assert(destination[at] == 0xa5);
    }

    const uint32_t invalid[] = {0, offset - 1, offset, offset + CFW_PAYLOAD_BYTES + 1,
                                UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        staged_body_bytes = invalid[i];
        copied = 0;
        if (!setjmp(failure)) {
            osos_prepare_entry();
            assert(0);
        }
        assert(!copied);
    }
    return 0;
}
