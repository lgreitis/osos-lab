/* SPDX-License-Identifier: GPL-3.0-only */
#include "config.h"
#include "system.h"
#include "ata.h"
#include "string.h"
#include "hfs-upload.h"
#include "reader.h"

/* Only the primary volume header publishes a file. Until then, the new file,
 * catalog and allocation bitmap all occupy previously free blocks. */
#define NODE_LIMIT 32768
#define IO_BYTES 16384
#define NO_SPACE (-322)
#define UNSUPPORTED (-324)
#define BAD_IO (-325)
#define NOT_UNMOUNTED (-326)
#define CATALOG 272
#define BITMAP 112
#define ROOT_FOLDER 2
#define MAX_COMPONENTS 96

static struct hfs_volume vol;
static struct hfs_disk disk;
static uint8_t header[512], fresh_data[80], fresh_cat[80], fresh_map[80], replaced[80];
static uint8_t block_io[IO_BYTES] CACHEALIGN_ATTR;
static uint8_t page[IO_BYTES], old_node[NODE_LIMIT], out_node[NODE_LIMIT],
    child[NODE_LIMIT];
static uint32_t node_size, original_nodes, old_first, old_last, old_count;
static uint32_t map_pos, map_valid, alloc_cursor, used_blocks, freed_blocks;
static uint32_t out_id, first_at_level;
static unsigned out_count, out_end, height;
/* Coalesce consecutive catalog nodes before returning to the source fork on HDD. */
static uint8_t catalog_batch[256 * 1024];
static unsigned batch_bytes;
static uint32_t batch_start;

static unsigned u16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t u32(const uint8_t *p)
{
    return ((uint32_t)u16(p) << 16) | u16(p + 2);
}

static uint64_t u64(const uint8_t *p)
{
    return ((uint64_t)u32(p) << 32) | u32(p + 4);
}

