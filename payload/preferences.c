/* SPDX-License-Identifier: GPL-3.0-only */
#include "preferences.h"
#include "osos.h"

/* Paths are built on the stack: a constant osos_path costs its full text buffer. */
static void set_path(struct osos_path *path, const char *name)
{
    uint32_t i = 0;
    path->volume = 0;
    for (; name[i] && i < sizeof(path->text) - 1; i++)
        path->text[i] = name[i];
    path->text[i] = 0;
}

static int transfer(const char *name, uint32_t mode, void *record, uint32_t size)
{
    struct osos_path path;
    void *handle;
    uint32_t count = 0;
    set_path(&path, name);
    if (osos_file_open(&path, mode, &handle) != 0)
        return 0;
    int result = mode == 1 ? osos_file_read(handle, record, size, &count)
                           : osos_file_write(handle, record, size, &count);
    osos_file_close(handle);
    return !result && count == size;
}

int cfw_preferences_read(const char *name, void *record, uint32_t size)
{
    return transfer(name, 1, record, size);
}

int cfw_preferences_write(const char *name, const void *record, uint32_t size)
{
    return transfer(name, 2, (void *)record, size);
}

void cfw_preferences_delete(const char *name)
{
    struct osos_path path;
    set_path(&path, name);
    osos_file_delete(&path);
}
