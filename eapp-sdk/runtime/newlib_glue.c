/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/newlib_glue.c (8fc840d) by lgreitis. */

#include "eapp.h"
#include "eapp_libc.h"

#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>

static struct eapp_file *descriptors[12];

void *_malloc_r(struct _reent *r, size_t size)
{
    void *p = eapp_alloc(size);
    if (!p)
        r->_errno = ENOMEM;
    return p;
}

void _free_r(struct _reent *r, void *p)
{
    (void)r;
    eapp_free(p);
}

void *_realloc_r(struct _reent *r, void *p, size_t size)
{
    void *q = eapp_realloc(p, size);
    if (!q && size)
        r->_errno = ENOMEM;
    return q;
}

void *_calloc_r(struct _reent *r, size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) {
        r->_errno = ENOMEM;
        return NULL;
    }

    void *p = _malloc_r(r, count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

static struct eapp_file *file_for(int fd)
{
    if (fd < 3 || fd >= 15 || !descriptors[fd - 3]) {
        errno = EBADF;
        return NULL;
    }
    return descriptors[fd - 3];
}

int _open(const char *path, int flags, ...)
{
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) {
        errno = EROFS;
        return -1;
    }

    for (unsigned i = 0; i < 12; ++i) {
        if (descriptors[i])
            continue;
        descriptors[i] = eapp_file_open(path);
        if (!descriptors[i]) {
            errno = ENOENT;
            return -1;
        }
        return (int)i + 3;
    }

    errno = EMFILE;
    return -1;
}

int _close(int fd)
{
    if (fd >= 0 && fd < 3)
        return 0;

    struct eapp_file *file = file_for(fd);
    if (!file)
        return -1;

    eapp_file_close(file);
    descriptors[fd - 3] = NULL;
    return 0;
}

_ssize_t _read(int fd, void *buffer, size_t size)
{
    struct eapp_file *file = file_for(fd);
    if (!file)
        return -1;

    int result = eapp_file_read(file, buffer, size);
    if (result < 0)
        errno = EIO;
    return result;
}

_ssize_t _write(int fd, const void *buffer, size_t size)
{
    if (fd == 1 || fd == 2) {
        eapp_libc_write(buffer, size);
        return size;
    }
    errno = EROFS;
    return -1;
}

_off_t _lseek(int fd, _off_t offset, int whence)
{
    struct eapp_file *file = file_for(fd);
    if (!file)
        return -1;

    int64_t target = offset;
    if (whence == SEEK_CUR)
        target += eapp_file_position(file);
    else if (whence == SEEK_END)
        target += eapp_file_size(file);
    else if (whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }

    if (target < 0 || target > INT32_MAX || eapp_file_seek(file, target) != 0) {
        errno = EINVAL;
        return -1;
    }
    return target;
}

int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    if (fd >= 0 && fd < 3) {
        st->st_mode = S_IFCHR;
        return 0;
    }

    struct eapp_file *file = file_for(fd);
    if (!file)
        return -1;

    st->st_mode = S_IFREG | 0444;
    st->st_size = eapp_file_size(file);
    st->st_blksize = 4096;
    return 0;
}

int _stat(const char *path, struct stat *st)
{
    int fd = _open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    int result = _fstat(fd, st);
    _close(fd);
    return result;
}

int _isatty(int fd)
{
    return fd >= 0 && fd < 3;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int signal)
{
    (void)pid;
    (void)signal;
    errno = ENOSYS;
    return -1;
}

int _unlink(const char *path)
{
    (void)path;
    errno = EROFS;
    return -1;
}

int _link(const char *a, const char *b)
{
    (void)a;
    (void)b;
    errno = EROFS;
    return -1;
}

int mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = EROFS;
    return -1;
}

int system(const char *command)
{
    (void)command;
    errno = ENOSYS;
    return -1;
}

void *_sbrk(ptrdiff_t size)
{
    (void)size;
    errno = ENOMEM;
    return (void *)-1;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    uint32_t ms = eapp_ticks_ms();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

clock_t _times(struct tms *t)
{
    memset(t, 0, sizeof(*t));
    return (clock_t)eapp_ticks_ms();
}

_Noreturn void _exit(int status)
{
    eapp_libc_exit(status);
}

_Noreturn void exit(int status)
{
    eapp_libc_exit(status);
}
