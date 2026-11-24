/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef uint32_t u32;
typedef struct rvx_package Pkg;
typedef struct rvx_file File;
typedef void (*ListFn)(const char *, int, void *);

struct rvx_hook {
    u32 site, value;
    int16_t priority;
    uint8_t mode, reserved;
};

struct rvx_setting {
    uint16_t key;
    uint8_t type, screen;
    int32_t fallback, minimum, maximum;
    const char *label;
    const char *const *labels;
    u32 label_count;
};

struct rvx_descriptor {
    u32 size;
    const struct rvx_hook *hooks;
    u32 count;
    const struct rvx_setting *settings;
    u32 setting_count;
    void (*unload)(void);
    void (*changed)(uint16_t);
    const void *screens;
    u32 screen_count;
    const void *entries;
    u32 entry_count;
    const void *service;
};

struct rvx_site {
    u32 address, expected;
    uint8_t kind, argc;
    uint16_t name;
};

enum {
    RVX_RAW_MAX = 8,
    RVX_MODULES = 15,
    RVX_API_MAJOR = 1,
    RVX_API = (RVX_API_MAJOR << 16) | 81
};

enum {
    OK,
    CONFLICT,
    NO_MEMORY,
    NOT_BOUND,
    BAD_SITE,
    BAD_ARGUMENT,
    NOT_FOUND,
    ALREADY_BOUND
};

struct rvx_manifest {
    u32 magic, version, minimum_api, flags, modules, sites, strings;
    uint16_t text[7], content, libraries, category;
    u32 content_count, requires_count;
};

struct module_record {
    uint16_t name;
    uint8_t trigger, screens;
    uint16_t first_screen;
    uint8_t preload, reserved;
};

struct entry {
    u32 name;
    uint8_t kind, codec, preload, reserved;
    u32 offset, stored, size;
};

struct rvx_module {
    uint8_t *image;
    const struct rvx_descriptor *desc;
    u32 size, text, export_count;

    struct rvx_export {
        u32 name, offset;
    } *exports;

    int loading, pending_unload;
};

struct rvx_package {
    Pkg *next;
    struct rvx_manifest *manifest;
    const struct rvx_site *sites;
    const struct module_record *records;
    const char *strings;
    struct entry *catalog;
    u32 entries, state;
    struct rvx_module *modules;
    struct rvx_block *heap;
    File *files;
    struct rvx_timer *timers;
    struct rvx_value *settings;
    void **preloaded;
    u32 string_base, string_count;
    char id[33], path[];
};

extern Pkg *rvx_packages;

int rvx_bind(Pkg *, const struct rvx_hook *);
int rvx_unbind(Pkg *, const struct rvx_hook *);
void rvx_remove_hooks(Pkg *, const struct rvx_module *);
u32 rvx_next(const void *, u32, u32, u32, u32, u32, u32);
u32 rvx_next7(const void *, u32, u32, u32, u32, u32, u32, u32);
u32 cfw_rvx_lock(void);
void cfw_rvx_unlock(u32);
void cfw_rvx_sync(const void *, const void *);
void cfw_rvx_write(uintptr_t, u32);

u32 rvx_hash(const char *);
int rvx_load(Pkg *, u32);
void rvx_unload(Pkg *, u32);
const void *rvx_service(Pkg *, u32);
void *rvx_alloc(Pkg *, u32);
void rvx_free(Pkg *, void *);
void rvx_cleanup(Pkg *);
int rvx_file_open(Pkg *, const char *, int, File **);
int rvx_file_read(Pkg *, File *, void *, u32, u32 *);
int rvx_file_write(Pkg *, File *, const void *, u32);
int rvx_file_seek(Pkg *, File *, u32);
void rvx_file_close(Pkg *, File *);
int rvx_file_delete(Pkg *, const char *);
int rvx_folder_list(Pkg *, const char *, ListFn, void *);
int rvx_content_list(Pkg *, const char *, ListFn, void *);
int rvx_content_open(Pkg *, const char *, File **);
int rvx_firmware_open(Pkg *, File **);
int rvx_asset_open(Pkg *, const char *, File **);
void *rvx_asset_load(Pkg *, const char *, u32 *);
int rvx_asset_read(Pkg *, const char *, u32, void *, u32, u32 *);
int rvx_open_entry(Pkg *, const struct entry *, File **);
int rvx_read_exact(Pkg *, File *, void *, u32);
int32_t rvx_get_int(Pkg *, uint16_t);
int rvx_set_int(Pkg *, uint16_t, int32_t);
u32 rvx_read_setting(Pkg *, uint16_t, void *, u32);
int rvx_write_setting(Pkg *, uint16_t, uint8_t, const void *, u32);
void cfw_rvx_flush(void);
void rvx_stop_timers(Pkg *, const struct rvx_module *);

