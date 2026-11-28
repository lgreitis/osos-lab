/* SPDX-License-Identifier: GPL-3.0-only */

#include "config.h"
#include "system.h"
#include "file.h"
#include "dir.h"
#include "file_internal.h"
#include "disk.h"
#include "fat.h"
#include "ata.h"
#include "errno.h"
#include "string.h"
#include "upload.h"
#include "hfs-upload.h"
#include "sha256.h"

UPLOAD_CONFIG_QUALIFIER struct upload_config upload_config = {.tag = "REPRISE-UPLOAD2"};
/* The file being handled: the image config first, then each NEXT request. */
static struct upload_config current;
static int fd = -1;
static bool hfs, mounted, writes_enabled, closed;
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
    if (closed)
        return;
    closed = true;
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

static bool current_valid(void)
{
    memcpy(UPLOAD_RESULT->path, current.path, sizeof(current.path));
    UPLOAD_RESULT->size = current.size;
    return path_valid(current.path) && strncasecmp(current.path, "/.reprise-", 10) &&
           current.size <= UPLOAD_LIMIT;
}

static void prepare_file(void)
{
    if (hfs) {
        int rc = hfs_upload_plan(current.path, current.size, current.overwrite);
        if (rc)
            upload_fail(rc);
        else
            UPLOAD_RESULT->state = 1;
        return;
    }
    sector_t free_kib;
    if (!fat_size(IF_MV(0, ) NULL, &free_kib) ||
        (uint64_t)free_kib * 1024 < (uint64_t)current.size + fat_get_cluster_size(IF_MV(0))) {
        upload_fail(-322);
        return;
    }
    if (!current.overwrite && file_exists(current.path)) {
        upload_fail(-304);
        return;
    }
    UPLOAD_RESULT->state = 1;
}

static bool load_config(void)
{
    const volatile uint8_t *source = (const volatile uint8_t *)&upload_config;
    for (unsigned i = 0; i < sizeof(current); ++i)
        ((uint8_t *)&current)[i] = source[i];
    if (current_valid() && current.overwrite <= 2)
        return true;
    upload_fail(-302);
    return false;
}

void upload_prepare_hfs(uint64_t sectors)
{
    if (!load_config())
        return;
    uint64_t free_bytes;
    int rc = hfs_upload_mount(sectors, &data_start, &data_end, &free_bytes);
    if (rc) {
        upload_fail(rc);
        return;
    }
    hfs = true;
    if (current.overwrite < 2) {
        prepare_file();
        return;
    }
    int companion = hfs_upload_exists("/Reprise/Firmware/loader.bin");
    int osos = hfs_upload_exists("/Reprise/Firmware/reprise.bin");
    if (companion < 0 || osos < 0 || free_bytes < current.size) {
        upload_fail(companion < 0 ? companion : osos < 0 ? osos : -322);
        return;
    }
    STORAGE_RESULT->start = data_start;
    STORAGE_RESULT->end = data_end;
    STORAGE_RESULT->sector_bytes = 512;
    STORAGE_RESULT->free_bytes = free_bytes;
    STORAGE_RESULT->existing_files = companion | (osos << 1);
    UPLOAD_RESULT->state = 4;
}

void upload_prepare(uint64_t start, uint64_t end, unsigned bytes)
{
    if (!load_config())
        return;
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
    if (current.overwrite < 2) {
        prepare_file();
        return;
    }
    sector_t free_kib;
    if (!fat_size(IF_MV(0, ) NULL, &free_kib) ||
        (uint64_t)free_kib * 1024 < (uint64_t)current.size + fat_get_cluster_size(IF_MV(0))) {
        upload_fail(-322);
        return;
    }
    STORAGE_RESULT->start = start;
    STORAGE_RESULT->end = end;
    STORAGE_RESULT->free_bytes = (uint64_t)free_kib * 1024;
    STORAGE_RESULT->sector_bytes = bytes;
    STORAGE_RESULT->existing_files =
        file_exists("/Reprise/Firmware/loader.bin") |
        (file_exists("/Reprise/Firmware/reprise.bin") << 1);
    UPLOAD_RESULT->state = 4;
}

/* Queues another file on the mounted volume once the previous one is complete.
   `next` carries the nonce where the image config carries its tag. */
