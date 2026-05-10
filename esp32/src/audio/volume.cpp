#include "volume.h"
#include <Preferences.h>
#include "zbox_config.h"
#include "logging.h"
#include "audio.h"

static int btVolume = BT_VOL_DEFAULT;
static Preferences preferences;

void loadBtVolume()
{
    preferences.begin("zbox", true);
    btVolume = preferences.getInt("bt_volume", BT_VOL_DEFAULT);
    preferences.end();
    btVolume = constrain(btVolume, BT_VOL_MIN, BT_VOL_MAX);
    LOGI("[VOL] Restored: %d%%\n", btVolume);
}

void applyBtVolume()
{
    audioSetBtVolumePercent(btVolume);
    LOGI("[VOL] Applied: %d%%\n", btVolume);
}

void volumeTick()
{
    // no-op: throttle and deferred save removed; saving done via PersistVolume effect
}

void setBtVolumeAndApply(uint8_t percent)
{
    btVolume = constrain((int)percent, BT_VOL_MIN, BT_VOL_MAX);
    applyBtVolume();
    LOGI("[VOL] Set+apply: %d%%\n", btVolume);
}

uint8_t getBtVolume()
{
    return (uint8_t)btVolume;
}
