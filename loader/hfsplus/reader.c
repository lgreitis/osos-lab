/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "reader.h"
#include <string.h>

static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return (be16(p) << 16) | be16(p + 2);
}

static enum hfs_result find_partition(struct hfs_volume *v, uint64_t *region_bytes)
{
    uint8_t p[80];
    const struct hfs_disk *d = v->disk;
    if (*region_bytes < sizeof(p))
        return HFS_INVALID;
    if (!d->read(d->context, v->offset, p, sizeof(p)))
        return HFS_IO;
    if (memcmp(p, "ER", 2))
        return HFS_NOT_FOUND;
    uint32_t unit = be16(p + 2);
    if (unit != 512 && unit != 4096)
        return HFS_UNSUPPORTED;
    uint64_t units = be32(p + 4), count = 1, partition_bytes = 0, start_byte = 0;
    if (units < 2 || units > *region_bytes / unit)
        return HFS_INVALID;
    for (uint64_t i = 1; i <= count; ++i) {
        if (i >= units)
            return HFS_INVALID;
        if (!d->read(d->context, v->offset + i * unit, p, sizeof(p)))
            return HFS_IO;
        if (memcmp(p, "PM", 2))
            return HFS_INVALID;
        if (i == 1)
            count = be32(p + 4);
        if (!count || count >= units || count != be32(p + 4))
            return HFS_INVALID;
        if (count > 256)
            return HFS_UNSUPPORTED;
        if (memcmp(p + 48, "Apple_HFS", sizeof("Apple_HFS")))
            continue;
        uint64_t start = be32(p + 8), blocks = be32(p + 12);
        if (partition_bytes || start <= count || start >= units || !blocks ||
            blocks > units - start)
            return HFS_INVALID;
        start_byte = start * unit;
        partition_bytes = blocks * unit;
    }
    if (!partition_bytes)
        return HFS_NOT_FOUND;
    for (uint64_t i = 1; i <= count; ++i) {
        if (!d->read(d->context, v->offset + i * unit, p, sizeof(p)))
            return HFS_IO;
        if (!memcmp(p + 48, "Apple_HFS", sizeof("Apple_HFS")))
            continue;
        uint64_t first = be32(p + 8), n = be32(p + 12);
        if (first >= units || n > units - first ||
            (n && first * unit < start_byte + partition_bytes &&
             (first + n) * unit > start_byte))
            return HFS_INVALID;
    }
    v->offset += start_byte;
    *region_bytes = partition_bytes;
    return HFS_OK;
}

static uint32_t le32_at(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* On Classics the USB-visible map starts at the end of the hidden firmware
 * partition. Accept a unique APM only on this two-entry iPod MBR. Apple's
 * firmware takes the first entry of type 0 or 0x3f as the firmware partition
 * and finds the data volume through the map, whatever the data entry's type. */
static enum hfs_result find_usb_map(struct hfs_volume *v, uint64_t *region)
{
    uint8_t mbr[512];
    const struct hfs_disk *d = v->disk;
    if (!d->read(d->context, 0, mbr, sizeof(mbr)))
        return HFS_IO;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa)
        return HFS_NOT_FOUND;
    uint64_t fw_end = 0, data_start = 0, data_end = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = mbr + 446 + 16 * i;
        uint32_t start = le32_at(p + 8), count = le32_at(p + 12);
        if (!p[4] && !start && !count)
            continue;
        if ((p[0] && p[0] != 0x80) || !start || !count)
            return HFS_NOT_FOUND;
        if ((p[4] == 0 || p[4] == 0x3f) && !fw_end)
            fw_end = (uint64_t)start + count;
        else if (!data_start) {
            data_start = start;
            data_end = (uint64_t)start + count;
        } else
            return HFS_NOT_FOUND;
    }
    if (!fw_end || fw_end > data_start)
        return HFS_NOT_FOUND;
    bool found = false;
    for (unsigned unit = 512; unit <= 4096; unit *= 2) {
        if (data_end > d->bytes / unit)
            continue;
        struct hfs_volume candidate = {.disk = d, .offset = fw_end * unit};
        uint64_t bytes = d->bytes - candidate.offset;
        enum hfs_result rc = find_partition(&candidate, &bytes);
        if (rc == HFS_NOT_FOUND)
            continue;
        if (rc != HFS_OK)
            return rc;
        if (found)
            return HFS_INVALID;
        *v = candidate;
        *region = bytes;
        found = true;
    }
    return found ? HFS_OK : HFS_NOT_FOUND;
}