static void w16(uint8_t *p, unsigned v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static void w32(uint8_t *p, uint32_t v)
{
    w16(p, v >> 16);
    w16(p + 2, v);
}

static void w64(uint8_t *p, uint64_t v)
{
    w32(p, v >> 32);
    w32(p + 4, v);
}

static bool transfer(uint64_t off, void *data, size_t len, bool writing)
{
    uint8_t *p = data;
    if (off > disk.bytes || len > disk.bytes - off)
        return false;
    if (writing && (off < vol.offset || off - vol.offset > vol.bytes ||
                    len > vol.bytes - (off - vol.offset)))
        return false;
    while (len) {
        unsigned skip = off & 511, n = MIN(len, IO_BYTES - skip);
        unsigned sectors = (skip + n + 511) / 512;
        if ((!writing || skip || ((skip + n) & 511)) &&
            ata_read_sectors(IF_MD(0, ) off >> 9, sectors, block_io))
            return false;
        if (writing) {
            memcpy(block_io + skip, p, n);
            if (ata_write_sectors(IF_MD(0, ) off >> 9, sectors, block_io))
                return false;
        } else {
            memcpy(p, block_io + skip, n);
        }
        p += n;
        off += n;
        len -= n;
    }
    return true;
}

static bool read_disk(void *ctx, uint64_t off, void *buf, size_t n)
{
    (void)ctx;
    return transfer(off, buf, n, false);
}

static bool write_disk(void *ctx, uint64_t off, void *buf, size_t n)
{
    (void)ctx;
    return transfer(off, buf, n, true);
}

static bool fork_io(const uint8_t *fork, uint64_t pos, void *data, unsigned len,
                    bool write)
{
    return hfs_fork_transfer(&vol, fork, pos, data, len,
                             write ? write_disk : read_disk) == HFS_OK;
}

static bool inline_fork(const uint8_t *fork)
{
    uint64_t blocks = 0;
    bool ended = false;
    for (int i = 0; i < 8; ++i) {
        uint32_t start = u32(fork + 16 + 8 * i), n = u32(fork + 20 + 8 * i);
        if (!n)
            ended = true;
        if (n && (ended || start >= vol.blocks || n > vol.blocks - start))
            return false;
        blocks += n;
    }
    return blocks == u32(fork + 12) && u64(fork) <= blocks * vol.block_size;
}

static bool journal_clean(void)
{
    if (!(u32(header + 4) & (1u << 13)))
        return true;
    uint32_t jib = u32(header + 12);
    uint8_t b[64];
    if (jib >= vol.blocks ||
        !read_disk(NULL, vol.offset + (uint64_t)jib * vol.block_size, b, 52) ||
        (u32(b) != 1 && u32(b) != 5))
        return false;
    uint32_t flags = u32(b);
    uint64_t off = u64(b + 36), n = u64(b + 44);
    if (n < 512 || off > vol.bytes || n > vol.bytes - off)
        return false;
    /* TN1150: kJIJournalNeedInitMask means there are no valid transactions. */
    if (flags & 4)
        return true;
    if (!read_disk(NULL, vol.offset + off, b, 48))
        return false;
    bool little = !memcmp(b, "xLNJ\x78\x56\x34\x12", 8);
    if ((!little && memcmp(b, "JNLx\x12\x34\x56\x78", 8)) || memcmp(b + 8, b + 16, 8))
        return false;
    uint8_t normalized[48];
    memcpy(normalized, b, sizeof(normalized));
    if (little) {
        for (unsigned i = 8; i < 44;) {
            unsigned width = i < 32 ? 8 : 4;
            for (unsigned j = 0; j < width; ++j)
                normalized[i + j] = b[i + width - 1 - j];
            i += width;
        }
    }
    uint64_t cursor = u64(normalized + 8);
    uint32_t header_size = u32(normalized + 40), checksum = 0;
    memset(b + 36, 0, 4);
    for (unsigned i = 0; i < 44; ++i)
        checksum = (checksum << 8) ^ (checksum + b[i]);
    return ~checksum == u32(normalized + 36) && u64(normalized + 24) == n &&
           header_size >= 512 && cursor >= header_size && cursor <= n;
}

static unsigned rec_off(const uint8_t *node, unsigned i)
{
    return u16(node + node_size - 2 * (i + 1));
}

static bool catalog_node(const uint8_t *fork, uint32_t id, uint8_t *buf)
{
    return fork_io(fork, (uint64_t)id * node_size, buf, node_size, false);
}

static bool catalog_header(void)
{
    uint8_t b[120];
    if (!fork_io(header + CATALOG, 0, b, sizeof(b), false))
        return false;
    node_size = u16(b + 32);
    original_nodes = u32(b + 36);
    old_first = u32(b + 24);
    old_last = u32(b + 28);
    old_count = u32(b + 20);
    return b[8] == 1 && u16(b + 10) == 3 && b[51] == 0xcf && (u32(b + 52) & 6) == 6 &&
           node_size >= 4096 && node_size <= NODE_LIMIT &&
           !(node_size & (node_size - 1)) && old_first && old_last &&
           old_first < original_nodes && old_last < original_nodes &&
           (uint64_t)original_nodes * node_size == u64(header + CATALOG);
}

int hfs_upload_mount(uint64_t sectors, uint64_t *start, uint64_t *end,
                     uint64_t *free_bytes)
{
    disk = (struct hfs_disk){read_disk, NULL, sectors * 512};
    enum hfs_result opened = hfs_open(&vol, &disk);
    if (opened == HFS_NOT_UNMOUNTED)
        return NOT_UNMOUNTED;
    if (opened != HFS_OK || vol.block_size > (1u << 30) || vol.bytes < 2048 ||
        vol.bytes - (uint64_t)vol.blocks * vol.block_size >= vol.block_size ||
        !read_disk(NULL, vol.offset + 1024, header, 512))
        return UNSUPPORTED;
    /* A zeroed alternate is what a publish cut short leaves. */
    uint8_t alt[512];
    if (!read_disk(NULL, vol.offset + vol.bytes - 1024, alt, 512) ||
        ((memcmp(alt, header, 4) || memcmp(alt + 40, header + 40, 8)) &&
         (alt[0] || memcmp(alt, alt + 1, sizeof(alt) - 1))) ||
        (u32(header + 4) & ((1u << 7) | (1u << 12) | (1u << 15) | (1u << 30))) ||
        !journal_clean() || u32(header + 48) > vol.blocks ||
        !inline_fork(header + CATALOG) || !inline_fork(header + BITMAP) ||
        u64(header + BITMAP) < ((uint64_t)vol.blocks + 7) / 8 || !catalog_header())
        return UNSUPPORTED;
    *start = vol.offset / 512;
    *end = *start + vol.bytes / 512;
    *free_bytes = (uint64_t)u32(header + 48) * vol.block_size;
    return 0;
}

struct item {
    uint32_t parent, id;
    char *name;
    bool folder, create;
};
static struct item path_items[MAX_COMPONENTS];
static char path_buf[192];
static unsigned path_count;
static uint32_t next_cnid;

typedef int (*leaf_visit)(const uint8_t *, unsigned, void *);

static int visit_leaves(leaf_visit visit, void *arg)
{
    uint32_t previous = 0, id = old_first, seen = 0;
    while (id) {
        if (id >= original_nodes || !catalog_node(header + CATALOG, id, old_node) ||
            old_node[8] != 255 || old_node[9] != 1 || u32(old_node + 4) != previous)
            return BAD_IO;
        unsigned n = u16(old_node + 10);
        if (!n || n > (node_size - 16) / 2 || n > old_count - seen)
            return UNSUPPORTED;
        for (unsigned i = 0; i < n; ++i) {
            unsigned a = rec_off(old_node, i), b = rec_off(old_node, i + 1);
            if (a < 14 || b < a + 10 || b > node_size - 2 * (n + 1))
                return UNSUPPORTED;
            const uint8_t *rec = old_node + a;
            if (u16(rec + 6) > 255 || u16(rec) != 6 + 2 * u16(rec + 6) ||
                (unsigned)u16(rec) + 4 > b - a)
                return UNSUPPORTED;
            int rc = visit(rec, b - a, arg);
            if (rc)
                return rc;
        }
        seen += n;
        previous = id;
        id = u32(old_node);
    }
    return seen == old_count && previous == old_last ? 0 : UNSUPPORTED;
}

/* A catalog lookup: 1 when found, 0 when missing, otherwise an error code. */
static int found(enum hfs_result rc)
{
    return rc == HFS_OK          ? 1
           : rc == HFS_NOT_FOUND ? 0
           : rc == HFS_IO        ? BAD_IO
                                 : UNSUPPORTED;
}

/* Walk an absolute path into path_items, the last component a file or, with
 * folder, a folder. 1 when it exists, with value at its record in old_node;
 * 0 when a component is missing or of the other kind. With create, missing
 * components get new IDs and are marked for creation, and a component of the
 * other kind is an error. */
static int walk_path(const char *path, bool create, bool folder, const uint8_t **value)
{
    size_t len = strlen(path);
    if (len < 2 || len >= sizeof(path_buf) || *path != '/')
        return UNSUPPORTED;
    memcpy(path_buf, path + 1, len);
    uint32_t parent = ROOT_FOLDER;
    path_count = 0;
    next_cnid = u32(header + 64);
    for (char *name = path_buf;;) {
        char *slash = strchr(name, '/');
        if (slash)
            *slash = 0;
        if (!*name || path_count == MAX_COMPONENTS)
            return UNSUPPORTED;
        const uint8_t *v = NULL;
        int rc = 0;
        /* Nothing exists below a folder that is still to be created. */
        if (!path_count || !path_items[path_count - 1].create)
            rc = found(hfs_find_record(&vol, header + CATALOG, parent, name,
                                       strlen(name), &v, old_node, sizeof(old_node)));
        if (rc < 0)
            return rc;
        unsigned type = rc ? u16(v) : 0;
        if (type && type != (slash || folder ? 1u : 2u))
            return create ? UNSUPPORTED : 0;
        if (!type && !create)
            return 0;
        if (!type && (next_cnid < 16 || next_cnid == UINT32_MAX))
            return UNSUPPORTED;
        struct item *p = &path_items[path_count++];
        *p = (struct item){parent, type ? u32(v + 8) : next_cnid++, name, slash != NULL,
                           !type};
        parent = p->id;
        if (!slash) {
            if (value)
                *value = v;
            return type ? 1 : 0;
        }
        name = slash + 1;
    }
}

/* 1 when path is a file, 0 when it is missing or not a file. */
int hfs_upload_exists(const char *path)
{
    return walk_path(path, false, false, NULL);
}

struct run {
    uint32_t start, blocks;
};

static void keep_run(struct run largest[8], struct run candidate)
{
    for (unsigned i = 0; i < 8 && candidate.blocks; ++i) {
        if (candidate.blocks > largest[i].blocks) {
            struct run previous = largest[i];
            largest[i] = candidate;
            candidate = previous;
        }
    }
}

static uint32_t reserved_until(const uint8_t *fork, uint32_t block)
{
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t start = u32(fork + 16 + i * 8), n = u32(fork + 20 + i * 8);
        if (block >= start && block - start < n)
            return start + n;
    }
    return block;
}

