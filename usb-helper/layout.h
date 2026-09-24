/* SPDX-License-Identifier: GPL-3.0-only */

struct disk_layout {
    int32_t rc[5];
    uint32_t mask, lba[4];
    uint8_t sectors[5][512] __attribute__((aligned(32)));
};
static struct disk_layout layout;
static uint8_t readback[512] __attribute__((aligned(32)));

static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int read_layout_sector(unsigned slot, uint32_t lba)
{
    int rc = ata_read_sectors(IF_MD(0, ) lba, 1, layout.sectors[slot]);
    if (!rc)
        rc = ata_read_sectors(IF_MD(0, ) lba, 1, readback);
    if (!rc && memcmp(readback, layout.sectors[slot], 512))
        rc = -201;
    layout.rc[slot] = rc;
    if (!rc)
        layout.mask |= 1u << slot;
    return rc;
}

static void read_layout(void)
{
    memset(&layout, 0, sizeof(layout));
    for (unsigned i = 0; i < 5; ++i)
        layout.rc[i] = -200;
    if (RECORD->sector_size != 512)
        return;
    if (read_layout_sector(0, 0))
        return;
    const uint8_t *mbr = layout.sectors[0];
    if (mbr[510] != 0x55 || mbr[511] != 0xaa)
        return;
    uint64_t total = ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low;
    unsigned scale = 0, fat_slot = 0;
    /* Rockbox tries 512–4096-byte partition units on this target. */
    for (unsigned i = 0; i < 4 && !scale; ++i) {
        const uint8_t *entry = mbr + 446 + i * 16;
        if (entry[4] != 0x0b && entry[4] != 0x0c)
            continue;
        uint32_t start = le32(entry + 8), count = le32(entry + 12);
        if (!start || !count)
            continue;
        for (unsigned mult = 1; mult <= 8; mult <<= 1) {
            if (((uint64_t)start + count) * mult > total ||
                (uint64_t)start * mult > UINT32_MAX)
                continue;
            layout.lba[i] = start * mult;
            if (read_layout_sector(i + 1, start * mult))
                continue;
            const uint8_t *v = layout.sectors[i + 1];
            unsigned bytes = v[11] | ((unsigned)v[12] << 8);
            if (v[510] == 0x55 && v[511] == 0xaa && bytes == 512 * mult) {
                scale = mult;
                fat_slot = i + 1;
                break;
            }
        }
    }
    if (!scale)
        return;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *entry = mbr + 446 + i * 16;
        uint32_t start = le32(entry + 8), count = le32(entry + 12);
        if (!count || !start || ((uint64_t)start + count) * scale > total ||
            (uint64_t)start * scale > UINT32_MAX)
            continue;
        layout.lba[i] = start * scale;
        if (i + 1 == fat_slot)
            continue;
        read_layout_sector(i + 1, start * scale);
    }
}
