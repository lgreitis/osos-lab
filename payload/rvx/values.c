/* SPDX-License-Identifier: GPL-3.0-only */
#include "internal.h"
#include "osos.h"
#include "../../rvx-sdk/rv_api.h"

struct rvx_value {
    struct rvx_value *next;
    uint16_t key;
    uint8_t dirty, capacity, type, bytes, data[];
};

static void filename(uint16_t key, char *name)
{
    const char *prefix = ".settings/";
    for (unsigned i = 0; i < 10; i++)
        name[i] = prefix[i];
    name[14] = 0;
    for (unsigned i = 0; i < 4; i++) {
        name[13 - i] = "0123456789abcdef"[key & 15];
        key >>= 4;
    }
}

static struct rvx_value *value(Pkg *p, uint16_t key)
{
    if (!key || key == 0xffff)
        return NULL;
    struct rvx_value *v = p->settings;
    while (v && v->key != key)
        v = v->next;
    if (v)
        return v;
    char name[15];
    filename(key, name);
    File *f = NULL;
    uint8_t header[2] = {0, 0};
    u32 got;
    if (rvx_file_open(p, name, 1, &f) == OK &&
        (rvx_file_read(p, f, header, sizeof header, &got) != OK ||
         got != sizeof header))
        header[0] = header[1] = 0;
    unsigned capacity = header[1] < sizeof(int32_t) ? sizeof(int32_t) : header[1];
    v = osos_import_alloc(offsetof(struct rvx_value, data) + capacity);
    if (v) {
        v->key = key;
        v->capacity = capacity;
        v->type = header[0];
        if (f && header[1] && rvx_file_read(p, f, v->data, header[1], &got) == OK &&
            got == header[1])
            v->bytes = header[1];
        v->next = p->settings;
        p->settings = v;
    }
    if (f)
        rvx_file_close(p, f);
    return v;
}

u32 rvx_read_setting(Pkg *p, uint16_t key, void *out, u32 cap)
{
    const struct rvx_value *v = value(p, key);
    if (!v || !out)
        return 0;
    u32 n = cap < v->bytes ? cap : v->bytes;
    for (u32 i = 0; i < n; i++)
        ((uint8_t *)out)[i] = v->data[i];
    return n;
}

int rvx_write_setting(Pkg *p, uint16_t key, uint8_t type, const void *data, u32 n)
{
    if (!key || key == 0xffff || type < 1 || type > 4 || n > 255 || (n && !data))
        return BAD_ARGUMENT;
    struct rvx_value *v = value(p, key);
    if (!v)
        return NO_MEMORY;
    if (n > v->capacity) {
        struct rvx_value *larger =
            osos_import_alloc(offsetof(struct rvx_value, data) + n);
        if (!larger)
            return NO_MEMORY;
        larger->key = key;
        larger->capacity = n;
        struct rvx_value **link = &p->settings;
        while (*link != v)
            link = &(*link)->next;
        larger->next = v->next;
        *link = larger;
        osos_import_free(v);
        v = larger;
    }
    const uint8_t *src = data;
    int same = v->type == type && v->bytes == n;
    for (unsigned i = 0; i < n; i++) {
        same &= v->data[i] == src[i];
        v->data[i] = src[i];
    }
    if (same)
        return OK;
    v->type = type;
    v->bytes = n;
    v->dirty = 1;
    /* Screens and menus show the new value before the extension hears of it. */
    rvx_setting_updated(p, key);
    const struct rvx_descriptor *d = p->modules[0].desc;
    if (d && d->changed)
        d->changed(key);
    return OK;
}

const struct rvx_setting *rvx_declaration(Pkg *p, uint16_t key)
{
    const struct rvx_descriptor *d = p->modules[0].desc;
    if (d)
        for (unsigned i = 0; i < d->setting_count; i++)
            if (d->settings[i].key == key)
                return d->settings + i;
    return NULL;
}

/* A stored int or bool of four bytes, else the declared fallback. */
int32_t rvx_get_int(Pkg *p, uint16_t key)
{
    const struct rvx_value *v = value(p, key);
    int32_t n;
    if (v && (v->type == RV_SETTING_TYPE_INT || v->type == RV_SETTING_TYPE_BOOL) &&
        v->bytes == sizeof n) {
        uint8_t *to = (uint8_t *)&n;
        for (unsigned i = 0; i < sizeof n; i++)
            to[i] = v->data[i];
        return n;
    }
    const struct rvx_setting *s = rvx_declaration(p, key);
    return s ? s->fallback : 0;
}

/* Clamped to the declared range; a multi-choice keeps the bits its maximum
 * names. Stored as an int whatever the declared type. */
int rvx_set_int(Pkg *p, uint16_t key, int32_t n)
{
    const struct rvx_setting *s = rvx_declaration(p, key);
    if (s && s->screen == RV_SCREEN_KIND_MULTI_CHOICE)
        n = (int32_t)((u32)n & (u32)s->maximum);
    else if (s && n < s->minimum)
        n = s->minimum;
    else if (s && n > s->maximum)
        n = s->maximum;
    return rvx_write_setting(p, key, RV_SETTING_TYPE_INT, &n, sizeof n);
}

void cfw_rvx_flush(void)
{
    for (Pkg *p = rvx_packages; p; p = p->next)
        for (struct rvx_value *v = p->settings; v; v = v->next) {
            if (!v->dirty)
                continue;
            char name[15];
            filename(v->key, name);
            File *f;
            if (rvx_file_open(p, name, 2, &f) == OK) {
                if (rvx_file_write(p, f, &v->type, 2 + v->bytes) == OK)
                    v->dirty = 0;
                rvx_file_close(p, f);
            }
        }
}

static void reset_record(const char *name, int folder, void *arg)
{
    if (folder || !name)
        return;
    unsigned key = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned c = (unsigned char)name[i];
        if (c >= '0' && c <= '9')
            c -= '0';
        else if ((c | 32) >= 'a' && (c | 32) <= 'f')
            c = (c | 32) - 'a' + 10;
        else
            return;
        key = (key << 4) | c;
    }
    if (!name[4])
        value(arg, key);
}

/* Reset All Settings empties every package's settings, started or not; menus
 * then show the rows and values the fallbacks give. */
void cfw_rvx_reset(void)
{
    for (Pkg *p = rvx_packages; p; p = p->next) {
        rvx_folder_list(p, ".settings", reset_record, p);
        while (p->settings) {
            struct rvx_value *v = p->settings;
            char name[15];
            filename(v->key, name);
            rvx_file_delete(p, name);
            p->settings = v->next;
            osos_import_free(v);
        }
    }
    rvx_menus_settings_reset();
    for (Pkg *p = rvx_packages; p; p = p->next) {
        const struct rvx_descriptor *d = p->state == 2 ? p->modules[0].desc : NULL;
        if (d && d->changed)
            d->changed(0xffff);
    }
}