void upload_next(const struct upload_config *next)
{
    if (UPLOAD_RESULT->state != 4 || UPLOAD_RESULT->rc || current.overwrite > 1)
        return;
    memcpy(&current, next, sizeof(current));
    UPLOAD_RESULT->received = 0;
    UPLOAD_RESULT->written = 0;
    UPLOAD_RESULT->verified = 0;
    UPLOAD_RESULT->error_no = 0;
    UPLOAD_RESULT->rejected = 0;
    memset(UPLOAD_RESULT->source_sha256, 0, sizeof(UPLOAD_RESULT->source_sha256));
    memset(UPLOAD_RESULT->disk_sha256, 0, sizeof(UPLOAD_RESULT->disk_sha256));
    if (!current_valid() || current.overwrite > 1) {
        upload_fail(-302);
        return;
    }
    prepare_file();
}

static void begin_readback(void)
{
    sha256_final(&hash, UPLOAD_RESULT->source_sha256);
    for (unsigned i = 0; i < 32; ++i)
        if (UPLOAD_RESULT->source_sha256[i] != current.sha256[i]) {
            upload_fail(-305);
            return;
        }
    if (hfs) {
        if (ata_flush()) {
            upload_fail(-306);
            return;
        }
        sha256_init(&hash);
        UPLOAD_RESULT->state = 3;
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
    closed = false;
    int mounts = disk_mount_all();
    mounted = mounts > 0;
    if (mounts != 1) {
        upload_fail(-308);
        return;
    }
    fd = open(temporary, O_RDONLY);
    if (fd < 0 || filesize(fd) != (off_t)current.size) {
        upload_fail(-309);
        return;
    }
    sha256_init(&hash);
    UPLOAD_RESULT->state = 3;
}

void upload_start(void)
{
    writes_enabled = true;
    if (hfs) {
        sha256_init(&hash);
        if (!current.size)
            begin_readback();
        return;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        upload_fail(-310);
        return;
    }
    sha256_init(&hash);
    if (!current.size)
        begin_readback();
}

void upload_write(unsigned bytes)
{
    if ((!hfs && fd < 0) || bytes > UPLOAD_CHUNK ||
        bytes > current.size - UPLOAD_RESULT->written) {
        upload_fail(-311);
        return;
    }
    sha256_update(&hash, UPLOAD_DATA, bytes);
    ssize_t n = bytes;
    if (hfs) {
        if (hfs_upload_data(UPLOAD_RESULT->written, UPLOAD_DATA, bytes, true))
            n = -1;
    } else {
        n = write(fd, UPLOAD_DATA, bytes);
    }
    if (n > 0)
        UPLOAD_RESULT->written += n;
    if (n != (ssize_t)bytes) {
        upload_fail(-312);
        return;
    }
    if (UPLOAD_RESULT->written == current.size)
        begin_readback();
}

static int create_parents(const char *path)
{
    char parent[sizeof(current.path)];
    strcpy(parent, path);
    for (char *slash = strchr(parent + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        if (!dir_exists(parent) && mkdir(parent))
            return -1;
        *slash = '/';
    }
    return 0;
}

void upload_verify_step(void)
{
    unsigned left = current.size - UPLOAD_RESULT->verified;
    unsigned count = MIN(left, UPLOAD_CHUNK);
    if (count) {
        ssize_t n = count;
        if (hfs) {
            if (hfs_upload_data(UPLOAD_RESULT->verified, UPLOAD_DATA, count, false))
                n = -1;
        } else {
            n = read(fd, UPLOAD_DATA, count);
        }
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
    if (hfs) {
        int rc = hfs_upload_commit();
        writes_enabled = false;
        if (rc)
            upload_fail(rc);
        else
            UPLOAD_RESULT->state = 4;
        return;
    }
    int rc = close(fd);
    fd = -1;
    if (rc) {
        upload_fail(-315);
        return;
    }
    if (!current.overwrite && file_exists(UPLOAD_RESULT->path)) {
        upload_fail(-304);
        return;
    }
    writes_enabled = true;
    if (create_parents(UPLOAD_RESULT->path) || rename(temporary, UPLOAD_RESULT->path)) {
        upload_fail(-316);
        return;
    }
    /* The volume stays mounted for the next file; the host's final request closes it. */
    writes_enabled = false;
    UPLOAD_RESULT->state = 4;
}
