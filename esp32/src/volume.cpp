#include "volume.h"
#include <Preferences.h>
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "audio.h"

static int btVolume = BT_VOL_DEFAULT;
static Preferences preferences;

static void saveBtVolume()
{
    preferences.begin("musicbox", false);
    preferences.putInt("bt_volume", btVolume);
    preferences.end();
}

void loadBtVolume()
{
    preferences.begin("musicbox", true);
    btVolume = preferences.getInt("bt_volume", BT_VOL_DEFAULT);
    preferences.end();
    LOG("[VOL] Restored: %d%%\n", btVolume);
}

void applyBtVolume()
{
    if (!g_btConnected) return;
    static unsigned long lastApply = 0;
    unsigned long now = millis();
    if (now - lastApply < 500)
    {
        LOG("[VOL] Skipped (throttle): %d%%\n", btVolume);
        return;
    }
    lastApply = now;
    audioSetBtVolumePercent(btVolume);
    LOG("[VOL] Applied: %d%%\n", btVolume);
}

void volumeUp()
{
    btVolume = min(btVolume + BT_VOL_STEP, BT_VOL_MAX);
    applyBtVolume();
    saveBtVolume();
    ledShowVolume(btVolume);
}

void volumeDown()
{
    btVolume = max(btVolume - BT_VOL_STEP, BT_VOL_MIN);
    applyBtVolume();
    saveBtVolume();
    ledShowVolume(btVolume);
}
