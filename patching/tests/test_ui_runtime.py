# SPDX-License-Identifier: GPL-3.0-only
"""Host unit checks for UI dispatch and native template enumeration, with mocked OSOS calls."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

PAYLOAD = Path(__file__).resolve().parents[2] / "payload"


@unittest.skipUnless(shutil.which("cc"), "Host C compiler not installed")
class UiRuntimeTests(unittest.TestCase):
    def test_selection_labels_and_multiple_text_pages(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("ui.c", "ui.h", "resources.c", "resources.h"):
                shutil.copyfile(PAYLOAD / name, root / name)
            (root / "patch.h").write_text(
                "#define PATCH_ARM\n#define PATCH_CALL(...)\n"
            )
            (root / "osos.h").write_text("""
#include <stdint.h>
#define OSOS_RESOURCE_BANK_INIT_OVERRIDES 0
struct osos_string { int unused; };
void *osos_resource_bank(void);
void *osos_resource_get(void *, uint32_t, uint32_t, uint32_t *);
void osos_menu_item_changed(uint32_t);
const void *osos_resource_next(void *, uint32_t *, uint32_t *);
void osos_string_init(struct osos_string *, const char *);
uint32_t *osos_resource_name_slot(struct osos_string *);
void osos_string_destroy(struct osos_string *);
void osos_resource_set(void *, uint32_t, uint32_t, const void *, uint32_t);
""")
            (root / "test.c").write_text("""
#include <assert.h>
#include <string.h>
#include "ui.h"
#include "resources.h"
#include "osos.h"
static uint32_t parent[100], selector[100], changed;
static const unsigned char first[] = {1}, second[] = {2}, stock[] = {3};
const struct cfw_resource cfw_resources[] = {
    {0x56696577, 10, 1, first}, {0x53747220, 11, 1, first},
    {0x56696577, 12, 1, second},
};
const uint32_t cfw_resource_count = 3, cfw_resource_name_count = 0;
const struct cfw_resource_name cfw_resource_names[] = {{"", 0}};
void *osos_resource_bank(void) { return 0; }
void *osos_resource_get(void *bank, uint32_t type, uint32_t id, uint32_t *size)
{ (void)bank; (void)type; *size = sizeof(parent); return id == 1 ? parent : selector; }
void osos_menu_item_changed(uint32_t item) { changed = item; }
const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{ (void)bank; if (*id) return 0; *id = 1; *size = 1; return stock; }
void osos_string_init(struct osos_string *s, const char *n) { (void)s; (void)n; }
uint32_t *osos_resource_name_slot(struct osos_string *s) { (void)s; return &changed; }
void osos_string_destroy(struct osos_string *s) { (void)s; }
void osos_resource_set(void *b, uint32_t t, uint32_t i, const void *d, uint32_t s)
{ (void)b; (void)t; (void)i; (void)d; (void)s; }
int main(void)
{
    const uint32_t labels[] = {100,101}, marked[] = {200,201};
    const uint32_t summaries[] = {300,301}, items[] = {400,401};
    const struct cfw_ui_field fields[] = {{1,0,50,2,2,0,labels,marked,summaries,items}};
    unsigned int field, value;
    assert(cfw_ui_selection("TEST_Set_", "TEST_Set_00_01", fields, 1, &field, &value));
    assert(field == 0 && value == 1);
    const char *invalid[] = {0,"", "TEST_Set_", "TEST_Set_00_", "TEST_Set_00_02",
                            "TEST_Set_01_00", "TEST_Set_00_01x", "TEST_Set_a0_01"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); i++)
        assert(!cfw_ui_selection("TEST_Set_", invalid[i], fields, 1, &field, &value));
    cfw_ui_refresh(fields, 1);
    assert(parent[(12+0x68)/4] == 301 && changed == 50);
    assert(selector[(12+0x68)/4] == 100 && selector[(12+0xa0+0x68)/4] == 201);
    const struct cfw_ui_action action = {1,0,60,2,labels};
    cfw_ui_status(&action, 1);
    assert(parent[(12+0x68)/4] == 101 && changed == 60);
    cfw_ui_status(&action, 2);
    assert(parent[(12+0x68)/4] == 101);
    uint32_t id = 0, size = 0;
    assert(cfw_next_template(0, &id, &size) == stock && id == 1);
    assert(cfw_next_template(0, &id, &size) == first && id == 10 && size == 1);
    assert(cfw_next_template(0, &id, &size) == second && id == 12 && size == 1);
    assert(cfw_next_template(0, &id, &size) == 0);
    return 0;
}
""")
            subprocess.run(
                [
                    "cc",
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "ui.c",
                    "resources.c",
                    "test.c",
                    "-o",
                    "test",
                ],
                cwd=root,
                check=True,
            )
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