static int alloc_fork(uint8_t *fork, uint64_t bytes)
{
    uint64_t needed = (bytes + vol.block_size - 1) / vol.block_size;
    if (needed > vol.blocks || used_blocks + needed > u32(header + 48))
        return NO_SPACE;
    memset(fork, 0, 80);
    w64(fork, bytes);
    w32(fork + 12, needed);
    if (!needed)
        return 0;
    struct run largest[8] = {{0}}, run = {0};
    uint32_t first = (1536u + vol.block_size - 1) / vol.block_size;
    uint32_t end = vol.blocks - (1024u + vol.block_size - 1) / vol.block_size;
    for (uint32_t block = first; block < end;) {
        uint32_t byte = block / 8;
        if (!map_valid || byte < map_pos || byte - map_pos >= map_valid) {
            map_pos = byte;
            uint64_t left = u64(header + BITMAP) - byte;
            map_valid = MIN(left, sizeof(page));
            if (!fork_io(header + BITMAP, byte, page, map_valid, false))
                return BAD_IO;
        }
        unsigned bits = page[byte - map_pos];
        uint32_t next = block;
        if (bits & (128u >> (block & 7)))
            next = bits == 255 ? block + MIN(8 - (block & 7), end - block) : block + 1;
        if (fork != fresh_data)
            next = MAX(next, reserved_until(fresh_data, block));
        if (fork == fresh_map)
            next = MAX(next, reserved_until(fresh_cat, block));
        if (next != block) {
            keep_run(largest, run);
            run.blocks = 0;
            block = next;
            continue;
        }
        if (!run.blocks)
            run.start = block;
        ++block;
        if (++run.blocks == needed)
            break;
    }
    keep_run(largest, run);
    for (unsigned i = 0; i < 8 && needed; ++i) {
        uint32_t n = MIN(needed, largest[i].blocks);
        if (!n)
            return NO_SPACE;
        w32(fork + 16 + 8 * i, largest[i].start);
        w32(fork + 20 + 8 * i, n);
        alloc_cursor = largest[i].start + n;
        needed -= n;
        used_blocks += n;
    }
    return needed ? NO_SPACE : 0;
}

