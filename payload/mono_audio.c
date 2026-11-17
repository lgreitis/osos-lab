/* SPDX-License-Identifier: GPL-3.0-only */

#include "sound.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"
#include "store.h"
#include "mono_audio_ui.h"
#include "resources.h"

enum {
	MONO_AUDIO_OFF_RECORD = 0x304f4e4d, /* MNO0 */
	MONO_AUDIO_ON_RECORD = 0x314f4e4d, /* MNO1 */
};

static volatile int enabled;

PATCH_ARM void cfw_mono_audio_process(int16_t *samples, uint32_t frames)
{
	/* Returns to avoid processing if mono audio is not enabled */
	if (!enabled) return;
	
	/* Loops through array of samples on a frame-by-frame basis */
    for (int16_t *sample = samples, *end = samples + frames * 2; sample < end; sample += 2) {
		/* Computes the downmixed sample and assigns the new value to both channels */
		sample[0] = (int16_t) (((int32_t) sample[0] + sample[1]) / 2);
		sample[1] = sample[0];
	}
}

void cfw_mono_audio_load(void)
{
    uint32_t record = 0;
	cfw_store_get(CFW_STORE_MONO_AUDIO, &record, sizeof(record));
	enabled = record == MONO_AUDIO_ON_RECORD;
}

void cfw_mono_audio_reset(void)
{
	const uint32_t record = MONO_AUDIO_OFF_RECORD;
	enabled = 0;
	cfw_store_set(CFW_STORE_MONO_AUDIO, &record, sizeof(record));
}

int cfw_mono_audio_property(uint32_t type, uint32_t property, uintptr_t *out)
{
	if (type == OSOS_RESOURCE_BMAP && property == CFW_MonoAudio_PreviewImage) {
		*out = enabled ? (uintptr_t) cfw_resource_bitmap(CFW_MonoAudio_Icon) : (uintptr_t) cfw_resource_bitmap(CFW_StereoAudio_Icon);
		return 1;
	}

	if (type != OSOS_PROPERTY_STRING)
		return 0;
	
	if (property == CFW_MonoAudio_Value) {
		*out = osos_settings_on_off(enabled);
		return 1;
	}
	
	if (property == CFW_MonoAudio_PreviewText) {
		*out = osos_ui_string(enabled ? CFW_MonoAudio_OnLabel : CFW_MonoAudio_OffLabel);
		return 1;
	}

	return 0;
}

/* Toggles the active state of the mono audio setting */
int cfw_mono_audio_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_MonoAudio_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    enabled = !enabled;
	uint32_t record = enabled ? MONO_AUDIO_ON_RECORD : MONO_AUDIO_OFF_RECORD;
	cfw_store_set(CFW_STORE_MONO_AUDIO, &record, sizeof(record));
	osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL, CFW_MonoAudio_Value);
	osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL, CFW_MonoAudio_PreviewImage);
	osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL, CFW_MonoAudio_PreviewText);
	return 1;
}
