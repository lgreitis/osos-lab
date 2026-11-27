/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */

/* What kind of disk the iPod has, from its ATA IDENTIFY data.
 *
 * The flash markers follow Rockbox firmware/export/ata.h ata_disk_isssd. Rockbox
 * is GPL-2.0-or-later; this version is GPL-3.0-only. */

#include <stdint.h>

enum {
    ID_CONFIG = 0,
    ID_MODEL = 27,
    ID_MODEL_WORDS = 20,
    ID_COMMAND_SET_2 = 83,
    ID_CF_ADVANCED_TIMING = 163,
    ID_FORM_FACTOR = 168,
    ID_DATA_SET_MANAGEMENT = 169,
    ID_ROTATION_RATE = 217,
};

/* IDENTIFY stores two ASCII characters per word, high byte first. */
static void model_text(const uint16_t *identify, char *model)
{
    for (int i = 0; i < ID_MODEL_WORDS; i++) {
        model[2 * i] = (char)(identify[ID_MODEL + i] >> 8);
        model[2 * i + 1] = (char)identify[ID_MODEL + i];
    }
    model[ID_MODEL_WORDS * 2] = 0;
}

static int contains(const char *text, const char *needle)
{
    for (; *text; text++) {
        int i = 0;
        while (needle[i] && text[i] == needle[i])
            i++;
        if (!needle[i])
            return 1;
    }
    return 0;
}

/* Toshiba (MK1634GAL) and Samsung (HS12YHA) 1.8" drives, the only hard disks
 * Apple fitted to these iPods, optionally after the vendor name. */
static int hard_disk_model(const char *model)
{
    if (contains(model, "TOSHIBA") || contains(model, "SAMSUNG"))
        return 1;
    for (const char *p = model; *p; p++) {
        int word_start = p == model || p[-1] == ' ';
        if (word_start &&
            ((p[0] == 'M' && p[1] == 'K') || (p[0] == 'H' && p[1] == 'S')) &&
            p[2] >= '0' && p[2] <= '9')
            return 1;
    }
    return 0;
}

/* Flash adapters report themselves inconsistently: a reported rotation rate
 * decides first, then the flash markers Rockbox checks, then the known hard-disk
 * models; any other disk in a Classic is an adapter. */
int rvx_storage_is_flash(const uint16_t *identify)
{
    char model[ID_MODEL_WORDS * 2 + 1];
    model_text(identify, model);
    if (!identify[ID_CONFIG] && !model[0])
        return 0; /* no IDENTIFY data: native behaviour */

    /* Some CF adapters byte-swap the solid-state value. */
    uint16_t rate = identify[ID_ROTATION_RATE];
    if (rate == 0x0001 || rate == 0x0100)
        return 1;
    if (rate >= 0x0401 && rate <= 0xfffe)
        return 0;

    if (identify[ID_CONFIG] == 0x848a || identify[ID_CONFIG] == 0x844a)
        return 1;
    /* Bits 15:14 = 01 mark the word valid; bit 2 is the CompactFlash feature set. */
    if ((identify[ID_COMMAND_SET_2] & 0xc004) == 0x4004)
        return 1;
    if ((identify[ID_FORM_FACTOR] & 0x0f) >= 0x06)
        return 1;
    if (identify[ID_DATA_SET_MANAGEMENT] & 1) /* TRIM */
        return 1;
    if (identify[ID_CF_ADVANCED_TIMING])
        return 1;
    if (contains(model, "iFlash"))
        return 1;
    return !hard_disk_model(model);
}
