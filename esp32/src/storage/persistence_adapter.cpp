#include "persistence_adapter.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"
#include "zbox_config.h"
#include <Preferences.h>

namespace {
constexpr const char *PREF_NS        = "zbox";
constexpr const char *BRIGHTNESS_KEY = "night_light";
constexpr const char *PLAYBACK_KEY   = "playback_mode";
constexpr const char *VOLUME_KEY     = "volume_level";
}

void persistenceAdapterSaveBrightness(uint8_t percent) {
    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putUChar(BRIGHTNESS_KEY, percent);
    prefs.end();
    LOGI("[PERSIST] Brightness saved: %d%%\n", percent);
    postEventFromTask(makeEvent(EventType::BrightnessPersisted));
}

void persistenceAdapterSavePlaybackMode(PlaybackMode mode) {
    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putUChar(PLAYBACK_KEY, static_cast<uint8_t>(mode));
    prefs.end();
    LOGI("[PERSIST] PlaybackMode saved: %d\n", static_cast<int>(mode));
    postEventFromTask(makeEvent(EventType::PlaybackModePersisted));
}

void persistenceAdapterSaveVolume(uint8_t level) {
    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putUChar(VOLUME_KEY, level);
    prefs.end();
    LOGI("[PERSIST] Volume level saved: %u\n", (unsigned)level);
}

void persistenceAdapterSaveBtTarget(const char* name) {
    Preferences prefs;
    prefs.begin(PREF_NS, false);
    prefs.putString(BT_TARGET_NVS_KEY, name);
    prefs.end();
    LOGI("[PERSIST] BT target saved: %s\n", name);
}

void persistenceAdapterLoadBtTarget(char* buf, size_t len) {
    Preferences prefs;
    prefs.begin(PREF_NS, true);
    String val = prefs.getString(BT_TARGET_NVS_KEY, BT_DEFAULT_NAME);
    prefs.end();
    strlcpy(buf, val.c_str(), len);
}
