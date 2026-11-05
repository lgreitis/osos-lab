/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_switch.c"

static struct cfw_theme_switch switcher;
static struct cfw_theme_preferences disk;
static enum cfw_theme_store_result outcome;
static unsigned stores;
static int reenter;
static const struct cfw_theme_selection modern = {.id = "modern", .dark = 1};
static const struct cfw_theme_selection stock = {0};

static enum cfw_theme_store_result store(void *context,
                                         const struct cfw_theme_preferences *state)
{
    (void)context;
    stores++;
    assert(!state->pending);
    if (reenter)
        assert(!cfw_theme_switch_apply(&switcher, &stock, NULL, &baseline));
    if (outcome == CFW_THEME_STORE_DURABLE)
        disk = *state;
    return outcome;
}
static const struct cfw_theme_switch_ops ops = {store};

static void reset(void)
{
    assert(!allocations && !live_bytes && opens == closes);
    cfw_theme_input_drained();
    memcpy(pixels, original, sizeof(pixels));
    memset(&disk, 0, sizeof(disk));
    outcome = CFW_THEME_STORE_DURABLE;
    stores = reenter = 0;
    fail_alloc = reads_fail = short_reads = 0;
    alloc_attempts = fail_alloc_at = peak_bytes = 0;
    make_baseline();
    make_target(0);
    assert(cfw_theme_switch_init(&switcher, &ops, NULL));
}

static int apply(const struct cfw_theme_selection *selection)
{
    return cfw_theme_switch_apply(
        &switcher, selection, selection->id[0] || selection->dark ? &incoming : NULL,
        &baseline);
}

static void check_stock(void)
{
    assert(!switcher.active.selection.id[0] && !switcher.active.selection.dark);
    assert(!cfw_theme_style_custom());
    assert(!memcmp(pixels, original, sizeof(pixels)));
}

static void boot_recovery(void)
{
    struct cfw_theme_selection desired;
    struct cfw_theme_preferences saved = {.version = 1, .selection = modern};
    assert(
        !cfw_theme_boot_selection(CFW_THEME_PREF_ABSENT, NULL, 0x314b5244, &desired));
    assert(!desired.id[0] && desired.dark);
    assert(
        !cfw_theme_boot_selection(CFW_THEME_PREF_ABSENT, NULL, 0xffffffff, &desired) &&
        !desired.dark);
    assert(!cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &saved, 0, &desired));
    assert(!strcmp(desired.id, "modern"));
    for (unsigned malformed = 0; malformed < 5; malformed++) {
        struct cfw_theme_preferences bad = saved;
        if (malformed == 0)
            bad.pending = 1;
        if (malformed == 1)
            bad.version = 2;
        if (malformed == 2)
            bad.selection.dark = 2;
        if (malformed == 3)
            bad.selection.id[0] = '.';
        if (malformed == 4)
            bad.selection.accent = CFW_THEME_ACCENT_COUNT;
        assert(
            cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &bad, 0x314b5244, &desired));
        assert(!memcmp(&desired, &stock, sizeof(stock)));
    }
    assert(
        cfw_theme_boot_selection(CFW_THEME_PREF_INVALID, &saved, 0x314b5244, &desired));
}

static void failures(void)
{
    for (unsigned failure = 0; failure < 6; failure++) {
        reset();
        if (failure == 1)
            switcher.assets[0] = 2; /* Force a stock-restoration allocation. */
        fail_alloc_at = failure == 0 ? 1 : failure == 1 ? 2 : 0;
        reads_fail = failure == 2;
        short_reads = failure == 3;
        if (failure == 4)
            files[0].data[60] ^= 1;
        if (failure == 5)
            outcome = CFW_THEME_STORE_UNKNOWN;
        assert(!apply(&modern));
        assert(switcher.result ==
               (failure == 5 ? CFW_THEME_STORE_FAILED : CFW_THEME_LOAD_FAILED));
        assert(stores == (failure == 5));
        check_stock();
        assert(!allocations && !live_bytes && !switcher.busy);
        assert(!apply(&modern)); /* queued repeat remains suppressed on failure */
    }
    reset();
    assert(apply(&modern));
    cfw_theme_input_drained();
    files[1].size--;
    unsigned before = stores;
    assert(!apply(&stock) && stores == before);
    assert(pixels[0][32] == 99 && cfw_theme_style_custom());
    assert(!allocations);
}

static void switching(void)
{
    reset();
    reenter = 1;
    assert(apply(&modern));
    assert(stores == 1 && !disk.pending && !strcmp(disk.selection.id, "modern"));
    assert(pixels[0][32] == 99 && cfw_theme_style_custom());
    assert(!allocations && !live_bytes);
    assert(!apply(&stock) && stores == 1); /* queued presses do not reverse it */
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();
    for (unsigned i = 0; i < 100; i++) {
        cfw_theme_input_drained();
        make_target(i % 2);
        assert(apply(&modern));
        assert(pixels[i % 2][32] == 99 && pixels[1 - i % 2][32] != 99);
        assert(!allocations && opens == closes);
    }
    cfw_theme_input_drained();
    make_target(0);
    struct pack_header *header = (void *)files[0].data;
    header->count = 0;
    header->size = CFW_THEME_STYLE_BYTES;
    files[0].size = 52 + CFW_THEME_STYLE_BYTES;
    fix_crc();
    assert(apply(&modern)); /* color-only target restores all prior replacements */
    assert(!memcmp(pixels, original, sizeof(pixels)) && cfw_theme_style_custom());
    cfw_theme_input_drained();
    outcome = CFW_THEME_STORE_UNCHANGED;
    assert(!apply(&stock));
    assert(!strcmp(switcher.active.selection.id, "modern"));
    cfw_theme_input_drained();
    outcome = CFW_THEME_STORE_DURABLE;
    assert(apply(&stock));
    check_stock();
}

static void classic_dark(void)
{
    reset();
    make_pack(&files[0], 1, 1, 1);
    struct cfw_theme_selection dark = {.dark = 1};
    assert(apply(&dark));
    assert(!cfw_theme_style_custom() && cfw_theme_appearance_get(0) == 1);
    assert(cfw_theme_color(CFW_THEME_BACKGROUND, cfw_theme_appearance_get(0))[0] == 24);
    assert(pixels[0][32] == 99);
    cfw_theme_input_drained();
    assert(apply(&stock));
    assert(cfw_theme_appearance_get(1) == 0);
    check_stock();
}

static void validation(void)
{
    reset();
    struct cfw_theme_selection bad = modern;
    memset(bad.id, 'a', sizeof(bad.id));
    assert(!apply(&bad));
    bad = modern;
    strcpy(bad.id, "classic");
    assert(!cfw_theme_selection_valid(&bad));
    strcpy(bad.id, "dark");
    assert(!apply(&bad));
    bad = modern;
    bad.accent = CFW_THEME_ACCENT_COUNT;
    assert(!apply(&bad));
    assert(!cfw_theme_switch_apply(&switcher, NULL, NULL, &baseline));
    assert(!cfw_theme_switch_apply(&switcher, &modern, NULL, &baseline));
    assert(!cfw_theme_switch_apply(&switcher, &stock, &incoming, &baseline));
    assert(!stores && !allocations);
    assert(cfw_theme_switch_store_classic(&switcher));
    outcome = CFW_THEME_STORE_UNKNOWN;
    assert(!cfw_theme_switch_store_classic(&switcher));
    check_stock();
}

int main(void)
{
    memcpy(original, pixels, sizeof(pixels));
    boot_recovery();
    validation();
    failures();
    switching();
    classic_dark();
    return 0;
}
