/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_UI_MACROS_H
#define CFW_UI_MACROS_H

/* Build-only, little-endian records consumed by patching/ui.py. No pointers. */
struct ui_record {
    int kind, numbers[5];
    char name[64], text[96], reference[64];
};

_Static_assert(sizeof(struct ui_record) == 248, "UI record layout changed");

#define UI_VALUES(id) {1, {0}, #id, "", ""},
#define UI_VALUE(value, label) {2, {value}, "", label, ""},
#define UI_END_VALUES() {4, {0}, "", "", ""},
/* Values are integer units divided by scale, which must be a power of ten. */
#define UI_RANGE(id, first, last, step, scale, suffix, sign)                           \
    {3, {first, last, step, scale, sign}, #id, suffix, ""},
#define UI_MENU(id, title) {5, {0}, #id, title, ""},
#define UI_END_MENU() {6, {0}, "", "", ""},
/* slot is a persistent settings index; changing menu order preserves it. */
#define UI_SELECTOR(id, title, values, slot, default_value)                            \
    {7, {slot, default_value}, #id, title, #values},
#define UI_ACTION(id, title, labels) {8, {0}, #id, title, #labels},
#define UI_SETTINGS_ENTRY(menu, after_item, open_action)                               \
    {9, {after_item}, #menu, "", open_action},

#define UI_TEXT(id, title, file) {10, {0}, #id, title, file},
#define UI_STRING(id, text) {11, {0}, #id, text, ""},

#endif
