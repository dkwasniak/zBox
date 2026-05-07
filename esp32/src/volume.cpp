#include "volume.h"
#include <Preferences.h>
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "audio.h"
#include "persistent_log.h"

static int btVolume = BT_VOL_DEFAULT;
static Preferences preferences;
static bool volumeSavePending = false;
static unsigned long volumeSaveDueMs = 0;
static bool volumeApplyPending = false;
static unsigned long volumeApplyDueMs = 0;
static unsigned long lastApplyMs = 0;

static constexpr unsigned long VOLUME_SAVE_DELAY_MS = 1500;
static constexpr unsigned long VOLUME_APPLY_THROTTLE_MS = 500;

static void saveBtVolume()
{
    preferences.begin("musicbox", false);
    preferences.putInt("bt_volume", btVolume);
    preferences.end();
    LOGI("[VOL] Saved: %d%%\n", btVolume);
}

void loadBtVolume()
{
    preferences.begin("musicbox", true);
    btVolume = preferences.getInt("bt_volume", BT_VOL_DEFAULT);
    preferences.end();
    btVolume = constrain(btVolume, BT_VOL_MIN, BT_VOL_MAX);
    LOGI("[VOL] Restored: %d%%\n", btVolume);
}

void applyBtVolume()
{
    if (!g_btConnected)
    {
        volumeApplyPending = false;
        return;
    }
    unsigned long now = millis();
    unsigned long elapsed = now - lastApplyMs;
    if (elapsed < VOLUME_APPLY_THROTTLE_MS)
    {
        volumeApplyPending = true;
        volumeApplyDueMs = lastApplyMs + VOLUME_APPLY_THROTTLE_MS;
        LOGI("[VOL] Skipped (throttle): %d%%\n", btVolume);
        return;
    }
    volumeApplyPending = false;
    lastApplyMs = now;
    audioSetBtVolumePercent(btVolume);
    LOGI("[VOL] Queued apply: %d%%\n", btVolume);
}

static void scheduleVolumeSave()
{
    volumeSavePending = true;
    volumeSaveDueMs = millis() + VOLUME_SAVE_DELAY_MS;
}

void volumeTick()
{
    unsigned long now = millis();

    if (volumeApplyPending && (long)(now - volumeApplyDueMs) >= 0)
        applyBtVolume();

    if (volumeSavePending && (long)(now - volumeSaveDueMs) >= 0)
    {
        volumeSavePending = false;
        saveBtVolume();
    }
}

void volumeUp()
{
    int oldVolume = btVolume;
    btVolume = min(btVolume + BT_VOL_STEP, BT_VOL_MAX);
    LOGI("[VOL] Up %d%% -> %d%%\n", oldVolume, btVolume);
    applyBtVolume();
    scheduleVolumeSave();
    ledShowVolume(btVolume);
}

void volumeDown()
{
    int oldVolume = btVolume;
    btVolume = max(btVolume - BT_VOL_STEP, BT_VOL_MIN);
    LOGI("[VOL] Down %d%% -> %d%%\n", oldVolume, btVolume);
    applyBtVolume();
    scheduleVolumeSave();
    ledShowVolume(btVolume);
}