int hfs_upload_plan(const char *path, uint32_t size, bool overwrite)
{
    int rc = walk_path(path, true, false, NULL);
    if (rc < 0)
        return rc;
    if (!strcasecmp(path, "/.journal") || !strcasecmp(path, "/.journal_info_block"))
        return UNSUPPORTED;
    if (rc && !overwrite)
        return -304;
    map_valid = 0;
    alloc_cursor = 0;
    used_blocks = freed_blocks = 0;
    memset(replaced, 0, sizeof(replaced));
    if (!catalog_node(header + CATALOG, 0, child))
        return BAD_IO;
    uint32_t free_nodes = u32(child + 40);
    if (free_nodes >= original_nodes)
        return UNSUPPORTED;
    /* A dense new tree normally uses fewer nodes. Reserve growth room for
     * the new records, upper levels, and a subsequent native insertion. */
    uint64_t nodes = (uint64_t)(original_nodes - free_nodes) * 2 + 4 * path_count + 16;
    if (nodes > UINT32_MAX || nodes * node_size > vol.bytes)
        return NO_SPACE;
    rc = alloc_fork(fresh_data, size);
    uint64_t cat_bytes =
        (nodes * node_size + vol.block_size - 1) & ~((uint64_t)vol.block_size - 1);
    if (!rc)
        rc = alloc_fork(fresh_cat, cat_bytes);
    if (!rc)
        memcpy(fresh_cat + 8, header + CATALOG + 8, 4);
    if (!rc)
        rc = alloc_fork(fresh_map, u64(header + BITMAP));
    if (!rc)
        memcpy(fresh_map + 8, header + BITMAP + 8, 4);
    return rc;
}

int hfs_upload_data(uint32_t pos, void *data, unsigned len, bool write)
{
    return fork_io(fresh_data, pos, data, len, write) ? 0 : BAD_IO;
}

static void init_node(unsigned level)
{
    memset(out_node, 0, node_size);
    out_node[8] = level == 1 ? 255 : 0;
    out_node[9] = level;
    out_count = 0;
    out_end = 14;
}

