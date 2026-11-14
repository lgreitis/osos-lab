/* SPDX-License-Identifier: GPL-3.0-only */

#include "sound.h"
#include "native/audio-layout.h"
#include "patch.h"

/* The playback task calls this with the buffer it is about to submit. The
 * native gain stage already scales it in place, so it is writable. */
PATCH_ARM void cfw_sound_process(int16_t *samples, uint32_t frames, const void *effects)
{
    const unsigned char *context = effects;
    uint32_t rate = *(const uint32_t *)(context + OSOS_AUDIO_OUTPUT_RATE_OFFSET);
    uint32_t bits = *(const uint32_t *)(context + OSOS_AUDIO_OUTPUT_BITS_OFFSET);
    uint32_t channels =
        *(const uint32_t *)(context + OSOS_AUDIO_OUTPUT_CHANNELS_OFFSET);
    if (!frames || bits != 16 || channels != 2 || rate < 8000 || rate > 48000)
        return;
    cfw_crossfeed_process(samples, frames, rate);
}
