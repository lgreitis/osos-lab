/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Meier crossfeed, ported from Rockbox lib/rbcodec/dsp/crossfeed.c.
 *
 * Copyright (C) 2006 Thom Johansen
 * Copyright (C) 2010 Bertrik Sikken
 * Copyright (C) 2012 Michael Sevakis
 *
 * Modified for RepriseOS: runs on the native stereo S16 output in place and
 * keeps only the Meier mode, with an Off/On setting in Settings.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "sound.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"
#include "store.h"
#include "crossfeed_ui.h"

enum {
    CROSSFEED_OFF_RECORD = 0x30465843, /* CXF0 */
    CROSSFEED_ON_RECORD = 0x31465843,  /* CXF1 */
    /* Rockbox's s3.28 sample format: full scale S16 sits at 1 << 28. */
    SAMPLE_SHIFT = 13,
};

static volatile int enabled;

/* Owned by the playback task. */
static struct {
    int32_t vcl, vcr, vdiff, coef1, coef2;
    uint32_t rate;
    int active;
} state;


/* numerator / rate as s0.31 for numerator < rate <= 65535, without a 64-bit
 * division. */
static int32_t ratio_s31(uint32_t numerator, uint32_t rate)
{
    uint32_t high = (numerator << 15) / rate;
    uint32_t remainder = (numerator << 15) % rate;
    return (int32_t)((high << 16) + (remainder << 16) / rate);
}

static inline int32_t fracmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 31);
}

static inline int16_t output(int32_t sample)
{
    int32_t value = (sample + (1 << (SAMPLE_SHIFT - 1))) >> SAMPLE_SHIFT;
    return value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
}

/* Jan Meier's passive crossfeed circuit:
 * http://www.meier-audio.homepage.t-online.de/passivefilter.htm */
PATCH_ARM void cfw_crossfeed_process(int16_t *samples, uint32_t frames, uint32_t rate)
{
    if (!enabled) {
        state.active = 0;
        return;
    }
    if (!state.active || state.rate != rate) {
        /* 1 / (F.Rforward.C) and 1 / (F.Rcross.C) as s0.31. */
        state.coef1 = ratio_s31(2128, rate);
        state.coef2 = ratio_s31(1000, rate);
        state.vcl = state.vcr = state.vdiff = 0;
        state.rate = rate;
        state.active = 1;
    }
    int32_t vcl = state.vcl, vcr = state.vcr, vdiff = state.vdiff;
    const int32_t coef1 = state.coef1, coef2 = state.coef2;
    for (int16_t *sample = samples, *end = samples + frames * 2; sample < end;
         sample += 2) {
        int32_t lout = (int32_t)sample[0] * (1 << SAMPLE_SHIFT) + vcl;
        int32_t rout = (int32_t)sample[1] * (1 << SAMPLE_SHIFT) + vcr;
        sample[0] = output(lout);
        sample[1] = output(rout);

        int32_t common = fracmul(vdiff, coef2);
        vcl -= fracmul(vcl, coef1) + common;
        vcr -= fracmul(vcr, coef1) - common;
        vdiff = lout - rout;
    }
    state.vcl = vcl;
    state.vcr = vcr;
    state.vdiff = vdiff;
}

void cfw_crossfeed_load(void)
{
    uint32_t record = 0;
    cfw_store_get(CFW_STORE_CROSSFEED, &record, sizeof(record));
    enabled = record == CROSSFEED_ON_RECORD;
}

void cfw_crossfeed_reset(void)
{
    const uint32_t record = CROSSFEED_OFF_RECORD;
    enabled = 0;
    cfw_store_set(CFW_STORE_CROSSFEED, &record, sizeof(record));
}

int cfw_crossfeed_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_Crossfeed_Value)
        return 0;
    *out = osos_settings_on_off(enabled);
    return 1;
}

int cfw_crossfeed_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_Crossfeed_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    enabled = !enabled;
    uint32_t record = enabled ? CROSSFEED_ON_RECORD : CROSSFEED_OFF_RECORD;
    cfw_store_set(CFW_STORE_CROSSFEED, &record, sizeof(record));
    osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                      CFW_Crossfeed_Value);
    return 1;
}
