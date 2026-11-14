/* SPDX-License-Identifier: GPL-3.0-only */
/* In-memory volume for preferences.c. Include after struct osos_path is defined. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

struct fake_file {
    char name[64];
    unsigned char data[1024];
    uint32_t size;
    int exists;
};

static struct fake_file fake_files[8];
static unsigned fake_writes, fake_deletes;
static int fake_fail_write;

static struct fake_file *fake_find(const char *name, int create)
{
    struct fake_file *free_slot = NULL;
    for (unsigned i = 0; i < sizeof(fake_files) / sizeof(fake_files[0]); i++) {
        if (fake_files[i].exists && !strcmp(fake_files[i].name, name))
            return &fake_files[i];
        if (!fake_files[i].exists && !free_slot)
            free_slot = &fake_files[i];
    }
    if (!create)
        return NULL;
    assert(free_slot && strlen(name) < sizeof(free_slot->name));
    memset(free_slot, 0, sizeof(*free_slot));
    strcpy(free_slot->name, name);
    free_slot->exists = 1;
    return free_slot;
}

static void fake_put(const char *name, const void *data, uint32_t size)
{
    struct fake_file *file = fake_find(name, 1);
    assert(size <= sizeof(file->data));
    memcpy(file->data, data, size);
    file->size = size;
}

static int fake_open(const struct osos_path *path, uint32_t mode, void **handle)
{
    assert(path->volume == 0 && (mode == 1 || mode == 2));
    struct fake_file *file = fake_find(path->text, mode == 2);
    if (!file)
        return -43;
    *handle = file;
    return 0;
}

static int fake_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    struct fake_file *file = handle;
    *count = file->size < size ? file->size : size;
    memcpy(out, file->data, *count);
    return 0;
}

/* Like the native writer this does not truncate: a shorter write keeps the tail. */
static int fake_write(void *handle, const void *data, uint32_t size, uint32_t *count)
{
    struct fake_file *file = handle;
    fake_writes++;
    if (fake_fail_write)
        return -1;
    assert(size <= sizeof(file->data));
    memcpy(file->data, data, size);
    if (size > file->size)
        file->size = size;
    *count = size;
    return 0;
}

static void fake_close(void *handle)
{
    assert(handle);
}

static int fake_delete(const struct osos_path *path)
{
    struct fake_file *file = fake_find(path->text, 0);
    fake_deletes++;
    if (!file)
        return -43;
    file->exists = 0;
    return 0;
}

#define osos_file_open fake_open
#define osos_file_read fake_read
#define osos_file_write fake_write
#define osos_file_close fake_close
#define osos_file_delete fake_delete
