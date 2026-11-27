/* SPDX-License-Identifier: GPL-3.0-only */
/* Every backlight poll is bounded, also when the hardware stalls. */
#include <assert.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"

static uint32_t i2c[9], clock_gates;
static uint32_t ticks;
static int clock_stopped;

static uint32_t fake_clock(void)
{
    return clock_stopped ? ticks : ticks++;
}

#undef OSOS_I2C0_CONTROLLER
#define OSOS_I2C0_CONTROLLER ((uintptr_t)i2c)
#undef OSOS_CLOCK_GATES_1
#define OSOS_CLOCK_GATES_1 ((uintptr_t)&clock_gates)
#undef OSOS_CLOCK_GET_MICROSECONDS
#define OSOS_CLOCK_GET_MICROSECONDS ((uintptr_t)fake_clock)
#include "../../../payload/panic_backlight.c"

int main(void)
{
    uint32_t result[4];
    clock_gates = CLOCK_GATE_I2C0 | 0x80000000;
    i2c[4] = 1;
    i2c[3] = 0xfeed;
    cfw_panic_backlight(result); /* controller stays busy */
    assert(i2c[3] == 0xfeed);
    assert(clock_gates == 0x80000000);
    assert(result[0] == 3);
    i2c[4] = 0;
    clock_gates = 0;
    i2c[1] = I2C_STATUS_BUSY;
    cfw_panic_backlight(result); /* an interrupted transaction is left alone */
    assert(i2c[3] == 0xfeed && result[0] == 2);
    i2c[1] = 0;
    i2c[4] = 0;
    cfw_panic_backlight(result); /* a pending STOP is consumed first */
    assert(result[0] == 1 && !(i2c[1] & I2C_STATUS_BUSY));
    i2c[8] = I2C_TRANSFER_DONE;
    clock_stopped = 1;
    cfw_panic_backlight(result); /* frozen clock: bounded, then STOP */
    assert(result[0] == (6 | 2 << 8) && !(i2c[1] & I2C_STATUS_BUSY));
    assert(i2c[3] == PMU_BACKLIGHT_ENABLE);
    clock_stopped = 0;
    cfw_panic_backlight(result);
    assert(i2c[3] == PMU_BACKLIGHT_ON);
    assert(!(i2c[1] & I2C_STATUS_BUSY));
    return 0;
}