enum hfs_result hfs_open(struct hfs_volume *v, const struct hfs_disk *d)
{
    *v = (struct hfs_volume){.disk = d};
    uint64_t partition_bytes = d->bytes;
    enum hfs_result rc = find_partition(v, &partition_bytes);
    if (rc == HFS_NOT_FOUND)
        rc = find_usb_map(v, &partition_bytes);
    if (rc != HFS_OK)
        return rc;
    uint8_t signature[2];
    if (!d->read(d->context, v->offset, signature, sizeof(signature)))
        return HFS_IO;
    if (!memcmp(signature, "ER", 2)) {
        rc = find_partition(v, &partition_bytes);
        if (rc != HFS_OK)
            return rc;
    }
    uint8_t header[512];
    if (partition_bytes < 1536)
        return HFS_INVALID;
    if (!d->read(d->context, v->offset + 1024, header, sizeof(header)))
        return HFS_IO;
    if (memcmp(header, "H+", 2) || be16(header + 2) != 4)
        return HFS_UNSUPPORTED;
    uint32_t attributes = be32(header + 4);
    if (attributes & ((1u << 11) | (1u << 14)))
        return HFS_UNSUPPORTED;
    v->block_size = be32(header + 40);
    v->bytes = partition_bytes;
    v->blocks = be32(header + 44);
    if (v->block_size < 512 || (v->block_size & (v->block_size - 1)) || !v->blocks ||
        (uint64_t)v->blocks * v->block_size > partition_bytes)
        return HFS_INVALID;
    return attributes & (1u << 8) ? HFS_OK : HFS_NOT_UNMOUNTED;
}

/* HFSPlusForkData embeds eight extents. Never infer contiguity beyond them. */
enum hfs_result hfs_fork_transfer(const struct hfs_volume *v, const uint8_t *fork,
                                  uint64_t pos, uint8_t *out, size_t n, hfs_io transfer)
{
    uint64_t size = ((uint64_t)be32(fork) << 32) | be32(fork + 4);
    if (pos > size || n > size - pos)
        return HFS_INVALID;
    for (unsigned i = 0; i < 8 && n; ++i) {
        const uint8_t *extent = fork + 16 + 8 * i;
        uint32_t start = be32(extent), blocks = be32(extent + 4);
        if (!blocks || start >= v->blocks || blocks > v->blocks - start)
            return HFS_INVALID;
        uint64_t bytes = (uint64_t)blocks * v->block_size;
        if (pos >= bytes) {
            pos -= bytes;
            continue;
        }
        size_t chunk = bytes - pos < n ? (size_t)(bytes - pos) : n;
        uint64_t offset = v->offset + (uint64_t)start * v->block_size + pos;
        if (!transfer(v->disk->context, offset, out, chunk))
            return HFS_IO;
        n -= chunk;
        out += chunk;
        pos = 0;
    }
    return n ? HFS_UNSUPPORTED : HFS_OK;
}

/* Path components are printable ASCII. Only folds whose result is ASCII or
 * ignorable can affect their ordering against UTF-16 catalog keys (HFS Plus
 * FastUnicodeCompare, TN1150). */
static unsigned folded(unsigned c)
{
    if (!c)
        return 0xffff;
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if ((c >= 0x200c && c <= 0x200f) || (c >= 0x202a && c <= 0x202e) ||
        (c >= 0x206a && c <= 0x206f) || c == 0xfeff)
        return 0;
    return c;
}

int hfs_key_compare(const uint8_t *key, uint32_t parent, const char *name,
                    size_t length)
{
    uint32_t id = be32(key + 2);
    if (id != parent)
        return id < parent ? -1 : 1;
    unsigned count = be16(key + 6);
    for (unsigned i = 0; i < count; ++i) {
        unsigned c = folded(be16(key + 8 + 2 * i));
        if (!c)
            continue;
        unsigned target = length ? folded((uint8_t)*name) : 0;
        if (!length || c != target)
            return !length || c > target ? 1 : -1;
        ++name;
        --length;
    }
    return length ? -1 : 0;
}

/* The walk descends from the root through the index nodes, following the last
 * record whose key is not larger. */
