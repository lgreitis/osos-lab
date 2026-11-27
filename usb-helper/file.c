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
static bool hfs, mounted, writes_enabled, closed, more;

/* HFS+ files staged since the last publish, read back after it. */
static struct {
    char path[192];
    uint32_t size;
    uint8_t sha256[32];
} batch[HFS_BATCH];
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
    if (hfs_upload_staged())
        upload_fail(-318);
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

/* Split the batch bit off the mode: only uploads take it. */
static bool take_mode(void)
{
    more = current.overwrite & UPLOAD_MORE;
    current.overwrite &= ~UPLOAD_MORE;
    return !more || current.overwrite <= 1;
}

static bool load_config(void)
{
    const volatile uint8_t *source = (const volatile uint8_t *)&upload_config;
    for (unsigned i = 0; i < sizeof(current); ++i)
        ((uint8_t *)&current)[i] = source[i];
    if (current_valid() && take_mode() && current.overwrite <= 3)
        return true;
    upload_fail(-302);
    return false;
}

#define REMOVE_DEPTH 8
static char tree[256];

/* An unfinished upload of any earlier session: /.reprise-<32 hex>.part. */
static bool staged_name(const char *name)
{
    size_t n = strlen(name);
    return n > 9 + 5 && !strncasecmp(name, ".reprise-", 9) &&
           !strcasecmp(name + n - 5, ".part");
}

/* The companion loader and the firmware image: the folder of later releases first,
   then the disk root of releases through 0.4.0. Index bit 0 tells the two files apart. */
static const char *const firmware_files[] = {
    "/Reprise/Firmware/loader.bin",
    "/Reprise/Firmware/reprise.bin",
    "/cfw-loader.bin",
    "/osos-cfw.bin",
};

static bool firmware_path(const char *path)
{
    for (unsigned i = 0; i < 4; ++i)
        if (!strcasecmp(path, firmware_files[i]))
            return true;
    return false;
}

/* Append the child of tree[length] after the first `skip` children to tree. Returns 1
   for a directory, 0 for a file, -1 when none is left and -2 on error. The directory
   is closed before the caller deletes anything. */
