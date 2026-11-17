/* SPDX-License-Identifier: GPL-3.0-only */
#include "preferences.h"
#include "osos.h"
#include "native/path.h"

enum { FILE_NOT_FOUND = -43, FILE_EOF = -39 };

int cfw_preferences_read_status(const char *name, void *record, uint32_t size)
{
    struct osos_path path;
    void *handle;
    uint32_t count = 0;
    osos_path_init(&path, name);
    int result = osos_file_open(&path, 1, &handle);
    if (result)
        return result == FILE_NOT_FOUND ? CFW_PREFERENCES_MISSING
                                        : CFW_PREFERENCES_IO_ERROR;
    result = osos_file_read(handle, record, size, &count);
    osos_file_close(handle);
    if (result == FILE_EOF)
        return CFW_PREFERENCES_SHORT;
    return result          ? CFW_PREFERENCES_IO_ERROR
           : count == size ? CFW_PREFERENCES_OK
                           : CFW_PREFERENCES_SHORT;
}

int cfw_preferences_write(const char *name, const void *record, uint32_t size)
{
    struct osos_path path;
    void *handle;
    uint32_t count = 0;
    osos_path_init(&path, name);
    if (osos_file_open(&path, 2, &handle))
        return 0;
    int result = osos_file_write(handle, record, size, &count);
    osos_file_close(handle);
    return !result && count == size;
}

void cfw_preferences_delete(const char *name)
{
    struct osos_path path;
    osos_path_init(&path, name);
    osos_file_delete(&path);
}
