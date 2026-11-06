/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#define baseline fixture_baseline
#include "theme_support.h"
#undef baseline
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_switch.c"
#include "../../../payload/theme_tint.c"

#include "../../../payload/theme_boot.h"
#include "../../../payload/theme_discovery.h"
#define OSOS_ACTION_SUPPORT_QUERY UINT32_MAX
#define OSOS_PROPERTY_COUNT 1
#define OSOS_PROPERTY_STRING 2
#define OSOS_RESOURCE_BMAP 3
#define OSOS_NOTIFY_ALL 4
#define OSOS_RESOURCE_DRAW 5
#define OSOS_MENU_CHECKED_BITMAP 100
static unsigned row, notices, invalidations, stores, scans, boot_resolves;
#include "theme_settings_ui.h"
#include "accent_settings_ui.h"
#include "dark_mode_ui.h"
static int dark_visible, theme_visible;

static int osos_menu_item_set_visible(uint32_t item, uint32_t visible)
{
    if (item == CFW_DarkMode_Item)
        dark_visible = visible;
    else if (item != CFW_Accent_Item) {
        assert(item == CFW_Theme_Item);
        theme_visible = visible;
    }
    return 1;
}

static int fail_store, fail_discovery, boot_recover;
static struct cfw_theme_preferences saved;
static struct cfw_theme_boot_request boot;
static uint32_t available = 3;

static void *osos_settings_model(void *c)
{
    return c;
}

static void *osos_now_playing_model(void)
{
    return (void *)1;
}

static void osos_model_notify(void *m, uint32_t t, uint32_t p)
{
    (void)m;
    (void)t;
    (void)p;
    notices++;
}

static void osos_ui_invalidate(void)
{
    invalidations++;
}

static void *osos_ui_selection(void)
{
    return &row;
}

static uint32_t osos_selection_index(void *p)
{
    assert(p == &row);
    return row;
}

int cfw_theme_boot_resolve(struct cfw_theme_boot_request *r)
{
    boot_resolves++;
    *r = boot;
    return boot_recover;
}

enum cfw_theme_store_result
cfw_theme_preferences_store(const struct cfw_theme_preferences *s)
{
    stores++;
    if (fail_store)
        return CFW_THEME_STORE_UNKNOWN;
    saved = *s;
    return CFW_THEME_STORE_DURABLE;
}

static int discovery_overflow, no_custom;
static unsigned probes;

uint32_t cfw_theme_discovery_scan(struct cfw_theme_choice *out, uint32_t capacity,
                                  int *overflow)
{
    scans++;
    if (!capacity) {
        assert(!out);
        probes++;
        *overflow = !no_custom;
        return 0;
    }
    *overflow = discovery_overflow;
    if (no_custom)
        return 0;
    if (discovery_overflow) {
        for (unsigned i = 0; i < capacity; i++) {
            memset(&out[i], 0, sizeof(out[i]));
            snprintf(out[i].id, sizeof(out[i].id), "a%02u", i);
            strcpy(out[i].name, out[i].id);
        }
        return capacity;
    }
    memset(out, 0, sizeof(*out));
    strcpy(out->id, "modern");
    strcpy(out->name, "Modern");
    return 1;
}

int cfw_theme_discovery_path(const char *id, const char *file, struct osos_path *out)
{
    (void)id;
    (void)file;
    out->volume = 0;
    strcpy(out->text, "0");
    return 1;
}

int cfw_theme_discovery_select(const char *id, uint32_t prefer, int fallback,
                               struct cfw_theme_metadata *out, struct osos_path *path,
                               uint32_t *dark)
{
    assert(!strcmp(id, "modern"));
    if (fail_discovery || (!(available & (1u << prefer)) && !fallback))
        return 0;
    memset(out, 0, sizeof(*out));
    strcpy(out->id, id);
    strcpy(out->name, "Modern");
    out->appearance_mask = available;
    *dark = available & (1u << prefer) ? prefer : !prefer;
    path->volume = 0;
    strcpy(path->text, "0");
    return 1;
}

#include "../../../payload/theme_settings.c"

static void choose(unsigned index)
{
    row = index;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Select", 0));
}