static bool flush_node(bool last)
{
    w32(out_node, last ? 0 : out_id + 1);
    w32(out_node + 4, out_id == first_at_level ? 0 : out_id - 1);
    w16(out_node + 10, out_count);
    w16(out_node + node_size - 2 * (out_count + 1), out_end);
    if (!batch_bytes)
        batch_start = out_id;
    memcpy(catalog_batch + batch_bytes, out_node, node_size);
    batch_bytes += node_size;
    if (!last && batch_bytes < sizeof(catalog_batch))
        return true;
    bool ok = fork_io(fresh_cat, (uint64_t)batch_start * node_size, catalog_batch,
                      batch_bytes, true);
    batch_bytes = 0;
    return ok;
}

static bool append_record(const uint8_t *rec, unsigned size)
{
    if (size > node_size - 18)
        return false;
    if (out_end + size + 2 * (out_count + 2) > node_size) {
        if (!out_count || !flush_node(false))
            return false;
        ++out_id;
        init_node(height);
    }
    w16(out_node + node_size - 2 * (++out_count), out_end);
    memcpy(out_node + out_end, rec, size);
    out_end += size;
    return true;
}

struct addition {
    struct item *item;
    uint32_t parent;
    const char *name;
    bool thread;
};
static struct addition additions[2 * MAX_COMPONENTS];
static unsigned add_count, add_pos;
static uint32_t leaf_records, changed_parents, replaced_count;

static void queue_record(struct addition a)
{
    unsigned i = add_count++;
    while (i && (additions[i - 1].parent > a.parent ||
                 (additions[i - 1].parent == a.parent &&
                  strcasecmp(additions[i - 1].name, a.name) > 0))) {
        additions[i] = additions[i - 1];
        --i;
    }
    additions[i] = a;
}

static void put_name(uint8_t *p, const char *name)
{
    unsigned len = strlen(name);
    w16(p, len);
    for (unsigned i = 0; i < len; ++i)
        w16(p + 2 + 2 * i, (uint8_t)name[i]);
}

static bool emit_added(const struct addition *a)
{
    uint8_t rec[2 * 192 + 8 + 248] = {0};
    const struct item *p = a->item;
    unsigned key = 8 + 2 * strlen(a->name);
    w16(rec, key - 2);
    w32(rec + 2, a->parent);
    put_name(rec + 6, a->name);
    uint8_t *v = rec + key;
    unsigned n;
    if (a->thread) {
        w16(v, p->folder ? 3 : 4);
        w32(v + 4, p->parent);
        put_name(v + 8, p->name);
        n = 10 + 2 * strlen(p->name);
    } else {
        w16(v, p->folder ? 1 : 2);
        if (!p->folder)
            w16(v + 2, 2); /* file thread exists */
        w32(v + 8, p->id);
        for (int i = 12; i <= 24; i += 4)
            memcpy(v + i, header + 20, 4);
        w16(v + 42, p->folder ? 0040755 : 0100644);
        if (p->folder)
            w32(v + 4, 1); /* next path component */
        else
            memcpy(v + 88, fresh_data, 80);
        n = p->folder ? 88 : 248;
    }
    ++leaf_records;
    return append_record(rec, key + n);
}

static int copy_record(const uint8_t *rec, unsigned size, void *unused)
{
    (void)unused;
    while (add_pos < add_count) {
        struct addition *a = &additions[add_pos];
        int cmp = hfs_key_compare(rec, a->parent, a->name, strlen(a->name));
        if (!cmp)
            return UNSUPPORTED;
        if (cmp < 0)
            break;
        if (!emit_added(a))
            return BAD_IO;
        ++add_pos;
    }
    unsigned key = u16(rec) + 2;
    const uint8_t *v = rec + key;
    unsigned type = u16(v);
    uint8_t edited[8 + 510 + 248];
    if (type == 1 || type == 2) {
        if (size - key < (type == 1 ? 88u : 248u))
            return UNSUPPORTED;
        uint32_t id = u32(v + 8);
        for (unsigned i = 0; i < path_count; ++i) {
            const struct item *p = &path_items[i];
            if (type == 1 && p->create && p->parent == id) {
                if (size > sizeof(edited) || u32(v + 4) == UINT32_MAX)
                    return UNSUPPORTED;
                memcpy(edited, rec, size);
                w32(edited + key + 4, u32(v + 4) + 1);
                if (p->folder && (u16(v + 2) & 16))
                    w32(edited + key + 84, u32(v + 84) + 1);
                rec = edited;
                ++changed_parents;
                break;
            }
            if (type == 2 && !p->create && !p->folder && p->id == id) {
                if (size > sizeof(edited) || !inline_fork(v + 88) ||
                    ((u16(v + 42) & 0170000) && (u16(v + 42) & 0170000) != 0100000) ||
                    (u16(v + 2) & 1) || (v[40] & 6) || (v[41] & 0x26) ||
                    !memcmp(v + 48, "hlnkhfs+", 8))
                    return UNSUPPORTED;
                memcpy(replaced, v + 88, 80);
                memcpy(edited, rec, size);
                memcpy(edited + key + 88, fresh_data, 80);
                rec = edited;
                ++replaced_count;
                break;
            }
        }
    }
    ++leaf_records;
    return append_record(rec, size) ? 0 : BAD_IO;
}

