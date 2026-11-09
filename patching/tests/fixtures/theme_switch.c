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
    if (outcome == CFW_THEME_STORE_SAVED)
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
    outcome = CFW_THEME_STORE_SAVED;
    stores = reenter = 0;
    fail_alloc = reads_fail = short_reads = 0;
    fail_read_at = missing_files = 0;
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

/* The accent asset matches stock except its stock blue palette entry, which holds
 * the active selection color (BGRA); its gray entry stays. */
static int accent_recolored(void)
{
    const unsigned char *selection =
        cfw_theme_color(CFW_THEME_SELECTION, cfw_theme_appearance_get(0));
    const unsigned char entry[4] = {selection[2], selection[1], selection[0], 255};
    return !memcmp(pixels[ACCENT_ASSET], original[ACCENT_ASSET], 32) &&
           !memcmp(pixels[ACCENT_ASSET] + 32, entry, 4) &&
           !memcmp(pixels[ACCENT_ASSET] + 36, original[ACCENT_ASSET] + 36, 8);
}

/* Everything but the recolored accent asset matches stock. */
static int stock_but_accent(void)
{
    for (unsigned i = 0; i < ASSETS; i++)
        if (i != ACCENT_ASSET && memcmp(pixels[i], original[i], sizeof(pixels[i])))
            return 0;
    return accent_recolored();
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

static void check_classic_saved(void)
{
    check_stock();
    assert(!cfw_theme_appearance_get(0) && !switcher.active.selection.accent);
    assert(disk.version == 1 && !disk.selection.id[0] && !disk.selection.dark &&
           !disk.selection.accent);
}

static void failures(void)
{
    /* Prefix and store failures leave the theme unchanged. */
    for (unsigned failure = 0; failure < 6; failure++) {
        reset();
        if (failure == 0)
            missing_files = 1;
        if (failure == 1)
            files[1].data[4] ^= 1; /* baseline fingerprint */
        reads_fail = failure == 2;
        short_reads = failure == 3;
        if (failure == 4) {
            files[0].data[52 + 3] = 0; /* transparent style color */
            fix_crc();
        }
        if (failure == 5)
            outcome = CFW_THEME_STORE_UNKNOWN;
        assert(!apply(&modern));
        assert(switcher.result ==
               (failure == 5 ? CFW_THEME_STORE_FAILED : CFW_THEME_LOAD_FAILED));
        assert(stores == (failure == 5));
        missing_files = reads_fail = short_reads = 0;
        check_stock();
        assert(!allocations && !live_bytes && !switcher.busy && opens == closes);
        assert(!apply(&modern)); /* queued repeat remains suppressed on failure */
    }
    /* Damage found while writing recovers Classic light and saves it. */
    for (unsigned failure = 0; failure < 3; failure++) {
        reset();
        assert(apply(&modern));
        cfw_theme_input_drained();
        make_target(1);
        if (failure == 0)
            files[0].data[52 + CFW_THEME_STYLE_BYTES + 12 + 4] ^= 1; /* checksum */
        if (failure == 1) {
            files[0].data[52 + CFW_THEME_STYLE_BYTES + 12 + 8] = 2; /* index */
            fix_crc();
        }
        /* header, end, style, baseline header and end, record header, data */
        if (failure == 2)
            fail_read_at = reads + 7;
        struct cfw_theme_selection accented = modern;
        accented.accent = CFW_THEME_ACCENT_PINK;
        assert(!apply(&accented));
        fail_read_at = 0;
        assert(switcher.result == CFW_THEME_RECOVERED && stores == 3);
        check_classic_saved();
        assert(!switcher.assets[0] && !allocations && opens == closes);
        cfw_theme_input_drained();
        make_target(0);
        assert(apply(&modern)); /* theming keeps working */
    }
    /* A baseline that cannot restore leaves the damaged bitmaps marked. */
    reset();
    assert(apply(&modern));
    cfw_theme_input_drained();
    files[1].data[52 + CFW_THEME_STYLE_BYTES + 12 + 4] ^= 1;
    assert(!apply(&stock));
    /* Asset 0 is the failed replacement, the accent asset its recolored art. */
    assert(switcher.result == CFW_THEME_RESTORE_FAILED &&
           switcher.assets[0] == (1u | 1u << ACCENT_ASSET));
    assert(!switcher.active.custom && !cfw_theme_style_custom());
    assert(disk.version == 1 && !disk.selection.id[0] && opens == closes);
    /* A stale baseline header fails before anything is saved or written. */
    reset();
    assert(apply(&modern));
    cfw_theme_input_drained();
    files[1].data[4] ^= 1;
    unsigned before = stores;
    assert(!apply(&stock) && stores == before);
    assert(switcher.result == CFW_THEME_LOAD_FAILED);
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
    peak_bytes = 0;
    for (unsigned i = 0; i < 100; i++) {
        cfw_theme_input_drained();
        make_target(i % 2);
        assert(apply(&modern));
        assert(!peak_bytes); /* nothing is staged in heap */
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
    assert(stock_but_accent() && cfw_theme_style_custom());
    cfw_theme_input_drained();
    outcome = CFW_THEME_STORE_UNCHANGED;
    assert(!apply(&stock));
    assert(!strcmp(switcher.active.selection.id, "modern"));
    cfw_theme_input_drained();
    outcome = CFW_THEME_STORE_SAVED;
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
    assert(accent_recolored() &&
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
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK, &baseline));
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
    /* A calendar marker turns on_accent only over the selected day's cell art
     * (asset 0 in the fixture); other cells keep the tint. */
    {
        unsigned char cell[0x1b4 + sizeof(void *)];
        const void *art = &plain;
        memcpy(cell + 0x1b4, &art, sizeof(art));
        cfw_theme_calendar_marker_begin(cell, &plain);
        assert(pixels[0][32 + 2] == 88);
        cfw_theme_bitmap_blit_end();
        assert(pixels[0][32 + 2] == 77);
        art = &other;
        memcpy(cell + 0x1b4, &art, sizeof(art));
        cfw_theme_calendar_marker_begin(cell, &plain);
        assert(pixels[0][32 + 2] == 77);
        cfw_theme_bitmap_blit_end();
    }
    assert(accent_recolored() &&
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
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK, &baseline));
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
        for (unsigned style = 0; style < 3; style++)
            assert(cfw_theme_well_letter_color(style) ==
                   (text_gap >= background_gap ? text : background));
    }
    assert(!cfw_theme_well_letter_color(3));
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
        /* The fixture's status bar and active calendar cell are the same
         * paletted asset; listed views and active days follow it too. */
        unsigned char out[4];
        assert(cfw_theme_contrast_text_color(0x0dad020e) == cfw_theme_field_text_color());
        assert(cfw_theme_calendar_text_color(CFW_CALENDAR_ACTIVE, out) ==
               cfw_theme_field_text_color());
        assert(!cfw_theme_contrast_text_color(0x0dad020f));
    }
    pixels[0][35] = 0; /* transparent field art keeps the native color */
    assert(!cfw_theme_field_text_color());
    assert(!cfw_theme_contrast_text_color(0x0dad020e));
    /* A clear cell center counts as the page: the role that contrasts with the
     * background, mixed halfway toward it for days of other months. */
    {
        unsigned char out[4], half[4];
        const unsigned char *background = cfw_theme_color(CFW_THEME_BACKGROUND, 1);
        const unsigned char *active = cfw_theme_calendar_text_color(CFW_CALENDAR_ACTIVE, out);
        assert(active == cfw_theme_color(CFW_THEME_TEXT, 1));
        cfw_theme_mix(half, active, background, 128);
        assert(cfw_theme_calendar_text_color(CFW_CALENDAR_INACTIVE, out) == out &&
               !memcmp(out, half, 4));
        assert(cfw_theme_calendar_text_color(CFW_CALENDAR_HEADER, out) == out &&
               !memcmp(out, half, 4));
        assert(!cfw_theme_calendar_text_color(CFW_CALENDAR_HEADER + 1, out));
    }
    memcpy(pixels[0] + 32, original[0] + 32, 4);
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();
    assert(!cfw_theme_well_letter_color(1));
    assert(!cfw_theme_field_text_color()); /* Classic keeps the native colors */
    assert(!cfw_theme_contrast_text_color(0x0dad020e));
    {
        unsigned char out[4];
        assert(!cfw_theme_calendar_text_color(CFW_CALENDAR_ACTIVE, out));
    }
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
}

