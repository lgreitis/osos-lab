/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/time.c (8fc840d) by lgreitis. */

#include "osos-2.0.5.h"
#include "eapp.h"

static uint32_t start_us, last_us, elapsed_ms, remainder_us;

/* The tick clock counts from the runtime's init by adding up what the host's
 * 32-bit microsecond counter moved between reads, so it survives the
 * counter's wrap. */
void eapp_clock_start(void)
{
    Clock_ReadMicroseconds(&start_us);
    last_us = start_us;
    elapsed_ms = 0;
    remainder_us = 0;
}

uint32_t eapp_clock_seed(void)
{
    return start_us;
}

static uint32_t ticks_at(uint32_t now)
{
    uint32_t delta = now - last_us;
    last_us = now;
    elapsed_ms += delta / 1000;
    remainder_us += delta % 1000;
    elapsed_ms += remainder_us / 1000;
    remainder_us %= 1000;
    return elapsed_ms;
}

uint32_t eapp_ticks_ms(void)
{
    uint32_t now;
    Clock_ReadMicroseconds(&now);
    return ticks_at(now);
}

/* The host stamps an event with its microsecond counter over 1000. The
 * stamp's age is taken in the counter's own 32-bit domain, so it is right
 * across the counter's wrap, and the same age back from the tick clock lands
 * within a millisecond. */
uint32_t eapp_clock_stamp_ms(uint32_t host_ms)
{
    uint32_t now;
    Clock_ReadMicroseconds(&now);
    return ticks_at(now) - (now - host_ms * 1000u) / 1000;
}

void eapp_sleep_ms(uint32_t milliseconds)
{
    GameSystem_Sleep(milliseconds);
}

void eapp_date_time(struct eapp_date_time *out)
{
    /* Second, minute, hour, day, month, year, in the host's order. */
    uint32_t fields[6] = {0};
    if (!out)
        return;
    GameSystem_GetDateTime(fields);
    out->second = (uint8_t)fields[0];
    out->minute = (uint8_t)fields[1];
    out->hour = (uint8_t)fields[2];
    out->day = (uint8_t)fields[3];
    out->month = (uint8_t)fields[4];
    out->year = (uint16_t)fields[5];
}

int eapp_clock_24h(void)
{
    /* The host answers "12" or "24" with the terminator. */
    char format[3] = {0};
    uint32_t size = sizeof format;
    return GameSettings_GetValue("TimeFormat", format, &size) == 0 && format[0] == '2' &&
           format[1] == '4';
}

static char *two_digits(char *out, unsigned value)
{
    *out++ = (char)('0' + value / 10 % 10);
    *out++ = (char)('0' + value % 10);
    return out;
}

char *eapp_format_time(char *out, const struct eapp_date_time *time)
{
    if (!out)
        return NULL;
    if (!time) {
        *out = 0;
        return out;
    }
    unsigned hour = time->hour % 24, minute = time->minute % 60;
    int clock_24h = eapp_clock_24h();
    if (clock_24h) {
        out = two_digits(out, hour);
    } else {
        unsigned shown = hour % 12 ? hour % 12 : 12;
        if (shown >= 10)
            *out++ = '1';
        *out++ = (char)('0' + shown % 10);
    }
    *out++ = ':';
    out = two_digits(out, minute);
    if (!clock_24h) {
        *out++ = ' ';
        *out++ = hour < 12 ? 'A' : 'P';
        *out++ = 'M';
    }
    *out = 0;
    return out;
}
