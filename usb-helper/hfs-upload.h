/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef REPRISE_HFS_UPLOAD_H
#define REPRISE_HFS_UPLOAD_H
#include <stdint.h>
#include <stdbool.h>
/* hfs_upload_mount: the disk holds no HFS+ volume. */
#define HFS_UPLOAD_NOT_FOUND 1
int hfs_upload_mount(uint64_t sectors, uint64_t *start, uint64_t *end,
                     uint64_t *free_bytes);
int hfs_upload_exists(const char *path);
/* Files one publish takes. */
#define HFS_BATCH 64
/* Stage a file: its data goes to free blocks through hfs_upload_data, and
 * hfs_upload_commit publishes every staged file in one catalog rebuild. */
int hfs_upload_plan(const char *path, uint32_t size, bool overwrite);
/* Data of the file staged last. */
int hfs_upload_data(uint32_t pos, void *data, unsigned len, bool write);
int hfs_upload_commit(void);
/* Files staged and not yet published. */
unsigned hfs_upload_staged(void);
/* Roots one removal takes. */
#define HFS_REMOVE_ROOTS 16

/* A root of a removal: a folder with everything below it, or a file. outcome:
 * 0 when the path did not exist, 1 when parts of it remain, 2 when it was
 * removed. The folders above a root go too when the removal empties them. */
struct hfs_removal {
    const char *path;
    uint8_t folder, outcome;
};

int hfs_upload_remove(struct hfs_removal *set, unsigned count);
/* Read back a published file through the catalog. */
int hfs_upload_open(const char *path, uint32_t *size);
int hfs_upload_read(uint32_t pos, void *data, unsigned len);
#endif
