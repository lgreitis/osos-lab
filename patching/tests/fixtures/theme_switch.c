/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_switch.c"
#include "../../../payload/theme_tint.c"

static struct cfw_theme_switch switcher;
static struct cfw_theme_preferences disk;
static enum cfw_theme_store_result outcome;
static unsigned stores;
static int reenter;
static const struct cfw_theme_selection modern = {.id = "modern", .dark = 1};
static const struct cfw_theme_selection stock = {0};

static enum cfw_theme_store_result store(const struct cfw_theme_preferences *state)
{
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
    assert(cfw_theme_switch_init(&switcher, &ops));
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
    /* First boot without a record is Classic light and needs no recovery write. */
    assert(!cfw_theme_boot_selection(CFW_THEME_PREF_ABSENT, NULL, &desired));
    assert(!memcmp(&desired, &stock, sizeof(stock)));
    assert(!cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &saved, &desired));
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
        assert(cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &bad, &desired));
        assert(!memcmp(&desired, &stock, sizeof(stock)));
    }
    assert(cfw_theme_boot_selection(CFW_THEME_PREF_INVALID, &saved, &desired));
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

/* Tinted palettes and ARGB pixels follow the Theme Default tint, their role and
 * the accent in RAM; stock restoration returns the exact original bytes. Masks
 * are tinted while drawing. */
