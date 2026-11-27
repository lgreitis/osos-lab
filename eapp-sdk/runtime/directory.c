/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/directory.c (8fc840d) by lgreitis. */

/* The host lists one folder at a time, so a recursive walk keeps a stack of
 * the host's lists, one per folder entered, and reads them depth first. */

#include "osos-2.0.5.h"
#include "platform.h"

#define DEPTH 8
#define NAME_BYTES sizeof(((struct eapp_directory_entry *)0)->name)

struct eapp_list {
    int handle;             /* -1 for a folder the host did not find. */
    uint32_t remaining;     /* Entries not read yet, from the listing's size. */
    unsigned prefix_length; /* The prefix to go back to when the list is done. */
};

struct eapp_directory {
    uint8_t active;
    uint8_t recursive;
    unsigned depth;
    struct eapp_list lists[DEPTH];
    /* The folder opened with a slash after it (nothing for the package root),
     * then the subfolders entered, each with a trailing slash. The part after
     * root_length is the prefix of an entry's name. */
    unsigned root_length;
    char path[2 * NAME_BYTES];
};

static struct eapp_directory directories[4];

static unsigned length_of(const char *text)
{
    unsigned length = 0;
    while (text[length])
        ++length;
    return length;
}

/* Lists the package folder `path` into the next stack level, which goes back
 * to a prefix of `prefix_length` when it is done; 0 or the host's status. A
 * missing folder is a level that reads nothing; a folder with more than 500
 * entries lists the first 500. */
static int push_list(struct eapp_directory *directory, const char *path,
                     unsigned prefix_length)
{
    struct GameFileRequest *request = eapp_request_new(GameFileRequest_ListDirectory);
    int status = eapp_request_finish(
        request, GameDirectory_QueueList(GameDirectory_Package, path, 0, request),
        EAPP_REQUEST_SIZE | EAPP_REQUEST_HANDLE);
    struct eapp_list *list = &directory->lists[directory->depth];
    if (status == FileStatus_Ok || status == FileStatus_ListTruncated) {
        list->handle = (int)request->handle;
        list->remaining = request->size;
    } else {
        if (request->handle != EAPP_REQUEST_PENDING)
            GameDirectory_ReleaseList((int)request->handle);
        if (status != FileStatus_NotFound)
            return status;
        list->handle = -1;
        list->remaining = 0;
    }
    list->prefix_length = prefix_length;
    ++directory->depth;
    return 0;
}

static void pop_list(struct eapp_directory *directory)
{
    struct eapp_list *list = &directory->lists[--directory->depth];
    if (list->handle >= 0)
        GameDirectory_ReleaseList(list->handle);
    directory->path[directory->root_length + list->prefix_length] = 0;
}

struct eapp_directory *eapp_directory_open(const char *name, int recursive)
{
    name = eapp_check_name(name, 1);
    if (!name || length_of(name) >= NAME_BYTES)
        return NULL;
    struct eapp_directory *directory = NULL;
    for (unsigned i = 0; i < 4 && !directory; ++i)
        if (!directories[i].active)
            directory = &directories[i];
    if (!directory)
        return NULL;

    directory->recursive = recursive != 0;
    directory->depth = 0;
    if (push_list(directory, name, 0) != 0)
        return NULL;
    directory->root_length = length_of(name);
    memcpy(directory->path, name, directory->root_length);
    if (directory->root_length)
        directory->path[directory->root_length++] = '/';
    directory->path[directory->root_length] = 0;
    directory->active = 1;
    return directory;
}

/* Enters the subfolder `name`, `length` long, of the current prefix: lists it
 * and appends it to the path. Silently stays outside when the host refuses or
 * the stack or the path is full. */
static void enter(struct eapp_directory *directory, const char *name, unsigned length)
{
    unsigned at = length_of(directory->path);
    if (directory->depth == DEPTH || at + length + 1 >= sizeof directory->path)
        return;
    memcpy(directory->path + at, name, length + 1);
    if (push_list(directory, directory->path, at - directory->root_length) != 0) {
        directory->path[at] = 0;
        return;
    }
    directory->path[at + length] = '/';
    directory->path[at + length + 1] = 0;
}

int eapp_directory_read(struct eapp_directory *directory,
                        struct eapp_directory_entry *entry)
{
    if (!directory || !directory->active || !entry)
        return -1;
    entry->name[0] = 0;
    entry->is_directory = 0;
    char name[NAME_BYTES];
    while (directory->depth) {
        struct eapp_list *list = &directory->lists[directory->depth - 1];
        if (!list->remaining) {
            pop_list(directory);
            continue;
        }
        --list->remaining;
        int status = GameDirectory_ReadEntry(list->handle, name, sizeof name, &entry->is_directory);
        if (status == GameDirectoryStatus_Last)
            list->remaining = 0;
        else if (status != GameDirectoryStatus_More)
            return -1;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;

        unsigned prefix = length_of(directory->path) - directory->root_length;
        unsigned length = length_of(name);
        if (prefix + length >= NAME_BYTES)
            return -1;
        memcpy(entry->name, directory->path + directory->root_length, prefix);
        memcpy(entry->name + prefix, name, length + 1);
        if (entry->is_directory && directory->recursive)
            enter(directory, name, length);
        return 1;
    }
    return 0;
}

void eapp_directory_close(struct eapp_directory *directory)
{
    if (!directory || !directory->active)
        return;
    while (directory->depth)
        pop_list(directory);
    directory->active = 0;
}

void eapp_directories_close_all(void)
{
    for (unsigned i = 0; i < 4; ++i)
        eapp_directory_close(&directories[i]);
}
