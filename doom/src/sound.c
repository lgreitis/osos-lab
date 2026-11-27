/* SPDX-License-Identifier: GPL-3.0-only */

/* Doom's sound effects on the eApp SDK's sounds. A DMX lump is an 8-byte
 * header (format 3, rate, length) and 8-bit unsigned mono samples, of which
 * DMX skips the first and last 16 bytes; a play copies them, flipped to
 * signed, into a PU_CACHE zone block owned by the sfxinfo, which the zone
 * may purge for other data. Each of Doom's channels plays on a sound of its
 * own, made from that block when the channel starts. The host reads the
 * samples while a voice plays, so a block is PU_STATIC while a channel holds
 * it. */

#include "eapp.h"

#include <stdio.h>
#include <string.h>

#include "deh_str.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

/* Bound by i_sound.c under FEATURE_SOUND; nothing resamples here. */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

struct sfx_pcm {
    uint32_t rate, length;
    uint32_t holders; /* Channels playing the block. */
    uint8_t samples[];
};

static struct eapp_sound *channels[8];
static struct sfx_pcm *channel_pcm[8];
static boolean sfx_prefix;

static boolean sound_init(boolean use_sfx_prefix)
{
    sfx_prefix = use_sfx_prefix;
    return true;
}

/* Frees the channel's sound and lets the zone purge its block again. */
static void release(int channel)
{
    eapp_sound_free(channels[channel]);
    channels[channel] = NULL;
    struct sfx_pcm *pcm = channel_pcm[channel];
    if (pcm && !--pcm->holders)
        Z_ChangeTag(pcm, PU_CACHE);
    channel_pcm[channel] = NULL;
}

static void sound_shutdown(void)
{
    for (int i = 0; i < 8; ++i)
        release(i);
}

static int sound_lump(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link)
        sfx = sfx->link;
    snprintf(name, sizeof name, sfx_prefix ? "ds%s" : "%s", DEH_String(sfx->name));
    return W_GetNumForName(name);
}

static struct sfx_pcm *sound_pcm(sfxinfo_t *sfx)
{
    if (sfx->driver_data)
        return sfx->driver_data;
    unsigned lump_size = W_LumpLength(sfx->lumpnum);
    const uint8_t *lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    uint32_t length = lump_size >= 8 ? lump[4] | lump[5] << 8 | lump[6] << 16 |
                                           (uint32_t)lump[7] << 24
                                     : 0;
    struct sfx_pcm *pcm = NULL;
    if (lump_size >= 8 && lump[0] == 3 && lump[1] == 0 && length <= lump_size - 8 &&
        length > 48) {
        pcm = Z_Malloc(sizeof *pcm + length - 32, PU_CACHE, &sfx->driver_data);
        pcm->rate = lump[2] | lump[3] << 8;
        pcm->length = length - 32;
        pcm->holders = 0;
        for (uint32_t i = 0; i < pcm->length; ++i)
            pcm->samples[i] = lump[8 + 16 + i] ^ 0x80;
    }
    W_ReleaseLumpNum(sfx->lumpnum);
    return pcm;
}

static void sound_params(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= 8 || !channels[channel])
        return;
    eapp_sound_set_volume(channels[channel], (uint32_t)vol * EAPP_SOUND_VOLUME_MAX / 127);
    int pan = sep < 1 ? -127 : sep > 255 ? 127 : sep - 128; /* NORM_SEP is 128. */
    eapp_sound_set_pan(channels[channel], pan * EAPP_SOUND_PAN_MAX / 127);
}

static int sound_start(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= 8)
        return -1;
    release(channel);
    struct sfx_pcm *pcm = sound_pcm(sfx);
    if (!pcm)
        return -1;
    if (!pcm->holders++)
        Z_ChangeTag(pcm, PU_STATIC);
    channel_pcm[channel] = pcm;
    channels[channel] = eapp_sound_create(pcm->samples, pcm->length, pcm->rate, 1, 8);
    if (!channels[channel]) {
        release(channel);
        return -1;
    }
    sound_params(channel, vol, sep);
    eapp_sound_play(channels[channel]);
    return channel;
}

static void sound_stop(int channel)
{
    if (channel >= 0 && channel < 8)
        release(channel);
}

static boolean sound_playing(int channel)
{
    return channel >= 0 && channel < 8 && eapp_sound_playing(channels[channel]);
}

static void sound_update(void)
{
    for (int i = 0; i < 8; ++i)
        if (channels[i] && !eapp_sound_playing(channels[i]))
            release(i);
}

static void sound_cache(sfxinfo_t *sounds, int count)
{
    (void)sounds;
    (void)count;
}

static snddevice_t sound_devices[] = {
    SNDDEVICE_SB,   SNDDEVICE_PAS,         SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    sound_devices, sizeof sound_devices / sizeof sound_devices[0],
    sound_init,    sound_shutdown,
    sound_lump,    sound_update,
    sound_params,  sound_start,
    sound_stop,    sound_playing,
    sound_cache,
};

/* Music stays off: the port starts Doom with -nomusic. */
music_module_t DG_music_module;
