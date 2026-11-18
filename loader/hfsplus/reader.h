/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CFW_HFSPLUS_READER_H
#define CFW_HFSPLUS_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*hfs_io)(void *context, uint64_t offset, void *buffer, size_t size);

struct hfs_disk {
    hfs_io read;
    void *context;
    uint64_t bytes;
};

enum hfs_result {
    HFS_OK,
    HFS_NOT_FOUND,
    HFS_INVALID,
    HFS_UNSUPPORTED,
    HFS_IO,
    HFS_TOO_LARGE
};

struct hfs_volume {
    const struct hfs_disk *disk;
    uint64_t offset, bytes;
    uint32_t block_size, blocks;
};

enum hfs_result hfs_open(struct hfs_volume *volume, const struct hfs_disk *disk);
enum hfs_result hfs_fork_transfer(const struct hfs_volume *volume, const uint8_t *fork,
                                  uint64_t position, uint8_t *buffer, size_t count,
                                  hfs_io transfer);

/* Read an ASCII-named root file on a clean HFS+ Apple Partition Map disk.
 * Pass separate destination and scratch buffers. Discard output on error. */
enum hfs_result hfs_read_root(const struct hfs_disk *disk, const char *name, void *dst,
                              size_t capacity, size_t *length, void *scratch,
                              size_t scratch_bytes);
#endif
