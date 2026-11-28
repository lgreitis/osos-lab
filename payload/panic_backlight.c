/* SPDX-License-Identifier: GPL-3.0-only */

/* Polling transfer sequence follows Rockbox's s5l8702 I2C driver,
 * firmware/target/arm/s5l8702/i2c-s5l8702.c (Copyright (C) 2009 by Bertrik
 * Sikken). Modified by the RepriseOS authors, 2026. Rockbox is
 * GPL-2.0-or-later; this version is GPL-3.0-only.
 *
 * Only LEDENA is written: brightness, overvoltage protection, current limit
 * and power rails retain their values. */
#include "native/bindings.h"
#include "panic.h"
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(address))
#define I2C_CONTROL REG32(OSOS_I2C0_CONTROLLER)
#define I2C_STATUS REG32(OSOS_I2C0_CONTROLLER + 0x04)
#define I2C_DATA REG32(OSOS_I2C0_CONTROLLER + 0x0c)
#define I2C_BUSY REG32(OSOS_I2C0_CONTROLLER + 0x10)
#define I2C_TRANSFER_STATUS REG32(OSOS_I2C0_CONTROLLER + 0x20)
#define CLOCK_GATES REG32(OSOS_CLOCK_GATES_1)
#define CLOCK_GATE_I2C0 0x10u
#define I2C_CONTROL_ACK 0x80u
#define I2C_CONTROL_INTERRUPTS 0x3f00u
#define I2C_CONTROL_CONTINUE 0x10u
#define I2C_STATUS_MASTER_TRANSMIT 0xc0u
#define I2C_STATUS_START 0x30u
#define I2C_STATUS_BUSY 0x20u
#define I2C_STATUS_NACK 0x1u
#define I2C_BYTE_DONE 0x100u
#define I2C_TRANSFER_DONE 0x2100u
#define PMU_WRITE_ADDRESS 0xe6u
#define PMU_BACKLIGHT_ENABLE 0x29u
#define PMU_BACKLIGHT_ON 0x1u

#define POLL_LIMIT 100000u

static int ready(void)
{
    for (unsigned n = 0; n < POLL_LIMIT; n++)
        if (!I2C_BUSY)
            return 1;
    return 0;
}

static int completed(void)
{
    for (unsigned n = 0; n < POLL_LIMIT; n++) {
        if (I2C_BUSY)
            continue;
        if (!(I2C_STATUS & I2C_STATUS_BUSY) || (I2C_TRANSFER_STATUS & I2C_BYTE_DONE)) {
            I2C_TRANSFER_STATUS |= I2C_TRANSFER_DONE;
            return 1;
        }
    }
    return 0;
}

static int settle(void)
{
    typedef uint32_t (*clock_fn)(void);
    clock_fn microseconds = (clock_fn)OSOS_CLOCK_GET_MICROSECONDS;
    uint32_t start = microseconds();
    for (unsigned n = 0; n < POLL_LIMIT; n++)
        if ((uint32_t)(microseconds() - start) >= 5)
            return 1;
    return 0;
}

/* Report result: 0 not attempted, 1 ACKed, 2 busy transaction, 3 controller
 * busy timeout, 4 transfer timeout, 5 NACK, 6 stopped timer, 7 STOP timeout.
 * The upper byte identifies a failed address/register/value stage (1/2/3). */
static unsigned send_byte(uint8_t value)
{
    if (!ready())
        return 3;
    I2C_DATA = value;
    if (!settle())
        return 6;
    if (!ready())
        return 3;
    I2C_CONTROL = I2C_CONTROL;
    if (!completed())
        return 4;
    return (I2C_STATUS & I2C_STATUS_NACK) ? 5 : 1;
}

void cfw_panic_backlight(uint32_t result[4])
{
    result[0] = 3;
    result[1] = CLOCK_GATES;
    result[2] = result[3] = 0;
    /* Rockbox gates the I2C clock around transfers. IRQ/FIQ are masked here;
     * leave this controller clock running until reset, without changing any
     * other clock gate. Never take over an interrupted PMU transaction. */
    CLOCK_GATES &= ~CLOCK_GATE_I2C0;
    if (!ready())
        return;
    result[2] = I2C_STATUS;
    result[3] = I2C_CONTROL;
    if (I2C_STATUS & I2C_STATUS_BUSY) {
        result[0] = 2;
        return;
    }
    /* Apple's driver consumes byte completion but can leave STOP pending. */
    I2C_TRANSFER_STATUS |= I2C_TRANSFER_DONE;
    if (!ready())
        return;
    I2C_CONTROL = (I2C_CONTROL & ~I2C_CONTROL_INTERRUPTS) | I2C_CONTROL_ACK;
    if (!ready())
        return;
    I2C_STATUS = I2C_STATUS_MASTER_TRANSMIT;
    if (!ready())
        return;
    I2C_DATA = PMU_WRITE_ADDRESS;
    if (!ready())
        return;
    I2C_STATUS = I2C_STATUS_MASTER_TRANSMIT | I2C_STATUS_START;
    unsigned status = !completed() ? 4 : (I2C_STATUS & I2C_STATUS_NACK) ? 5 : 1;
    unsigned stage = 1;
    if (status == 1) {
        stage = 2;
        status = send_byte(PMU_BACKLIGHT_ENABLE);
    }
    if (status == 1) {
        stage = 3;
        status = send_byte(PMU_BACKLIGHT_ON);
    }
    result[0] = status == 1 ? 1 : status | stage << 8;
    if (!ready())
        return;
    I2C_STATUS &= ~I2C_STATUS_BUSY;
    if (!ready())
        return;
    I2C_CONTROL = I2C_CONTROL_CONTINUE;
    if (!completed())
        result[0] = 7;
}
