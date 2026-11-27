/* SPDX-License-Identifier: MIT */

/* The iPod's player through the host's Audio interface. A command is an event
 * the host's UI task applies later; the host predicts the playback state and
 * the track count at once and answers the rest from the player, so those lag
 * until the UI task has run. The host snapshots the player's settings at
 * launch and restores them when the app ends, except the volume and an EQ
 * set with set_eq. */

#include "osos-2.0.5.h"
#include "eapp.h"
#include "eapp_abi.h"

_Static_assert(EAPP_MUSIC_STOPPED == GamePlaybackState_Stopped, "stopped");
_Static_assert(EAPP_MUSIC_PLAYING == GamePlaybackState_Playing, "playing");
_Static_assert(EAPP_MUSIC_PAUSED == GamePlaybackState_Paused, "paused");
_Static_assert(EAPP_MUSIC_REPEAT_OFF == GameRepeatMode_Off, "repeat off");
_Static_assert(EAPP_MUSIC_REPEAT_ONE == GameRepeatMode_One, "repeat one");
_Static_assert(EAPP_MUSIC_REPEAT_ALL == GameRepeatMode_All, "repeat all");
_Static_assert(EAPP_MUSIC_SHUFFLE_OFF == GamePlaybackOrder_Off, "shuffle off");
_Static_assert(EAPP_MUSIC_SHUFFLE_SONGS == GamePlaybackOrder_Songs, "shuffle songs");
_Static_assert(EAPP_MUSIC_SHUFFLE_ALBUMS == GamePlaybackOrder_Albums, "shuffle albums");
_Static_assert(EAPP_MUSIC_VOLUME_MAX == GameAudio_VolumeMax, "volume");
_Static_assert(EAPP_BUTTON_NEXT == GameButton_Next && EAPP_BUTTON_PREVIOUS == GameButton_Previous &&
                   EAPP_BUTTON_PLAY == GameButton_Play,
               "buttons");
_Static_assert(EAPP_INPUT_RELEASE == GameButtonEvent_Release, "release");

enum eapp_music_state eapp_music_state(void)
{
    return (enum eapp_music_state)GameAudio_GetPlaybackState();
}

int eapp_music_has_playlist(void)
{
    return GameAudio_HasPlaybackList() != 0;
}

void eapp_music_play_pause(void)
{
    GameAudio_TogglePlayPause(0);
}

void eapp_music_stop(void)
{
    GameAudio_Stop();
}

void eapp_music_next(void)
{
    GameAudio_NextTrack();
}

void eapp_music_previous(void)
{
    GameAudio_PreviousTrack();
}

void eapp_music_play_track(uint32_t index)
{
    if (index < GameAudio_GetEntryCount())
        GameAudio_PlayIndex(index, 0, 0);
}

void eapp_music_seek(uint32_t milliseconds)
{
    GameAudio_Seek(milliseconds);
}

void eapp_music_scan_forward(void)
{
    GameAudio_StartScanForward();
}

void eapp_music_scan_backward(void)
{
    GameAudio_StartScanBackward();
}

void eapp_music_scan_stop(void)
{
    GameAudio_StopScan();
}

int eapp_music_scanning(void)
{
    if (GameAudio_IsScanningForward())
        return 1;
    return GameAudio_IsScanningBackward() ? -1 : 0;
}

uint32_t eapp_music_position(void)
{
    return GameAudio_GetPosition();
}

uint32_t eapp_music_duration(void)
{
    return GameAudio_GetPosition() + GameAudio_GetRemainingPosition();
}

uint32_t eapp_music_track_index(void)
{
    return GameAudio_GetCurrentIndex();
}

uint32_t eapp_music_track_count(void)
{
    return GameAudio_GetEntryCount();
}

enum eapp_music_repeat eapp_music_repeat_mode(void)
{
    return (enum eapp_music_repeat)GameAudio_GetRepeatMode();
}

void eapp_music_set_repeat_mode(enum eapp_music_repeat mode)
{
    GameAudio_SetRepeatMode((uint8_t)mode);
}

enum eapp_music_shuffle eapp_music_shuffle_mode(void)
{
    return (enum eapp_music_shuffle)GameAudio_GetPlaybackOrder();
}

void eapp_music_set_shuffle_mode(enum eapp_music_shuffle mode)
{
    GameAudio_SetPlaybackOrder((uint8_t)mode);
}

uint32_t eapp_music_volume(void)
{
    return GameAudio_GetVolume();
}

uint32_t eapp_music_volume_limit(void)
{
    return GameAudio_GetVolumeLimit();
}

void eapp_music_set_volume(uint32_t volume)
{
    uint32_t limit = GameAudio_GetVolumeLimit();
    GameAudio_SetVolume(volume < limit ? volume : limit);
}

uint32_t eapp_music_eq(void)
{
    return GameAudio_GetEQMenuIndex();
}

void eapp_music_set_eq_until_exit(uint32_t index)
{
    GameAudio_SetEQMenuIndexTemporary(index);
}

void eapp_music_set_eq(uint32_t index)
{
    GameAudio_SetEQMenuIndex(index);
}

uint32_t eapp_music_speed(void)
{
    return (uint32_t)GameAudio_GetSpeed();
}

void eapp_music_set_speed(uint32_t per_mille)
{
    if (per_mille >= EAPP_MUSIC_SPEED_MIN && per_mille <= EAPP_MUSIC_SPEED_MAX)
        GameAudio_SetSpeed((int)per_mille);
}

int32_t eapp_music_gain(void)
{
    return GameAudio_GetGain();
}

void eapp_music_set_gain(int32_t gain)
{
    if (gain < -EAPP_MUSIC_GAIN_MAX)
        gain = -EAPP_MUSIC_GAIN_MAX;
    else if (gain > EAPP_MUSIC_GAIN_MAX)
        gain = EAPP_MUSIC_GAIN_MAX;
    GameAudio_SetGain(gain);
}

void eapp_music_pass_button(const struct eapp_input_event *event)
{
    if (!event || event->type != EAPP_INPUT_RELEASE ||
        (event->button != EAPP_BUTTON_PLAY && event->button != EAPP_BUTTON_NEXT &&
         event->button != EAPP_BUTTON_PREVIOUS))
        return;
    struct eapp_abi_input_event native = {
        .button = event->button,
        .type = event->type,
        .timestamp = event->timestamp_ms,
    };
    GameInput_ForwardButtonEvent((const struct GameInputEvent *)&native);
}
