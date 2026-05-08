#include "night_light.h"

#include <Arduino.h>
#include <Preferences.h>

#include "leds.h"
#include "logging.h"
#include "zbox_config.h"
#include "sleep.h"
#include "state.h"

namespace {

constexpr const char *PREF_NS = "zbox";
constexpr const char *BRIGHTNESS_KEY = "night_light";
constexpr unsigned long NIGHT_LIGHT_SAVE_DELAY_MS = 1500;

Preferences preferences;
int nightLightBrightnessPercent = NIGHT_LIGHT_BRIGHTNESS_DEFAULT;
bool savePending = false;
unsigned long saveDueMs = 0;
unsigned long sessionDeadlineMs = 0;

int clampBrightness(int value)
{
    return constrain(value, NIGHT_LIGHT_BRIGHTNESS_MIN, NIGHT_LIGHT_BRIGHTNESS_MAX);
}

void saveBrightness()
{
    preferences.begin(PREF_NS, false);
    preferences.putInt(BRIGHTNESS_KEY, nightLightBrightnessPercent);
    preferences.end();
    LOGI("[NIGHT] Saved brightness=%d%%\n", nightLightBrightnessPercent);
}

void scheduleSave()
{
    savePending = true;
    saveDueMs = millis() + NIGHT_LIGHT_SAVE_DELAY_MS;
}

void resetSessionDeadline()
{
    sessionDeadlineMs = millis() + NIGHT_LIGHT_TIMEOUT_MS;
}

void applyBrightness()
{
    ledSetNightLight(nightLightBrightnessPercent);
}

void adjustBrightness(int delta)
{
    int oldValue = nightLightBrightnessPercent;
    int newValue = clampBrightness(oldValue + delta);
    if (newValue == oldValue)
    {
        LOGI("[NIGHT] Brightness unchanged at %d%%\n", oldValue);
        return;
    }

    nightLightBrightnessPercent = newValue;
    LOGI("[NIGHT] Brightness %d%% -> %d%%\n", oldValue, newValue);
    applyBrightness();
    scheduleSave();
    resetSessionDeadline();
}

} // namespace

void nightLightInit()
{
    preferences.begin(PREF_NS, true);
    nightLightBrightnessPercent = clampBrightness(
        preferences.getInt(BRIGHTNESS_KEY, NIGHT_LIGHT_BRIGHTNESS_DEFAULT));
    preferences.end();

    savePending = false;
    resetSessionDeadline();
    applyBrightness();
    LOGI("[NIGHT] Session started brightness=%d%% timeout_ms=%lu\n",
         nightLightBrightnessPercent, (unsigned long)NIGHT_LIGHT_TIMEOUT_MS);
}

void nightLightTick()
{
    if (!runtimeIsNightLight())
        return;

    unsigned long now = millis();
    if (savePending && (long)(now - saveDueMs) >= 0)
    {
        savePending = false;
        saveBrightness();
    }

    if (sessionDeadlineMs > 0 && (long)(now - sessionDeadlineMs) >= 0)
    {
        LOGC("[NIGHT] Timeout expired - entering deep sleep\n");
        enterDeepSleep();
    }
}

void nightLightIncrease()
{
    adjustBrightness(NIGHT_LIGHT_BRIGHTNESS_STEP);
}

void nightLightDecrease()
{
    adjustBrightness(-NIGHT_LIGHT_BRIGHTNESS_STEP);
}

void nightLightFlushPendingSave()
{
    if (!savePending)
        return;
    savePending = false;
    saveBrightness();
}

int nightLightGetBrightnessPercent()
{
    return nightLightBrightnessPercent;
}