/* Stock accent artwork follows Classic accents other than Theme Default and Blue,
 * recolored from stock pixels each time, and returns to stock exactly. */
static void classic_accent(void)
{
    reset();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_BLUE, &baseline));
    check_stock();
    assert(!switcher.assets[0] && opens == closes);
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK, &baseline));
    assert(pixels[ACCENT_ASSET][32] == 86 && pixels[ACCENT_ASSET][34] == 196);
    assert(accent_recolored() && stock_but_accent());
    assert(switcher.assets[0] == 1u << ACCENT_ASSET &&
           switcher.accented[0] == 1u << ACCENT_ASSET);
    assert(disk.selection.accent == CFW_THEME_ACCENT_PINK);
    assert(!cfw_theme_switch_store_classic(&switcher));
    /* Another accent starts again from the stock pixels. */
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_GREEN, &baseline));
    assert(pixels[ACCENT_ASSET][32] == 58 && stock_but_accent());
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_DEFAULT, &baseline));
    check_stock();
    assert(!switcher.assets[0] && !switcher.accented[0] && opens == closes);

    /* Classic dark takes the dark preset; a theme switch restores the art first. */
    cfw_theme_input_drained();
    make_pack(&files[0], 1, 1, 1);
    struct cfw_theme_selection dark = {.dark = 1, .accent = CFW_THEME_ACCENT_TEAL};
    assert(apply(&dark));
    assert(pixels[0][32] == 99 && accent_recolored());
    assert(pixels[ACCENT_ASSET][34] == 0 && pixels[ACCENT_ASSET][33] == 120);
    assert(switcher.assets[0] == (1u | 1u << ACCENT_ASSET));
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();
    assert(!switcher.assets[0] && !switcher.accented[0]);

    /* A custom pack that replaces the asset keeps its own art. */
    cfw_theme_input_drained();
    make_target(ACCENT_ASSET);
    assert(apply(&modern));
    assert(pixels[ACCENT_ASSET][32] == 99 && !switcher.accented[0]);
    assert(switcher.assets[0] == 1u << ACCENT_ASSET);
    cfw_theme_input_drained();
    assert(cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_PINK, &baseline));
    assert(pixels[ACCENT_ASSET][32] == 99 && !switcher.accented[0]);
    cfw_theme_input_drained();
    assert(apply(&stock));
    check_stock();

    /* Boot restores a saved accent the same way. */
    reset();
    struct cfw_theme_selection pink = {.accent = CFW_THEME_ACCENT_PINK};
    assert(cfw_theme_switch_restore(&switcher, &pink, NULL, &baseline));
    assert(!stores && stock_but_accent());

    /* A stale baseline prefix fails before saving; damaged records recover. */
    cfw_theme_input_drained();
    files[1].data[4] ^= 1;
    assert(!cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_GREEN, &baseline));
    assert(switcher.result == CFW_THEME_LOAD_FAILED && !stores);
    assert(stock_but_accent() && cfw_theme_accent_get() == CFW_THEME_ACCENT_PINK);
    files[1].data[4] ^= 1;
    cfw_theme_input_drained();
    /* The accent asset's record follows asset 0's 24-byte record. */
    files[1].data[52 + CFW_THEME_STYLE_BYTES + 24 + 12 + 4] ^= 1;
    assert(!cfw_theme_switch_set_accent(&switcher, CFW_THEME_ACCENT_GREEN, &baseline));
    assert(switcher.result == CFW_THEME_RESTORE_FAILED);
    assert(switcher.assets[0] == 1u << ACCENT_ASSET && !cfw_theme_accent_get());
    assert(disk.version == 1 && !disk.selection.accent && opens == closes);
    cfw_theme_input_drained();
    make_baseline();
    assert(apply(&stock));
    check_stock();
    assert(!switcher.assets[0]);
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
    classic_accent();
    return 0;
}
