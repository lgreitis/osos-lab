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
    HFS_TOO_LARGE,
    HFS_NOT_UNMOUNTED
};

struct hfs_volume {
    const struct hfs_disk *disk;
    uint64_t offset, bytes;
    uint32_t block_size, blocks;
};

/* HFS_NOT_UNMOUNTED still fills volume: the catalog can be read as stored,
 * without replaying the journal. */
enum hfs_result hfs_open(struct hfs_volume *volume, const struct hfs_disk *disk);
enum hfs_result hfs_fork_transfer(const struct hfs_volume *volume, const uint8_t *fork,
                                  uint64_t position, uint8_t *buffer, size_t count,
                                  hfs_io transfer);

/* Order of a catalog key against (parent, name), as the catalog B-tree sorts
 * them: negative when the key sorts first. name is printable ASCII. */
int hfs_key_compare(const uint8_t *key, uint32_t parent, const char *name,
                    size_t length);

/* Find the leaf record keyed (parent, name) in the catalog B-tree whose fork
 * data is catalog, and point record at its data in node, scratch_bytes long,
 * which holds one catalog node. Folder records are 88 bytes, file records 248. */
enum hfs_result hfs_find_record(const struct hfs_volume *volume, const uint8_t *catalog,
                                uint32_t parent, const char *name, size_t length,
                                const uint8_t **record, uint8_t *node,
                                size_t scratch_bytes);

/* Find the data fork of a file by absolute ASCII path ("/Reprise/Firmware/x").
 * scratch holds one catalog node. */
enum hfs_result hfs_find_file(const struct hfs_volume *volume, const char *path,
                              uint8_t fork[80], void *scratch, size_t scratch_bytes);

/* Read a file by absolute ASCII path on an HFS+ Apple Partition Map disk, also
 * when the volume was not cleanly unmounted. Pass separate destination and
 * scratch buffers. Discard output on error. */
enum hfs_result hfs_read_path(const struct hfs_disk *disk, const char *path, void *dst,
                              size_t capacity, size_t *length, void *scratch,
                              size_t scratch_bytes);
#endif