int main(void)
{
    memcpy(original, pixels, sizeof(pixels));
    make_baseline();
    make_pack(&files[3], 1, 2, 0); /* Classic dark, independent of custom artwork. */
    make_target(0);
    fix_crc();
    (void)incoming;
    (void)fixture_baseline;
    (void)metadata;
    (void)active_bits;
    assert(
        cfw_theme_settings_action(NULL, "CFW_Theme_Open", OSOS_ACTION_SUPPORT_QUERY));
    assert(!initialized && !scans && !stores);
    cfw_theme_settings_load();
    cfw_theme_settings_load();
    assert(!stores && !switcher.active.custom && classic_available);
    assert(
        cfw_theme_settings_action(NULL, "CFW_Accent_Open", OSOS_ACTION_SUPPORT_QUERY));
    row = 2; /* Teal in display order, stable stored identifier 7. */
    int accent_opens = opens;
    unsigned accent_allocs = alloc_attempts;
    assert(cfw_theme_settings_action(NULL, "CFW_Accent_Select", 0));
    assert(saved.selection.accent == CFW_THEME_ACCENT_TEAL);
    assert(cfw_theme_accent_get() == CFW_THEME_ACCENT_TEAL);
    assert(opens == accent_opens && alloc_attempts == accent_allocs);
    assert(cfw_theme_settings_accent_selected() == 2);
    cfw_theme_input_drained();
    fail_store = 1;
    row = 4;
    assert(cfw_theme_settings_action(NULL, "CFW_Accent_Select", 0));
    assert(cfw_theme_accent_get() == CFW_THEME_ACCENT_TEAL);
    assert(!strcmp(accent_error, "Could not save theme") &&
           switcher.active.selection.accent == CFW_THEME_ACCENT_TEAL);
    cfw_theme_input_drained();
    fail_store = 0;
    row = 0;
    assert(cfw_theme_settings_action(NULL, "CFW_Accent_Select", 0));
    assert(!accent_error && cfw_theme_accent_get() == CFW_THEME_ACCENT_DEFAULT);
    cfw_theme_input_drained();
    stores = 0;
    no_custom = 1;
    cfw_theme_settings_refresh();
    assert(!theme_visible && dark_visible && !count);
    cfw_theme_settings_toggle(NULL);
    assert(switcher.active.selection.dark && stores == 1);
    cfw_theme_input_drained();
    cfw_theme_settings_toggle(NULL);
    assert(!switcher.active.selection.dark && stores == 2);
    cfw_theme_input_drained();
    stores = 0;
    no_custom = 0;
    unsigned before_probes = probes, before_allocs = alloc_attempts;
    cfw_theme_settings_refresh();
    assert(theme_visible && dark_visible && !count && !choices);
    assert(probes == before_probes + 1 && alloc_attempts == before_allocs);
    uintptr_t value;
    /* Without memory the picker offers Classic plus an error row. */
    fail_alloc = 1;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Open", 0));
    fail_alloc = 0;
    assert(!choices && !count && !strcmp(error, "Not enough memory"));
    assert(
        cfw_theme_settings_property(OSOS_PROPERTY_COUNT, CFW_Theme_Choices, &value) &&
        value == 2);
    assert(cfw_theme_settings_indexed_property(OSOS_PROPERTY_STRING, CFW_Theme_Choices,
                                               1, &value));
    assert(!strcmp((const char *)value, "Not enough memory"));
    choose(1);
    assert(!stores && !switcher.active.custom);
    cfw_theme_settings_close();
    unsigned live_before = live_bytes;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Open", 0));
    assert(choices && count == 1 && !error);
    assert(live_bytes == live_before + THEME_CHOICES * sizeof(struct cfw_theme_choice));
    assert(
        cfw_theme_settings_property(OSOS_PROPERTY_COUNT, CFW_Theme_Choices, &value) &&
        value == 2);
    assert(cfw_theme_settings_indexed_property(OSOS_PROPERTY_STRING, CFW_Theme_Choices,
                                               1, &value));
    assert(!strcmp((const char *)value, "Modern"));
    available = 2;
    choose(1); /* Light preference falls back to the theme's sole dark appearance. */
    assert(stores == 1 && switcher.active.custom && switcher.active.selection.dark);
    assert(!dark_visible && cfw_theme_settings_selected() == 1);
    assert(!strcmp(saved.selection.id, "modern") && pixels[0][32] == 99);
    discovery_overflow = 1;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Open", 0));
    assert(count == 64 && cfw_theme_settings_selected() == 64);
    assert(!strcmp(choices[63].name, "Modern"));
    fail_discovery = 1;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Open", 0));
    assert(count == 64 && !strcmp(choices[63].id, "a63"));
    fail_discovery = 0;
    discovery_overflow = 0;
    assert(cfw_theme_settings_action(NULL, "CFW_Theme_Open", 0));
    assert(count == 1 && cfw_theme_settings_selected() == 1);
    unsigned accepted = stores;
    choose(0);
    cfw_theme_settings_toggle(NULL);
    assert(stores == accepted && switcher.active.custom);
    cfw_theme_input_drained();
    cfw_theme_settings_toggle(NULL);
    assert(stores == accepted && switcher.active.selection.dark && error);
    assert(
        cfw_theme_settings_property(OSOS_PROPERTY_COUNT, CFW_Theme_Choices, &value) &&
        value == 3);
    choose(2);
    assert(stores == accepted); /* Error rows cannot apply a theme. */
    available = 3;
    struct pack_header *h = (void *)files[0].data;
    h->appearance = 0;
    fail_store = 1;
    cfw_theme_input_drained();
    cfw_theme_settings_toggle(NULL);
    assert(switcher.active.selection.dark && !strcmp(error, "Could not save theme"));
    fail_store = 0;
    cfw_theme_input_drained();
    cfw_theme_settings_toggle(NULL);
    assert(!switcher.active.selection.dark && !error && dark_visible);
    cfw_theme_input_drained();
    choose(0);
    assert(!switcher.active.custom && !memcmp(pixels, original, sizeof(pixels)));
    assert(cfw_theme_settings_indexed_property(OSOS_RESOURCE_BMAP, CFW_Theme_Choices, 0,
                                               &value));
    assert(value == 0 && cfw_theme_settings_selected() == 0);
    fail_discovery = 1;
    cfw_theme_input_drained();
    choose(1);
    assert(!switcher.active.custom && !strcmp(error, "Theme unavailable"));
    fail_discovery = 0;
    reads_fail = 1;
    cfw_theme_input_drained();
    choose(1);
    assert(!switcher.active.custom && !strcmp(error, "Could not load theme"));
    reads_fail = 0;
    /* Closing the picker releases the list; stale rows and Select are inert. */
    cfw_theme_settings_close();
    cfw_theme_settings_close();
    assert(!choices && !count && !overflow && !cfw_theme_settings_selected());
    assert(cfw_theme_settings_indexed_property(OSOS_PROPERTY_STRING, CFW_Theme_Choices,
                                               1, &value) &&
           (const char *)value == error);
    assert(!cfw_theme_settings_indexed_property(OSOS_PROPERTY_STRING, CFW_Theme_Choices,
                                                2, &value));
    accepted = stores;
    cfw_theme_input_drained();
    choose(1);
    assert(stores == accepted && !switcher.active.custom);
    assert(!allocations && opens == closes && notices && invalidations);
    /* A saved custom selection is applied at boot; a missing pack recovers stock. */
    initialized = 0;
    memset(&boot, 0, sizeof(boot));
    strcpy(boot.selection.id, "modern");
    strcpy(boot.pack.text, "0");
    strcpy(boot.name, "Modern");
    accepted = stores;
    fail_store = 1;
    cfw_theme_input_drained();
    cfw_theme_settings_load();
    assert(switcher.active.custom && stores == accepted);
    assert(!strcmp(active_name, "Modern"));
    fail_store = 0;
    memcpy(pixels, original, sizeof(pixels));
    initialized = 0;
    reads_fail = 1;
    unsigned resolved = boot_resolves;
    cfw_theme_input_drained();
    cfw_theme_settings_load();
    assert(boot_resolves == resolved);
    assert(!switcher.active.custom && !classic_available);
    assert(stores == accepted && !theme_visible && !dark_visible);
    reads_fail = 0;
    /* A repair does not unlock this session, including direct action calls. */
    unsigned before_scans = scans;
    cfw_theme_settings_refresh();
    choose(1);
    cfw_theme_settings_toggle(NULL);
    assert(stores == accepted && scans == before_scans && !classic_available);
    /* Restart with valid files restores the saved candidate without a write. */
    initialized = 0;
    cfw_theme_settings_load();
    assert(classic_available && switcher.active.custom && stores == accepted);
    memcpy(pixels, original, sizeof(pixels));
    for (unsigned missing = 1; missing <= 3; missing++) {
        initialized = 0;
        missing_files = (missing & 1 ? 2u : 0) | (missing & 2 ? 8u : 0);
        boot_recover = 1; /* Must not reach preference recovery. */
        cfw_theme_settings_load();
        choose(0);
        cfw_theme_settings_toggle(NULL);
        assert(!classic_available && !theme_visible && !dark_visible);
        assert(!switcher.active.custom && !cfw_theme_appearance_get(0));
        assert(stores == accepted);
    }
    missing_files = 0;
    boot_recover = 0;
    for (unsigned file = 1; file <= 3; file += 2) {
        initialized = 0;
        files[file].data[4] ^= 1;
        cfw_theme_settings_load();
        assert(!classic_available && stores == accepted);
        files[file].data[4] ^= 1;
    }
    assert(!allocations && opens == closes);
}
