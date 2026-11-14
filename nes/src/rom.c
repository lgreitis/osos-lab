/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_api.h"
#include <stdbool.h>
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "rom.h"

const char *nes_error;

static bool read_exact(struct game_file *file, void *data, unsigned size)
{
    BYTE *out = data;
    while (size) {
        int count = game_file_read(file, out, size);
        if (count <= 0)
            return false;
        out += count;
        size -= count;
    }
    return true;
}

void InfoNES_ReleaseRom(void)
{
    game_free(ROM);
    game_free(VROM);
    ROM = VROM = NULL;
}

int InfoNES_ReadRom(const char *path)
{
    struct game_file *file = game_file_open(path);
    nes_error = "CANNOT OPEN ROM";
    if (!file)
        return -1;

    bool valid = read_exact(file, &NesHeader, sizeof(NesHeader));
    unsigned prg = NesHeader.byRomSize * 16384u;
    unsigned chr = NesHeader.byVRomSize * 8192u;
    unsigned trainer = (NesHeader.byInfo1 & 4) ? 512 : 0;
    unsigned mapper = (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xf0);
    nes_error = "INVALID OR UNSUPPORTED INES ROM";
    valid = valid && NesHeader.byID[0] == 'N' && NesHeader.byID[1] == 'E' &&
            NesHeader.byID[2] == 'S' && NesHeader.byID[3] == 0x1a &&
            !(NesHeader.byInfo2 & 0x0f) && NesHeader.byReserve[0] <= 1 &&
            !(NesHeader.byReserve[1] & ~1) && prg && prg + chr <= 4 * 1024 * 1024 &&
            game_file_size(file) >= 16 + trainer + prg + chr;
    for (unsigned i = 2; i < 8; ++i)
        valid = valid && NesHeader.byReserve[i] == 0;
    valid = valid && (mapper == 0 || mapper == 1 || mapper == 2 || mapper == 3 ||
                      mapper == 4 || mapper == 7 || mapper == 66);
    if (mapper == 0 || mapper == 3)
        valid = valid && prg <= 32768;
    if (mapper == 4 || mapper == 7 || mapper == 66)
        valid = valid && prg >= 32768;
    if (mapper == 3 || mapper == 66)
        valid = valid && chr != 0;
    if (mapper == 0 || mapper == 2 || mapper == 7)
        valid = valid && chr <= 8192;
    if (!valid) {
        game_file_close(file);
        return -1;
    }

    InfoNES_MemorySet(SRAM, 0, SRAM_SIZE);
    ROM = game_alloc(prg);
    VROM = chr ? game_alloc(chr) : NULL;
    nes_error = "NOT ENOUGH GAME MEMORY";
    valid = ROM && (!chr || VROM);
    if (valid) {
        nes_error = "ROM READ FAILED";
        valid = (!trainer || read_exact(file, SRAM + 0x1000, trainer)) &&
                read_exact(file, ROM, prg) && (!chr || read_exact(file, VROM, chr));
    }
    game_file_close(file);
    if (!valid) {
        InfoNES_ReleaseRom();
        return -1;
    }
    nes_error = NULL;
    return 0;
}