enum hfs_result hfs_find_record(const struct hfs_volume *v, const uint8_t *catalog,
                                uint32_t parent, const char *name, size_t length,
                                const uint8_t **record, uint8_t *node,
                                size_t scratch_bytes)
{
    uint8_t header[40];
    enum hfs_result rc =
        hfs_fork_transfer(v, catalog, 0, header, sizeof(header), v->disk->read);
    if (rc != HFS_OK)
        return rc;
    if (header[8] != 1)
        return HFS_INVALID;
    uint32_t id = be32(header + 16), depth = be16(header + 14);
    uint32_t node_bytes = be16(header + 32), nodes = be32(header + 36);
    if (node_bytes < 4096 || (node_bytes & (node_bytes - 1)))
        return HFS_INVALID;
    if (node_bytes > scratch_bytes)
        return HFS_UNSUPPORTED;
    if (!id)
        return HFS_NOT_FOUND;
    for (uint32_t level = depth; level; --level) {
        if (id >= nodes)
            return HFS_INVALID;
        rc = hfs_fork_transfer(v, catalog, (uint64_t)id * node_bytes, node, node_bytes,
                               v->disk->read);
        if (rc != HFS_OK)
            return rc;
        bool leaf = level == 1;
        if ((int8_t)node[8] != (leaf ? -1 : 0) || node[9] != level)
            return HFS_INVALID;
        uint32_t records = be16(node + 10), next = 0;
        if (!records || 2 * (records + 1) > node_bytes - 14)
            return HFS_INVALID;
        for (uint32_t i = 0; i < records; ++i) {
            uint32_t first = be16(node + node_bytes - 2 * (i + 1));
            uint32_t end = be16(node + node_bytes - 2 * (i + 2));
            if (first < 14 || end < first + 10 || end > node_bytes - 2 * (records + 1))
                return HFS_INVALID;
            const uint8_t *key = node + first;
            uint32_t chars = be16(key + 6);
            uint32_t data = first + 2 + be16(key);
            if (be16(key) < 6 + 2 * chars || data + (leaf ? 2 : 4) > end)
                return HFS_INVALID;
            int order = hfs_key_compare(key, parent, name, length);
            if (!leaf) {
                if (order > 0 && i)
                    break;
                next = be32(node + data);
                continue;
            }
            if (order > 0)
                return HFS_NOT_FOUND;
            if (order < 0)
                continue;
            unsigned type = be16(node + data);
            if ((type != 1 && type != 2) || end - data < (type == 1 ? 88u : 248u))
                return HFS_INVALID;
            *record = node + data;
            return HFS_OK;
        }
        if (leaf)
            return HFS_NOT_FOUND;
        id = next;
    }
    return HFS_INVALID;
}

enum hfs_result hfs_find_file(const struct hfs_volume *v, const char *path,
                              uint8_t fork[80], void *scratch, size_t scratch_bytes)
{
    uint8_t catalog[80];
    const uint8_t *record;
    if (*path != '/')
        return HFS_INVALID;
    if (!v->disk->read(v->disk->context, v->offset + 1024 + 272, catalog,
                       sizeof(catalog)))
        return HFS_IO;
    uint32_t parent = 2;
    for (const char *name = path + 1;;) {
        const char *slash = strchr(name, '/');
        size_t length = slash ? (size_t)(slash - name) : strlen(name);
        if (!length)
            return HFS_INVALID;
        enum hfs_result rc = hfs_find_record(v, catalog, parent, name, length, &record,
                                             scratch, scratch_bytes);
        if (rc != HFS_OK)
            return rc;
        if (be16(record) != (slash ? 1 : 2))
            return HFS_NOT_FOUND;
        if (!slash) {
            memcpy(fork, record + 88, 80);
            return HFS_OK;
        }
        parent = be32(record + 8);
        name = slash + 1;
    }
}

enum hfs_result hfs_read_path(const struct hfs_disk *disk, const char *path, void *dst,
                              size_t capacity, size_t *length, void *scratch,
                              size_t scratch_bytes)
{
    struct hfs_volume v;
    uint8_t fork[80];
    *length = 0;
    enum hfs_result rc = hfs_open(&v, disk);
    if (rc == HFS_OK || rc == HFS_NOT_UNMOUNTED)
        rc = hfs_find_file(&v, path, fork, scratch, scratch_bytes);
    if (rc != HFS_OK)
        return rc;
    uint32_t size = be32(fork + 4);
    if (be32(fork) || size > capacity)
        return HFS_TOO_LARGE;
    rc = hfs_fork_transfer(&v, fork, 0, dst, size, disk->read);
    if (rc == HFS_OK)
        *length = size;
    return rc;
}
