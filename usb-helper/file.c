/* SPDX-License-Identifier: GPL-3.0-only */

#include "config.h"
#include "system.h"
#include "file.h"
#include "file_internal.h"
#include "disk.h"
#include "fat.h"
#include "ata.h"
#include "errno.h"
#include "string.h"
#include "upload.h"
#include "sha256.h"

const volatile struct upload_config upload_config = {.tag = "REPRISE-UPLOAD2"};
static int fd = -1;
static bool mounted, writes_enabled;
static uint64_t data_start, data_end;
static char temporary[64];
static struct sha256 hash;

/* Enabled only after the partition layout and mounted volume agree. */
int upload_write_allowed(uint64_t sector, int count)
{
    if (!writes_enabled || count <= 0 || sector < data_start || sector >= data_end ||
        (uint64_t)count > data_end - sector) {
        UPLOAD_RESULT->rejected++;
        return 0;
    }
    return 1;
}

static bool path_valid(const char *path)
{
    if (path[0] != '/' || !path[1])
        return false;
    unsigned start = 1;
    for (unsigned i = 1; i < 192; ++i) {
        unsigned c = (unsigned char)path[i];
        if (c == '/' || !c) {
            unsigned n = i - start;
            if (!n || (n == 1 && path[start] == '.') ||
                (n == 2 && path[start] == '.' && path[start + 1] == '.') ||
                path[i - 1] == '.' || path[i - 1] == ' ')
                return false;
            if (!c)
                return true;
            start = i + 1;
        } else if (c < 32 || c > 126 || strchr("\\:*?\"<>|", c))
            return false;
    }
    return false;
}

void upload_fail(int rc)
{
    if (!UPLOAD_RESULT->rc) {
        UPLOAD_RESULT->rc = rc;
        UPLOAD_RESULT->error_no = errno;
    }
    UPLOAD_RESULT->state = 4;
}

void upload_close(void)
{
    if (fd >= 0) {
        int result = close(fd);
        fd = -1;
        if (result)
            upload_fail(-320);
    }
    if (mounted) {
        int result = disk_unmount_all();
        mounted = false;
        if (result != 1)
            upload_fail(-321);
    }
    writes_enabled = false;
    ata_sleepnow();
}

/* Layout is checked by the helper before calling this function. */
void upload_prepare(uint64_t start, uint64_t end, unsigned bytes)
{
    char *path = UPLOAD_RESULT->path;
    for (unsigned i = 0; i < sizeof(upload_config.path); ++i)
        path[i] = upload_config.path[i];
    UPLOAD_RESULT->size = upload_config.size;
    if (!path_valid(path) || !strncasecmp(path, "/.reprise-", 10) ||
        upload_config.size > UPLOAD_LIMIT || upload_config.overwrite > 2) {
        upload_fail(-302);
        return;
    }
    const char hex[] = "0123456789abcdef";
    memcpy(temporary, "/.reprise-", 10);
    for (unsigned i = 0; i < 16; ++i) {
        temporary[10 + 2 * i] = hex[UPLOAD_RESULT->nonce[i] >> 4];
        temporary[11 + 2 * i] = hex[UPLOAD_RESULT->nonce[i] & 15];
    }
    memcpy(temporary + 42, ".part", 6);
    filesystem_init();
    int mounts = disk_mount_all();
    mounted = mounts > 0;
    struct partinfo p;
    bool match = false;
    for (int i = 0; i < 4; ++i)
        if (disk_partinfo(i, &p) && (p.type == 0x0b || p.type == 0x0c) &&
            p.start == start && p.size == end - start)
            match = true;
    if (mounts != 1 || !match ||
        (unsigned)fat_get_bytes_per_sector(IF_MV(0)) != bytes) {
        upload_fail(-303);
        return;
    }
    data_start = start;
    data_end = end;
    sector_t free_kib;
    if (!fat_size(IF_MV(0, ) NULL, &free_kib) ||
        (uint64_t)free_kib * 1024 <
            (uint64_t)upload_config.size + fat_get_cluster_size(IF_MV(0))) {
        upload_fail(-322);
        return;
    }
    if (upload_config.overwrite == 2) {
        STORAGE_RESULT->start = start;
        STORAGE_RESULT->end = end;
        STORAGE_RESULT->free_bytes = (uint64_t)free_kib * 1024;
        STORAGE_RESULT->sector_bytes = bytes;
        STORAGE_RESULT->existing_files =
            file_exists("/cfw-loader.bin") | (file_exists("/osos-cfw.bin") << 1);
        UPLOAD_RESULT->state = 4;
        return;
    }
    if (!upload_config.overwrite && file_exists(path)) {
        upload_fail(-304);
        return;
    }
    UPLOAD_RESULT->state = 1;
}

static void begin_readback(void)
{
    sha256_final(&hash, UPLOAD_RESULT->source_sha256);
    for (unsigned i = 0; i < 32; ++i)
        if (UPLOAD_RESULT->source_sha256[i] != upload_config.sha256[i]) {
            upload_fail(-305);
            return;
        }
    if (fsync(fd)) {
        upload_fail(-306);
        return;
    }
    int rc = close(fd);
    fd = -1;
    if (rc) {
        upload_fail(-307);
        return;
    }
    upload_close();
    if (UPLOAD_RESULT->rc)
        return;
    int mounts = disk_mount_all();
    mounted = mounts > 0;
    if (mounts != 1) {
        upload_fail(-308);
        return;
    }
    fd = open(temporary, O_RDONLY);
    if (fd < 0 || filesize(fd) != (off_t)upload_config.size) {
        upload_fail(-309);
        return;
    }
    sha256_init(&hash);
    UPLOAD_RESULT->state = 3;
}

void upload_start(void)
{
    writes_enabled = true;
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        upload_fail(-310);
        return;
    }
    sha256_init(&hash);
    if (!upload_config.size)
        begin_readback();
}

void upload_write(unsigned bytes)
{
    if (fd < 0 || bytes > UPLOAD_CHUNK ||
        bytes > upload_config.size - UPLOAD_RESULT->written) {
        upload_fail(-311);
        return;
    }
    sha256_update(&hash, UPLOAD_DATA, bytes);
    ssize_t n = write(fd, UPLOAD_DATA, bytes);
    if (n > 0)
        UPLOAD_RESULT->written += n;
    if (n != (ssize_t)bytes) {
        upload_fail(-312);
        return;
    }
    if (UPLOAD_RESULT->written == upload_config.size)
        begin_readback();
}

void upload_verify_step(void)
{
    unsigned left = upload_config.size - UPLOAD_RESULT->verified;
    unsigned count = MIN(left, UPLOAD_CHUNK);
    if (count) {
        ssize_t n = read(fd, UPLOAD_DATA, count);
        if (n != (ssize_t)count) {
            upload_fail(-313);
            return;
        }
        sha256_update(&hash, UPLOAD_DATA, count);
        UPLOAD_RESULT->verified += count;
        return;
    }
    sha256_final(&hash, UPLOAD_RESULT->disk_sha256);
    if (memcmp(UPLOAD_RESULT->disk_sha256, UPLOAD_RESULT->source_sha256, 32)) {
        upload_fail(-314);
        return;
    }
    int rc = close(fd);
    fd = -1;
    if (rc) {
        upload_fail(-315);
        return;
    }
    if (!upload_config.overwrite && file_exists(UPLOAD_RESULT->path)) {
        upload_fail(-304);
        return;
    }
    writes_enabled = true;
    if (rename(temporary, UPLOAD_RESULT->path)) {
        upload_fail(-316);
        return;
    }
    upload_close();
    UPLOAD_RESULT->state = 4;
}