static void fill_map(uint8_t *p, unsigned len, uint32_t base, uint32_t used)
{
    unsigned count = base >= used ? 0 : MIN(used - base, len * 8);
    memset(p, 0, len);
    memset(p, 255, count / 8);
    if (count & 7)
        p[count / 8] = (uint8_t)(255u << (8 - (count & 7)));
}

static bool write_tree_header(uint32_t last_leaf)
{
    if (!catalog_node(header + CATALOG, 0, child))
        return false;
    unsigned a = rec_off(child, 2), b = rec_off(child, 3);
    if (a < 120 || a >= b || b > node_size - 8)
        return false;
    uint32_t nodes = u64(fresh_cat) / node_size;
    uint32_t map_bits = (b - a) * 8, more_bits = (node_size - 20) * 8;
    uint32_t maps = nodes > map_bits ? 1 + (nodes - map_bits - 1) / more_bits : 0;
    uint32_t used = out_id + 1 + maps;
    if (used > nodes)
        return false;
    w32(child, maps ? out_id + 1 : 0);
    w16(child + 14, height);
    w32(child + 16, out_id);
    w32(child + 20, leaf_records);
    w32(child + 24, 1);
    w32(child + 28, last_leaf);
    w32(child + 36, nodes);
    w32(child + 40, nodes - used);
    fill_map(child + a, b - a, 0, used);
    if (!fork_io(fresh_cat, 0, child, node_size, true))
        return false;
    for (uint32_t m = 0; m < maps; ++m) {
        memset(child, 0, node_size);
        child[8] = 2;
        w16(child + 10, 1);
        w16(child + node_size - 2, 14);
        w16(child + node_size - 4, node_size - 6);
        if (m + 1 < maps)
            w32(child, out_id + m + 2);
        fill_map(child + 14, node_size - 20, map_bits + m * more_bits, used);
        if (!fork_io(fresh_cat, (uint64_t)(out_id + 1 + m) * node_size, child,
                     node_size, true))
            return false;
    }
    /* HFS Plus expects unused catalog nodes to contain zeroes. */
    memset(catalog_batch, 0, sizeof(catalog_batch));
    uint64_t pos = (uint64_t)used * node_size;
    while (pos < u64(fresh_cat)) {
        unsigned n = MIN(u64(fresh_cat) - pos, sizeof(catalog_batch));
        if (!fork_io(fresh_cat, pos, catalog_batch, n, true))
            return false;
        pos += n;
    }
    return true;
}

static int build_catalog(void)
{
    leaf_records = changed_parents = replaced_count = add_count = add_pos = 0;
    for (unsigned i = 0; i < path_count; ++i) {
        struct item *p = &path_items[i];
        if (p->create) {
            queue_record((struct addition){p, p->parent, p->name, false});
            queue_record((struct addition){p, p->id, "", true});
        }
    }
    batch_bytes = 0;
    out_id = first_at_level = height = 1;
    init_node(height);
    int rc = visit_leaves(copy_record, NULL);
    if (rc)
        return rc;
    while (add_pos < add_count)
        if (!emit_added(&additions[add_pos++]))
            return BAD_IO;
    if (changed_parents != (add_count ? 1u : 0u) ||
        replaced_count != (path_items[path_count - 1].create ? 0u : 1u) || !out_count ||
        !flush_node(true))
        return UNSUPPORTED;
    uint32_t last_leaf = out_id;
    while (out_id > first_at_level) {
        uint32_t first = first_at_level, last = out_id;
        first_at_level = ++out_id;
        init_node(++height);
        for (uint32_t id = first; id <= last; ++id) {
            if (!catalog_node(fresh_cat, id, child))
                return BAD_IO;
            unsigned n = 2 + u16(child + 14);
            if (n > 518 || n + 4 > node_size - 14)
                return UNSUPPORTED;
            w32(child + 14 + n, id);
            if (!append_record(child + 14, n + 4))
                return BAD_IO;
        }
        if (!flush_node(true))
            return BAD_IO;
    }
    return write_tree_header(last_leaf) ? 0 : BAD_IO;
}

