#include <assert.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"

static uint32_t provider[17];
static uint32_t languages[OSOS_UI_LANGUAGE_COUNT * 3];
static int ready;

static void *lookup(uint32_t service)
{
    assert(service == 0x80);
    return ready ? provider : 0;
}

#undef OSOS_UI_SERVICE_GET
#undef OSOS_UI_LANGUAGE_TABLE
#define OSOS_UI_SERVICE_GET lookup
#define OSOS_UI_LANGUAGE_TABLE languages
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert

int main(void)
{
    assert(osos_ui_language() == -1);
    for (int index = 0; index < OSOS_UI_LANGUAGE_COUNT; index++)
        languages[index * 3 + 1] = 0x1000 + index;
    languages[1] = 0x656e5553;
    languages[4 * 3 + 1] = 0x64654445;
    ready = 1;
    for (int index = 0; index < OSOS_UI_LANGUAGE_COUNT; index++) {
        provider[OSOS_UI_LOCALE_OFFSET / 4] = languages[index * 3 + 1];
        assert(osos_ui_language() == index);
    }
    provider[OSOS_UI_LOCALE_OFFSET / 4] = 0x64654445;
    assert(osos_ui_language() == 4);
    provider[OSOS_UI_LOCALE_OFFSET / 4] = 0x656e5553;
    assert(osos_ui_language() == 0);
    provider[OSOS_UI_LOCALE_OFFSET / 4] = 0;
    assert(osos_ui_language() == -1);
    ready = 0;
    assert(osos_ui_language() == -1);
    return 0;
}