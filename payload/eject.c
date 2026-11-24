/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
#include "patch.h"

/* Flush the mounted filesystem as a writable file close does. Unmount waits
 * for every other volume user, including handles held by the reloaded library. */
static int flush_volume(unsigned volume)
{
    uint32_t access[4];
    ((void (*)(void *, unsigned))OSOS_VOLUME_ACCESS_INIT)(access, volume);
    void *fs = ((void *(*)(void *, int))OSOS_VOLUME_ACCESS_GET)(access, 1);
    int status = ((int (*)(void *))osos_method(fs, OSOS_VOLUME_FLUSH_SLOT))(fs);
    ((void (*)(void *))OSOS_VOLUME_ACCESS_DESTROY)(access);
    return status;
}

/* Only the disk-mode return caller is patched. Startup calls the same reload
 * routine directly and must never consume a reboot request. */
PATCH_ARM int cfw_disk_mode_reload(void *database)
{
    int result = ((int (*)(void *))OSOS_MEDIA_DATABASE_RELOAD)(database);
    struct osos_path path;
    osos_path_init(&path, "iPod_Control\\Device\\RepriseReboot1.req");
    void *file;
    if (osos_file_open(&path, 1, &file))
        return result;

    uint32_t size, value, read;
    int requested = !osos_file_size(file, &size) && size == sizeof(value) &&
                    !osos_file_read(file, &value, sizeof(value), &read) &&
                    read == sizeof(value) && value == 0x31524252; /* RBR1 */
    osos_file_close(file);
    if (requested && !osos_file_delete(&path) && !flush_volume(path.volume)) {
        ((void (*)(void))OSOS_PREPARE_RESET)();
        ((void (*)(void))OSOS_RESET_SOC_AND_WAIT)();
    }
    return result;
}