static bool mark_extents(const uint8_t *fork, uint32_t byte, unsigned n, bool allocate)
{
    uint64_t first = (uint64_t)byte * 8, end = first + 8 * n;
    for (int i = 0; i < 8; ++i) {
        uint32_t start = u32(fork + 16 + i * 8), count = u32(fork + 20 + i * 8);
        uint64_t stop = MIN((uint64_t)start + count, end);
        for (uint64_t b = MAX((uint64_t)start, first); b < stop; ++b) {
            uint8_t *v = &page[(b - first) / 8], bit = 128u >> (b & 7);
            if (!!(*v & bit) == allocate)
                return false;
            *v ^= bit;
            if (!allocate)
                ++freed_blocks;
        }
    }
    return true;
}

static bool build_bitmap(void)
{
    uint64_t size = u64(fresh_map);
    for (uint64_t pos = 0; pos < size; pos += sizeof(page)) {
        unsigned n = MIN(size - pos, sizeof(page));
        if (!fork_io(header + BITMAP, pos, page, n, false) ||
            !mark_extents(fresh_data, pos, n, true) ||
            !mark_extents(fresh_cat, pos, n, true) ||
            !mark_extents(fresh_map, pos, n, true) ||
            !mark_extents(header + BITMAP, pos, n, false) ||
            !mark_extents(header + CATALOG, pos, n, false) ||
            !mark_extents(replaced, pos, n, false) ||
            !fork_io(fresh_map, pos, page, n, true))
            return false;
    }
    return true;
}

int hfs_upload_commit(void)
{
    int rc = build_catalog();
    if (rc)
        return rc;
    if (!build_bitmap() || ata_flush())
        return BAD_IO;
    w32(header + 48, u32(header + 48) - used_blocks + freed_blocks);
    w32(header + 52, alloc_cursor == vol.blocks ? 0 : alloc_cursor);
    w32(header + 64, next_cnid);
    w32(header + 68, u32(header + 68) + 1);
    for (unsigned i = 0; i < path_count; ++i) {
        if (path_items[i].create) {
            uint8_t *count = header + (path_items[i].folder ? 36 : 32);
            w32(count, u32(count) + 1);
        }
    }
    memcpy(header + BITMAP, fresh_map, 80);
    memcpy(header + CATALOG, fresh_cat, 80);
    /* fsck_hfs finds the B-trees through the alternate volume header, and
     * through the primary one when the alternate is invalid. Zeroing the
     * alternate before the primary is written keeps every interrupted publish
     * repairable to what the primary header describes. */
    uint8_t check[512] = {0};
    if (!transfer(vol.offset + vol.bytes - 1024, check, 512, true) || ata_flush() ||
        !transfer(vol.offset + 1024, header, 512, true) || ata_flush() ||
        !transfer(vol.offset + vol.bytes - 1024, header, 512, true) || ata_flush())
        return BAD_IO;
    if (!read_disk(NULL, vol.offset + 1024, check, sizeof(check)) ||
        memcmp(check, header, 512) || !catalog_header())
        return BAD_IO;
    return 0;
}

static uint8_t readback_fork[80];

int hfs_upload_open(const char *path, uint32_t *size)
{
    const uint8_t *v;
    if (walk_path(path, false, false, &v) != 1)
        return BAD_IO;
    memcpy(readback_fork, v + 88, 80);
    if (u32(readback_fork))
        return BAD_IO;
    *size = u32(readback_fork + 4);
    return 0;
}

int hfs_upload_read(uint32_t pos, void *data, unsigned len)
{
    return fork_io(readback_fork, pos, data, len, false) ? 0 : BAD_IO;
}
