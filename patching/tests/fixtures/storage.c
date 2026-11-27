/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../payload/rvx/storage.c"

static void set_model(uint16_t *identify, const char *model)
{
    for (int i = 0; i < 20; i++) {
        char high = model[2 * i], low = high ? model[2 * i + 1] : 0;
        identify[27 + i] = (uint16_t)((uint8_t)high << 8 | (uint8_t)low);
        if (!high || !low)
            break;
    }
}

int main(void)
{
    uint16_t identify[256];

    /* No IDENTIFY data keeps Apple's behaviour. */
    memset(identify, 0, sizeof identify);
    assert(!rvx_storage_is_flash(identify));

    /* A rotation rate decides first, in either byte order. */
    memset(identify, 0, sizeof identify);
    identify[0] = 0x045a;
    set_model(identify, "TOSHIBA MK1634GAL");
    identify[217] = 0x0001;
    assert(rvx_storage_is_flash(identify));
    identify[217] = 0x0100;
    assert(rvx_storage_is_flash(identify));
    identify[217] = 4200;
    set_model(identify, "iFlash-Quad");
    assert(!rvx_storage_is_flash(identify));

    /* Apple's drives, with and without the vendor name. */
    memset(identify, 0, sizeof identify);
    identify[0] = 0x045a;
    set_model(identify, "TOSHIBA MK1634GAL");
    assert(!rvx_storage_is_flash(identify));
    set_model(identify, "MK1634GAL");
    assert(!rvx_storage_is_flash(identify));
    set_model(identify, "SAMSUNG HS12YHA");
    assert(!rvx_storage_is_flash(identify));
    set_model(identify, "HS12YHA");
    assert(!rvx_storage_is_flash(identify));

    /* The flash markers. */
    set_model(identify, "SD CARD");
    assert(rvx_storage_is_flash(identify));
    set_model(identify, "TOSHIBA MK1634GAL");
    identify[0] = 0x848a;
    assert(rvx_storage_is_flash(identify));
    identify[0] = 0x045a;
    identify[83] = 0x4004;
    assert(rvx_storage_is_flash(identify));
    identify[83] = 0x0004; /* not marked valid */
    assert(!rvx_storage_is_flash(identify));
    identify[83] = 0;
    identify[168] = 0x0006;
    assert(rvx_storage_is_flash(identify));
    identify[168] = 0;
    identify[169] = 1;
    assert(rvx_storage_is_flash(identify));
    identify[169] = 0;
    identify[163] = 0x0007;
    assert(rvx_storage_is_flash(identify));
    identify[163] = 0;
    set_model(identify, "iFlash-Solo");
    assert(rvx_storage_is_flash(identify));
    return 0;
}
