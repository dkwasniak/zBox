#include "playback.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "persistent_log.h"
#include <Preferences.h>
#include <SD.h>
#include <vector>
#include <algorithm>

namespace {

constexpr const char *PLAYBACK_PREF_NS = "zbox";
constexpr const char *PLAYBACK_MODE_KEY = "playback_mode";

Preferences playbackPrefs;
PlaybackMode currentMode = PlaybackMode::Nfc;
std::vector<String> musicLibrary;

bool isMusicFile(const String &name)
{
    return name.endsWith(".mp3") || name.endsWith(".MP3");
}

String normalizeMusicPath(const char *rawName)
{
    String path = rawName ? String(rawName) : String();
    if (!path.startsWith("/"))
        path = "/music/" + path;
    return path;
}

void refreshMusicLibrary()
{
    musicLibrary.clear();
    if (!sdReady)
        return;

    File dir = SD.open("/music");
    if (!dir || !dir.isDirectory())
    {
        LOGW("[MUSIC] Cannot open /music directory\n");
        return;
    }

    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
            break;

        if (!entry.isDirectory())
        {
            String path = normalizeMusicPath(entry.name());
            String filename = path.substring(path.lastIndexOf('/') + 1);
            if (isMusicFile(filename))
                musicLibrary.push_back(path);
        }
        entry.close();
    }
    dir.close();

    std::sort(musicLibrary.begin(), musicLibrary.end(),
              [](const String &a, const String &b) { return strcmp(a.c_str(), b.c_str()) < 0; });

    LOGI("[MUSIC] Library size=%d\n", (int)musicLibrary.size());
}

bool hasMusicLibrary()
{
    if (musicLibrary.empty())
        refreshMusicLibrary();
    return !musicLibrary.empty();
}

void loadPlaybackMode()
{
    playbackPrefs.begin(PLAYBACK_PREF_NS, true);
    uint8_t raw = playbackPrefs.getUChar(PLAYBACK_MODE_KEY, static_cast<uint8_t>(PlaybackMode::Nfc));
    playbackPrefs.end();
    currentMode = (raw == static_cast<uint8_t>(PlaybackMode::Music)) ? PlaybackMode::Music : PlaybackMode::Nfc;
    LOGC("[BOOT] playback_mode=%d\n", static_cast<int>(currentMode));
    LOGI("[MODE] Restored mode=%d\n", static_cast<int>(currentMode));
}

} // namespace

void playbackInit()
{
    loadPlaybackMode();
    refreshMusicLibrary();
}

PlaybackMode playbackGetMode()
{
    return currentMode;
}

bool playbackIsNfcMode()
{
    return currentMode == PlaybackMode::Nfc;
}

bool playbackIsMusicMode()
{
    return currentMode == PlaybackMode::Music;
}

bool playbackGetMusicPath(uint16_t index, char *out_path, size_t out_size)
{
    if (!hasMusicLibrary()) return false;
    if (index >= musicLibrary.size()) return false;
    strlcpy(out_path, musicLibrary[index].c_str(), out_size);
    return true;
}
