#ifndef CFW_OSOS_H
#define CFW_OSOS_H

/* Classic 7G FW2.0.4 runtime addresses. Function entries below use ARM state. */
#define OSOS_RESOURCE_BANK_INIT_OVERRIDES 0x08111d0c

#ifndef __ASSEMBLER__
#include <stdint.h>

uint32_t cfw_irq_save(void);
void cfw_irq_restore(uint32_t flags);

/* Apple's libstdc++ string stores one pointer and owns its backing storage. */
struct osos_string {
    char *data;
};

static inline void osos_string_init(struct osos_string *string, const char *text)
{
    typedef void (*fn)(struct osos_string *, const char *);
    ((fn)0x083cdc84)(string, text);
}

static inline void osos_string_destroy(struct osos_string *string)
{
    typedef void (*fn)(struct osos_string *);
    ((fn)0x083cdc2c)(string);
}

/* Find or insert a name in the global resource-name map and return its ID slot. */
static inline uint32_t *osos_resource_name_slot(const struct osos_string *name)
{
    typedef uint32_t *(*fn)(void *, const struct osos_string *);
    return ((fn)0x083d0884)((void *)0x08ad7c6c, name);
}

/* Copy resource data into the bank's override map. */
static inline void osos_resource_set(void *bank, uint32_t type, uint32_t id,
                                     const void *data, uint32_t size)
{
    typedef void (*fn)(void *, uint32_t, uint32_t, const void *, uint32_t);
    ((fn)0x08111c2c)(bank, type, id, data, size);
}

/* Advance an active resource enumeration; ROM banks enumerate the stock index. */
static inline const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{
    typedef const void *(*fn)(void *, uint32_t *, uint32_t *);
    return ((fn)0x08111e0c)(bank, id, size);
}

static inline void *osos_resource_bank(void)
{
    typedef void *(*fn)(void);
    return ((fn)0x08194ed8)();
}

static inline void *osos_resource_get(void *bank, uint32_t type, uint32_t id,
                                      uint32_t *size)
{
    typedef void *(*fn)(void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x08111b80)(bank, type, id, size);
}

/* Notify native list providers that the item's displayed content changed. */
static inline void osos_menu_item_changed(uint32_t item)
{
    typedef void *(*service_fn)(void);
    typedef void (*notify_fn)(void *, uint32_t, uint32_t);
    void *service = ((service_fn)0x08267464)();
    uintptr_t *vtable = *(uintptr_t **)service;
    ((notify_fn)vtable[0x58 / 4])(service, 0x4e6f6e65, item); /* None */
}

static inline int osos_settings_action(void *controller, const char *action,
                                       uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)0x0821c690)(controller, action, argument);
}

static inline void osos_eq_load(void *state)
{
    typedef void (*fn)(void *);
    ((fn)0x08271bf0)(state);
}

static inline void osos_eq_reset(void *state)
{
    typedef void (*fn)(void *);
    ((fn)0x08271d60)(state);
}

static inline void osos_eq_process(void *state, int16_t *samples, uint32_t frames,
                                   int16_t **output, uint32_t *output_frames)
{
    typedef void (*fn)(void *, int16_t *, uint32_t, int16_t **, uint32_t *);
    ((fn)0x08271f2c)(state, samples, frames, output, output_frames);
}

struct osos_path {
    uint16_t volume;
    char text[258];
};

/* Native file modes: 1 reads; 2 opens or creates for writing. */
static inline int osos_file_open(const struct osos_path *path, uint32_t mode,
                                 void **handle)
{
    typedef int (*fn)(const struct osos_path *, uint32_t, uint32_t, void **);
    return ((fn)0x0804f75c)(path, 0x64617461, mode, handle);
}

static inline int osos_file_read(void *handle, void *data, uint32_t bytes,
                                 uint32_t *read)
{
    typedef int (*fn)(void *, void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x0804f810)(handle, data, bytes, 0, read);
}

static inline int osos_file_write(void *handle, const void *data, uint32_t bytes,
                                  uint32_t *written)
{
    typedef int (*fn)(void *, const void *, uint32_t, uint32_t, uint32_t *);
    return ((fn)0x0804ff40)(handle, data, bytes, 1, written);
}

static inline void osos_file_close(void *handle)
{
    typedef int (*fn)(void *);
    ((fn)0x0804f6bc)(handle);
}

#endif
#endif
