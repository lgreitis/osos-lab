/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "../../../payload/ui_visibility.h"

static uint32_t visible[3];
const struct cfw_ui_visibility cfw_ui_visibility[] = {
    {101, 1, {11, 21}, {2, 3}, 2, "a", &visible[0]},
    {202, 2, {12, 22}, {2, 3}, 1, "b", &visible[1]},
    {303, 3, {13, 23}, {3, 1}, 0, "c", &visible[2]},
};
const uint32_t cfw_ui_visibility_count = 3;

int main(void)
{
    const uint32_t rows[] = {1, 0, 0, 0, 0, 2,  0, 0, 0, 0,
                             3, 0, 0, 0, 0, 99, 0, 0, 0, 0};
    const uint32_t expected[2][7] = {
        {0, 1, CFW_UI_CUSTOM_ROW, CFW_UI_CUSTOM_ROW | 1, 2, CFW_UI_CUSTOM_ROW | 2, 3},
        {0, CFW_UI_CUSTOM_ROW | 2, 1, 2, CFW_UI_CUSTOM_ROW, CFW_UI_CUSTOM_ROW | 1, 3},
    };
    for (unsigned menu = 0; menu < 2; menu++) {
        assert(cfw_ui_visibility_extra_count(menu, rows, 4) == 3);
        assert(cfw_ui_visibility_extra_count(menu, rows + 15, 1) == 0);
        for (uint32_t row = 0; row < 7; row++)
            assert(cfw_ui_visibility_index(menu, row, rows, 4) == expected[menu][row]);
        assert(cfw_ui_visibility_index(menu, 7, rows, 4) == UINT32_MAX);
    }
    for (uint32_t bits = 0; bits < 4; bits++) {
        struct cfw_ui_visibility_file file = cfw_ui_visibility_encode(101, bits);
        assert(cfw_ui_visibility_valid(&file, 101));
        assert(!cfw_ui_visibility_valid(&file, 202));
        file.visible ^= 1;
        assert(!cfw_ui_visibility_valid(&file, 101));
        file = cfw_ui_visibility_encode(101, bits);
        file.version++;
        assert(!cfw_ui_visibility_valid(&file, 101));
    }
    struct cfw_ui_visibility_file invalid = cfw_ui_visibility_encode(101, 4);
    assert(!cfw_ui_visibility_valid(&invalid, 101));
    return 0;
}
