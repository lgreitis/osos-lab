#include "game_api.h"
#include "native.h"

struct game_wheel game_read_wheel(void)
{
    int32_t delta = 0;
    uint32_t flags = 0;

    osos_read_wheel(&delta, &flags);

    return (struct game_wheel){
        .delta = delta,
        .position = flags & 0xff,
        .touched = !!(flags & (1u << 30)),
    };
}

int game_hold_locked(void)
{
    uint8_t value = 0xff;
    uint32_t size = 1;

    if (osos_read_setting("HoldSwitch", &value, &size) != 0 || size != 1 || value > 1)
        return -1;
    return value;
}
