#include "volume.h"
#include <Preferences.h>
#include "logging.h"
#include "audio.h"
#include "volume_scale.h"

static uint8_t outputVolumeLevel = OUTPUT_VOL_LEVEL_DEFAULT;
static uint8_t outputVolumePercent = volumeLevelToPercent(OUTPUT_VOL_LEVEL_DEFAULT);
static Preferences preferences;

void loadOutputVolume()
{
    preferences.begin("zbox", true);
    outputVolumeLevel = clampVolumeLevel(preferences.getUChar("volume_level", OUTPUT_VOL_LEVEL_DEFAULT));
    preferences.end();
    outputVolumePercent = volumeLevelToPercent(outputVolumeLevel);
    LOGI("[VOL] Restored output volume: level=%u percent=%u\n",
         (unsigned)outputVolumeLevel,
         (unsigned)outputVolumePercent);
}

void applyOutputVolume()
{
    outputVolumePercent = volumeLevelToPercent(outputVolumeLevel);
    audioSetOutputVolumePercent(outputVolumePercent);
    LOGI("[VOL] Applied output volume: level=%u percent=%u\n",
         (unsigned)outputVolumeLevel,
         (unsigned)outputVolumePercent);
}

void volumeTick()
{
    // no-op: throttle and deferred save removed; saving done via PersistVolume effect
}

void setOutputVolumeAndApply(uint8_t percent)
{
    outputVolumePercent = constrain((int)percent, OUTPUT_VOL_PERCENT_MIN, OUTPUT_VOL_PERCENT_MAX);
    audioSetOutputVolumePercent(outputVolumePercent);
    LOGI("[VOL] Set+apply output volume: %u%%\n", (unsigned)outputVolumePercent);
}

uint8_t getOutputVolumeLevel()
{
    return outputVolumeLevel;
}