extern const struct rvx_api {
    u32 size, version;
    int (*bind)(Pkg *, const struct rvx_hook *),
        (*unbind)(Pkg *, const struct rvx_hook *);
    u32 (*next)(const void *, u32, u32, u32, u32, u32, u32);
    int32_t (*get)(Pkg *, uint16_t);
    int (*set)(Pkg *, uint16_t, int32_t);
    u32 (*read)(Pkg *, uint16_t, void *, u32);
    int (*write)(Pkg *, uint16_t, uint8_t, const void *, u32);
    void (*flush)(void);
    void *(*alloc)(Pkg *, u32);
    void (*free)(Pkg *, void *);
    int (*load)(Pkg *, u32);
    void (*unload)(Pkg *, u32);
    void (*log)(Pkg *, const char *);
    int (*push)(Pkg *, const char *, void *);
    void (*pop)(Pkg *), (*refresh)(Pkg *, const char *), (*invalidate)(Pkg *);
    const void *(*debug)(void);
    void *(*asset_load)(Pkg *, const char *, u32 *);
    int (*asset_read)(Pkg *, const char *, u32, void *, u32, u32 *);
    u32 (*number)(char *, u32, u32), (*size_text)(char *, u32, u32);
    struct rvx_timer *(*timer_start)(Pkg *, u32, int, void (*)(void *), void *);
    void (*timer_stop)(Pkg *, struct rvx_timer *);
    void (*rect)(Pkg *, const void *), (*pane)(Pkg *);
    int (*open)(Pkg *, const char *, int, File **),
        (*file_read)(Pkg *, File *, void *, u32, u32 *),
        (*file_write)(Pkg *, File *, const void *, u32), (*seek)(Pkg *, File *, u32);
    void (*close)(Pkg *, File *);
    int (*remove)(Pkg *, const char *), (*list)(Pkg *, const char *, ListFn, void *);
    u32 (*lock)(void);
    void (*unlock)(u32);
    void (*select)(Pkg *, const char *, u32);
    u32 (*string)(Pkg *, const char *);
    int (*content_open)(Pkg *, const char *, File **),
        (*content_list)(Pkg *, const char *, ListFn, void *),
        (*asset_open)(Pkg *, const char *, File **), (*colors)(Pkg *, const u32 *);
    void (*redraw)(Pkg *);
    u32 (*next7)(const void *, u32, u32, u32, u32, u32, u32, u32);
    const void *(*service)(Pkg *, u32);
    int (*provider)(Pkg *, const void *), (*firmware)(Pkg *, File **);
} cfw_rvx_api;

_Static_assert(sizeof(struct rvx_api) == 48 * 4 &&
                   sizeof(struct rvx_descriptor) == 48 &&
                   sizeof(struct rvx_manifest) == 56 && sizeof(struct entry) == 20,
               "RVX ABI");
int rvx_load_module(Pkg *, u32);
void rvx_install_menus(void);
void rvx_release_screen(Pkg *p, u32 index);
void *rvx_bitmap_load(Pkg *, const char *, u32 *);

int rvx_preload_assets(Pkg *);

void rvx_forget_provider(Pkg *, const void *, u32);
void rvx_forget_colors(Pkg *, const void *, u32);
int rvx_decode(const uint8_t *, u32, uint8_t *, u32);
void rvx_setting_updated(Pkg *, uint16_t);
