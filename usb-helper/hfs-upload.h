/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef REPRISE_HFS_UPLOAD_H
#define REPRISE_HFS_UPLOAD_H
#include <stdint.h>
#include <stdbool.h>
int hfs_upload_mount(uint64_t sectors, uint64_t *start, uint64_t *end,
                     uint64_t *free_bytes);
int hfs_upload_exists(const char *path);
int hfs_upload_plan(const char *path, uint32_t size, bool overwrite);
int hfs_upload_data(uint32_t pos, void *data, unsigned len, bool write);
int hfs_upload_commit(void);
#endif