static void tinting(void)
{
    static const unsigned char tinted_entries[] = {30, 20, 10, 255, 30, 20, 10, 255};
    reset();
    tint_target(0, 10, 20, 30);
    tint_target(MASK_ASSET, 10, 20, 30);
    tint_target(PAYLOAD_ASSET, 10, 20, 30);
    target_options()->accents[CFW_THEME_ACCENT_PINK - 1][CFW_PALETTE_ICON][0] = 77;
    target_options()->accents[CFW_THEME_ACCENT_PINK - 1][CFW_PALETTE_ON_ACCENT][0] = 88;
    fix_crc();
    assert(!cfw_theme_bitmap_tint(pixels[MASK_ASSET] + 28));
    assert(!cfw_theme_bitmap_tint(payload_pixels()));
    assert(apply(&modern));
    assert(!memcmp(pixels[0] + 32, tinted_entries,
                   8)); /* the record's 99 is overwritten */
    assert(!memcmp(pixels[0] + 40, original[0] + 40, 2));
    assert(!memcmp(pixels[1], original[1], 42) &&
           !memcmp(pixels[MASK_ASSET], original[MASK_ASSET], 42));
    const unsigned char *color = cfw_theme_bitmap_tint(pixels[MASK_ASSET] + 28);
    assert(color && color[0] == 10 && color[1] == 20 && color[2] == 30);
    assert(!cfw_theme_bitmap_tint(pixels[1] + 28)); /* not tinted */
    assert(!cfw_theme_bitmap_tint(pixels[0] + 32)); /* not a pixel start */
    assert(!cfw_theme_bitmap_tint(NULL));
    /* The payload's catalog bitmaps are tinted through the same tint roles. */
    assert(cfw_theme_bitmap_tint(payload_pixels()) == color);
    assert(!cfw_theme_bitmap_tint(payload_pixels() + 1));
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK));
    assert(pixels[0][32 + 2] == 77 && pixels[0][32 + 3] == 255 &&
           pixels[0][32 + 7] == 255);
    assert(cfw_theme_bitmap_tint(pixels[MASK_ASSET] + 28)[0] == 77);
    /* A highlighted view draws tinted assets in on_accent; the palette returns to
     * the tint after the blit. Masks have no palette to swap. */
    struct osos_view_stub plain = {pixels[0] + 28, 0}, lit = {pixels[0] + 28, 1},
                          mask = {pixels[MASK_ASSET] + 28, 1},
                          other = {pixels[1] + 28, 1};
    assert(cfw_theme_highlight_tint(&plain)[0] == 77);
    assert(cfw_theme_highlight_tint(&lit)[0] == 88);
    assert(cfw_theme_highlight_tint(&mask)[0] == 88);
    assert(!cfw_theme_highlight_tint(&other));
    cfw_theme_bitmap_blit_begin(&plain, &plain);
    assert(pixels[0][32 + 2] == 77);
    cfw_theme_bitmap_blit_end();
    cfw_theme_bitmap_blit_begin(&lit, &lit);
    assert(pixels[0][32 + 2] == 88 && pixels[0][32 + 3] == 255 &&
           pixels[0][32 + 6] == 88 && pixels[0][32 + 7] == 255);
    cfw_theme_bitmap_blit_end();
    assert(pixels[0][32 + 2] == 77 && pixels[0][32 + 6] == 77);
    cfw_theme_bitmap_blit_begin(&other, &other);
    cfw_theme_bitmap_blit_end();
    cfw_theme_bitmap_blit_begin(&mask, &mask);
    cfw_theme_bitmap_blit_end();
    assert(!memcmp(pixels[1], original[1], 42) &&
           !memcmp(pixels[MASK_ASSET], original[MASK_ASSET], 42));
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();
    assert(!cfw_theme_bitmap_tint(pixels[MASK_ASSET] + 28));
    assert(!cfw_theme_highlight_tint(&lit));
    cfw_theme_bitmap_blit_begin(&lit, &lit);
    cfw_theme_bitmap_blit_end();
    check_stock();
    assert(!cfw_theme_bitmap_tint(payload_pixels()));
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);

    /* An ARGB asset tinted in the selection role: opaque pixels take the role's
     * RGB (BGRA in memory) and keep their alpha, transparent pixels stay as the
     * record wrote them, and the accent's selection retints. Restoration is exact. */
    reset();
    make_target(ARGB_ASSET);
    tint_target_role(ARGB_ASSET, CFW_TINT_ROLE_BASE + CFW_THEME_SELECTION, 10, 20, 30);
    unsigned char *selection =
        (unsigned char *)files[0].data + 52 + CFW_THEME_SELECTION * 4;
    selection[0] = 1;
    selection[1] = 2;
    selection[2] = 3;
    target_options()->accents[CFW_THEME_ACCENT_PINK - 1][CFW_PALETTE_SELECTION][0] = 66;
    target_options()->accents[CFW_THEME_ACCENT_PINK - 1][CFW_PALETTE_ON_ACCENT][0] = 99;
    fix_crc();
    assert(apply(&modern));
    static const unsigned char tinted_argb[] = {3, 2, 1, 255, 0, 0, 0, 0};
    assert(!memcmp(pixels[ARGB_ASSET] + 28, tinted_argb, 8));
    assert(cfw_theme_bitmap_tint(pixels[ARGB_ASSET] + 28)[0] == 1);
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK));
    assert(pixels[ARGB_ASSET][28 + 2] == 66 && pixels[ARGB_ASSET][28 + 3] == 255 &&
           !memcmp(pixels[ARGB_ASSET] + 32, tinted_argb + 4, 4));
    struct osos_view_stub argb_lit = {pixels[ARGB_ASSET] + 28, 1};
    cfw_theme_bitmap_blit_begin(&argb_lit, &argb_lit);
    assert(pixels[ARGB_ASSET][28 + 2] == 99); /* the accent's on_accent */
    cfw_theme_bitmap_blit_end();
    assert(pixels[ARGB_ASSET][28 + 2] == 66);
    /* Well letters take text or background, whichever is farther in brightness
     * from the well center pixel. */
    for (unsigned shade = 0; shade < 256; shade += 51) {
        pixels[ARGB_ASSET][28] = pixels[ARGB_ASSET][29] = pixels[ARGB_ASSET][30] =
            shade;
        const unsigned char well[4] = {shade, shade, shade, 255};
        const unsigned char *text = cfw_theme_color(CFW_THEME_TEXT, 1);
        const unsigned char *background = cfw_theme_color(CFW_THEME_BACKGROUND, 1);
        int text_gap = abs(cfw_theme_brightness(text) - cfw_theme_brightness(well));
        int background_gap =
            abs(cfw_theme_brightness(background) - cfw_theme_brightness(well));
        for (unsigned style = 0; style < 2; style++)
            assert(cfw_theme_well_letter_color(style) ==
                   (text_gap >= background_gap ? text : background));
    }
    assert(!cfw_theme_well_letter_color(2));
    pixels[ARGB_ASSET][31] = 0; /* transparent center keeps the native color */
    assert(!cfw_theme_well_letter_color(0));
    pixels[ARGB_ASSET][31] = 255;
    /* The Search input text follows the same rule against the paletted field
     * center slice, whose center pixel is its first palette entry. */
    for (unsigned shade = 0; shade < 256; shade += 51) {
        pixels[0][32] = pixels[0][33] = pixels[0][34] = shade;
        const unsigned char field[4] = {shade, shade, shade, 255};
        const unsigned char *text = cfw_theme_color(CFW_THEME_TEXT, 1);
        const unsigned char *background = cfw_theme_color(CFW_THEME_BACKGROUND, 1);
        int text_gap = abs(cfw_theme_brightness(text) - cfw_theme_brightness(field));
        int background_gap =
            abs(cfw_theme_brightness(background) - cfw_theme_brightness(field));
        assert(cfw_theme_field_text_color() ==
               (text_gap >= background_gap ? text : background));
    }
    pixels[0][35] = 0; /* transparent field art keeps the native color */
    assert(!cfw_theme_field_text_color());
    memcpy(pixels[0] + 32, original[0] + 32, 4);
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();
    assert(!cfw_theme_well_letter_color(1));
    assert(!cfw_theme_field_text_color()); /* Classic keeps the native colors */
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
}

int main(void)
{
    memcpy(original, pixels, sizeof(pixels));
    boot_recovery();
    validation();
    failures();
    switching();
    classic_dark();
    tinting();
    return 0;
}