static int first_child(size_t length, bool staged, unsigned skip)
{
    DIR *dir = opendir(length ? tree : "/");
    if (!dir)
        return -2;
    int kind = -1;
    struct DIRENT *entry;
    while ((entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") || (staged && !staged_name(name)))
            continue;
        if (skip) {
            skip--;
            continue;
        }
        size_t n = strlen(name);
        if (length + 1 + n >= sizeof(tree)) {
            kind = -2;
            break;
        }
        tree[length] = '/';
        memcpy(tree + length + 1, name, n + 1);
        kind = dir_get_info(dir, entry).attribute & ATTR_DIRECTORY ? 1 : 0;
        break;
    }
    if (closedir(dir)) {
        tree[length] = 0;
        kind = -2;
    }
    return kind;
}

/* Deletes what it can below tree[length], skipping the firmware files and anything
   that refuses to go, so one stuck entry never stops the rest. The caller removes the
   directory itself. */
static void remove_tree(size_t length, unsigned depth)
{
    unsigned skip = 0;
    for (int kind; (kind = first_child(length, false, skip)) >= 0; tree[length] = 0) {
        if (kind == 1) {
            if (depth < REMOVE_DEPTH)
                remove_tree(strlen(tree), depth + 1);
            if (rmdir(tree))
                skip++;
        } else if (firmware_path(tree) || remove(tree)) {
            skip++;
        }
    }
}

/* The folders the installer created: /Reprise, and the extension folders releases
   through 0.4.0 kept under /Revola. Each goes with everything below it. */
static const char *const folders[] = {"/Reprise", "/Revola/Extensions", "/Revola/Data"};
/* The theme packs releases through 0.4.0 read. Their folders go once they are empty;
   /Revola and /themes likewise, so anything else there stays. */
static const char *const theme_packs[] = {"/themes/default/theme.bin",
                                          "/themes/dark/theme.bin"};
static const char *const parents[] = {
    "/Reprise/Firmware", "/Reprise",        "/Revola/Extensions", "/Revola/Data",
    "/Revola",           "/themes/default", "/themes/dark",       "/themes"};

/* existing_files bits 0-3 report what was deleted: companion, firmware, the folders and
   theme packs, staged uploads; bits 4-7 report the same four when something of them
   remains. Missing entries are neither. The firmware files go last: as long as they
   exist the RepriseOS bootloader still boots, and the host restores Apple's bootloader
   whatever remains. */
static void remove_installation(void)
{
    uint32_t *bits = &STORAGE_RESULT->existing_files;
    writes_enabled = true;
    bool had = false, left = false;
    for (unsigned i = 0; i < 3; ++i) {
        strcpy(tree, folders[i]);
        if (dir_exists(tree)) {
            had = true;
            remove_tree(strlen(tree), 0);
        }
    }
    for (unsigned i = 0; i < 2; ++i)
        if (file_exists(theme_packs[i])) {
            had = true;
            if (remove(theme_packs[i]))
                left = true;
        }
    for (unsigned i = 0; i < 4; ++i)
        if (file_exists(firmware_files[i]))
            *bits |= remove(firmware_files[i]) ? 16u << (i & 1) : 1u << (i & 1);
    for (unsigned i = 0; i < 8; ++i)
        if (dir_exists(parents[i]) && rmdir(parents[i]) && i < 4)
            left = true;
    if (had)
        *bits |= left ? 64 : 4;
    unsigned skip = 0;
    for (int kind; (kind = first_child(0, true, skip)) >= 0; tree[0] = 0) {
        if (kind != 0 || remove(tree)) {
            skip++;
            *bits |= 128;
        } else {
            *bits |= 8;
        }
    }
    writes_enabled = false;
}

/* The same report as remove_installation, in one catalog rebuild. HFS+
   uploads leave no unfinished files. */
static int remove_hfs_installation(void)
{
    uint32_t *bits = &STORAGE_RESULT->existing_files;
    struct hfs_removal set[9];
    for (unsigned i = 0; i < 3; ++i)
        set[i] = (struct hfs_removal){folders[i], true, 0};
    for (unsigned i = 0; i < 2; ++i)
        set[3 + i] = (struct hfs_removal){theme_packs[i], false, 0};
    for (unsigned i = 0; i < 4; ++i)
        set[5 + i] = (struct hfs_removal){firmware_files[i], false, 0};
    writes_enabled = true;
    int rc = hfs_upload_remove(set, 9);
    writes_enabled = false;
    if (rc)
        return rc;
    bool had = false, left = false;
    for (unsigned i = 0; i < 5; ++i) {
        had |= set[i].outcome != 0;
        left |= set[i].outcome == 1;
    }
    if (had)
        *bits |= left ? 64 : 4;
    for (unsigned i = 0; i < 4; ++i)
        if (set[5 + i].outcome)
            *bits |= set[5 + i].outcome == 2 ? 1u << (i & 1) : 16u << (i & 1);
    return 0;
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
    STORAGE_RESULT->start = data_start;
    STORAGE_RESULT->end = data_end;
    STORAGE_RESULT->sector_bytes = 512;
    STORAGE_RESULT->free_bytes = free_bytes;
    STORAGE_RESULT->existing_files = 0;
    if (current.overwrite == 2) {
        int companion = hfs_upload_exists("/Reprise/Firmware/loader.bin");
        int osos = hfs_upload_exists("/Reprise/Firmware/reprise.bin");
        if (companion < 0 || osos < 0 || free_bytes < current.size) {
            upload_fail(companion < 0 ? companion : osos < 0 ? osos : -322);
            return;
        }
        STORAGE_RESULT->existing_files = companion | (osos << 1);
    } else {
        rc = remove_hfs_installation();
        if (rc) {
            upload_fail(rc);
            return;
        }
    }
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
    /* Removal must work on a full disk. */
    if (!fat_size(IF_MV(0, ) NULL, &free_kib) ||
        (current.overwrite == 2 &&
         (uint64_t)free_kib * 1024 <
             (uint64_t)current.size + fat_get_cluster_size(IF_MV(0)))) {
        upload_fail(-322);
        return;
    }
    STORAGE_RESULT->start = start;
    STORAGE_RESULT->end = end;
    STORAGE_RESULT->free_bytes = (uint64_t)free_kib * 1024;
    STORAGE_RESULT->sector_bytes = bytes;
    STORAGE_RESULT->existing_files = 0;
    if (current.overwrite == 2)
        STORAGE_RESULT->existing_files =
            file_exists("/Reprise/Firmware/loader.bin") |
            (file_exists("/Reprise/Firmware/reprise.bin") << 1);
    else
        remove_installation();
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
    if (!current_valid() || !take_mode() || current.overwrite > 1) {
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

/* Read a file under its final name, the one the bootloader opens, and hash it
   again. Its data was verified before the rename or the HFS+ publish, so a
   mismatch here is a directory or allocation fault of the path. */
static int readback(const char *path, uint32_t expected, const uint8_t *sha256)
{
    uint32_t size;
    if (hfs ? hfs_upload_open(path, &size) != 0 : (fd = open(path, O_RDONLY)) < 0)
        return -1;
    if (!hfs)
        size = filesize(fd);
    if (size != expected)
        return -1;
    struct sha256 again;
    sha256_init(&again);
    for (unsigned pos = 0; pos < size;) {
        unsigned count = MIN(size - pos, UPLOAD_CHUNK);
        if (hfs ? hfs_upload_read(pos, UPLOAD_DATA, count) != 0
                : read(fd, UPLOAD_DATA, count) != (ssize_t)count)
            return -1;
        sha256_update(&again, UPLOAD_DATA, count);
        pos += count;
    }
    uint8_t digest[32];
    sha256_final(&again, digest);
    int rc = 0;
    if (!hfs) {
        rc = close(fd);
        fd = -1;
    }
    return rc || memcmp(digest, sha256, 32);
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
        unsigned staged = hfs_upload_staged();
        memcpy(batch[staged - 1].path, UPLOAD_RESULT->path, sizeof(batch[0].path));
        batch[staged - 1].size = current.size;
        memcpy(batch[staged - 1].sha256, UPLOAD_RESULT->source_sha256, 32);
        int rc = more ? 0 : hfs_upload_commit();
        writes_enabled = false;
        for (unsigned i = 0; !more && !rc && i < staged; ++i)
            if (readback(batch[i].path, batch[i].size, batch[i].sha256)) {
                memcpy(UPLOAD_RESULT->path, batch[i].path, sizeof(UPLOAD_RESULT->path));
                rc = -317;
            }
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
    if (readback(UPLOAD_RESULT->path, current.size, UPLOAD_RESULT->source_sha256)) {
        upload_fail(-317);
        return;
    }
    /* The volume stays mounted for the next file; the host's final request closes it. */
    writes_enabled = false;
    UPLOAD_RESULT->state = 4;
}
