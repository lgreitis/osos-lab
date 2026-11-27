/* SPDX-License-Identifier: MIT */

/* The host's device services: the backlight, the power source, the platform,
 * the user's language, the package name and the debug console. */

#include "osos-2.0.5.h"
#include "eapp.h"

_Static_assert(EAPP_PLATFORM_IPOD_VIDEO == GamesPlatform_IPodVideo, "video");
_Static_assert(EAPP_PLATFORM_NANO_3G == GamesPlatform_Nano3G, "nano");
_Static_assert(EAPP_PLATFORM_CLASSIC == GamesPlatform_Classic, "classic");

void eapp_set_backlight_level(uint32_t percent)
{
    GameSystem_SetBacklightLevel(percent > GameBacklight_LevelMax ? GameBacklight_LevelMax
                                                                  : (int)percent);
}

void eapp_set_backlight_dimmed(int on)
{
    GameSystem_SetBacklightDimmed(on != 0);
}

int eapp_external_power(void)
{
    return GameSystem_IsExternalPowerConnected() != 0;
}

/* The host reports the battery in 22 steps and lets the reported level fall
 * by at most one step per call. Asking at most once a second keeps that fall
 * to a step per second. Answers 0 to 100, or -1 when the host has no reading. */
#define BATTERY_STEPS 22
#define BATTERY_HOLD_MS 1000

int eapp_battery_level(void)
{
    static uint32_t read_ms;
    static int level, asked;
    uint32_t now = eapp_ticks_ms();
    if (!asked || now - read_ms >= BATTERY_HOLD_MS) {
        int gauge = GameSystem_GetBatteryLevel();
        read_ms = now;
        asked = 1;
        level = gauge < 0 || gauge > BATTERY_STEPS ? -1 : gauge * 100 / BATTERY_STEPS;
    }
    return level;
}

enum eapp_platform eapp_platform(void)
{
    uint32_t platform = GameSystem_GetGamesPlatformId();
    return platform <= EAPP_PLATFORM_CLASSIC ? (enum eapp_platform)platform
                                             : EAPP_PLATFORM_UNKNOWN;
}

/* The host's languages in the order of its Language setting, which it
 * reports as an index. */
static const char *const languages[] = {
    "en_US", "da_DK", "de_CH", "de_DE", "es_ES", "fi_FI", "fr_BE", "fr_CH", "fr_FR",
    "it_IT", "ja_JP", "ko_KR", "nl_BE", "nl_NL", "no_NO", "pt_PT", "sv_SE", "zh_CN",
    "zh_TW", "cs_CZ", "el_GR", "hu_HU", "pl_PL", "ru_RU", "tr_TR",
};

const char *eapp_language(void)
{
    uint32_t index = 0;
    uint32_t size = sizeof index;
    if (GameSettings_GetValue("Language", &index, &size) != 0 ||
        index >= sizeof languages / sizeof languages[0])
        return NULL;
    return languages[index];
}

int eapp_name(char *out, size_t capacity)
{
    if (!out || capacity == 0 || capacity > 0x7fffffff)
        return -1;
    return GameSystem_GetGameName(out, (int)capacity, 1);
}

void eapp_log(const char *text)
{
    /* The host's printf writes into its format while it prints, so it
     * cannot be a literal. */
    char format[] = "%s\n";
    if (text)
        GameDebug_Printf(format, text);
}
