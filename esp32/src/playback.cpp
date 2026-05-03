#include "playback.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "jbl.h"
#include "leds.h"
#include "audio.h"
#include "persistent_log.h"
#include "nfc_module.h"
#include <Preferences.h>
#include <SD.h>
#include <vector>
#include <algorithm>

namespace {

constexpr const char *PLAYBACK_PREF_NS = "musicbox";
constexpr const char *PLAYBACK_MODE_KEY = "playback_mode";

Preferences playbackPrefs;
PlaybackMode currentMode = PlaybackMode::NFC;
std::vector<String> musicLibrary;
int currentTrackIndex = -1;
bool musicAutostartPending = false;

void clearPendingPlayback()
{
    pendingPlaybackPath = "";
    pendingPlaybackUid = "";
}

void clearNfcState()
{
    clearPendingPlayback();
    lastNfcUid[0] = '\0';
}

void clearMusicState()
{
    currentTrackIndex = -1;
    musicAutostartPending = false;
}

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
        PLOGF("[MUSIC] Cannot open /music directory");
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

    PLOGF("[MUSIC] Library size=%d", (int)musicLibrary.size());
}

bool hasMusicLibrary()
{
    if (musicLibrary.empty())
        refreshMusicLibrary();
    return !musicLibrary.empty();
}

void savePlaybackMode()
{
    playbackPrefs.begin(PLAYBACK_PREF_NS, false);
    playbackPrefs.putUChar(PLAYBACK_MODE_KEY, static_cast<uint8_t>(currentMode));
    playbackPrefs.end();
    PLOGF("[MODE] Saved mode=%d", static_cast<int>(currentMode));
}

void loadPlaybackMode()
{
    playbackPrefs.begin(PLAYBACK_PREF_NS, true);
    uint8_t raw = playbackPrefs.getUChar(PLAYBACK_MODE_KEY, static_cast<uint8_t>(PlaybackMode::NFC));
    playbackPrefs.end();
    currentMode = (raw == static_cast<uint8_t>(PlaybackMode::MUSIC)) ? PlaybackMode::MUSIC : PlaybackMode::NFC;
    PLOGF("[MODE] Restored mode=%d", static_cast<int>(currentMode));
}

int wrapTrackIndex(int index)
{
    int size = (int)musicLibrary.size();
    if (size <= 0)
        return -1;
    while (index < 0)
        index += size;
    while (index >= size)
        index -= size;
    return index;
}

void markPlaybackStarted(const char *reason)
{
    isPlaying = true;
    isPaused = false;
    if (!bootTimingDone)
    {
        LOG("[T+%4lu] >>> PLAYBACK START (%s)\n", millis() - bootStart, reason);
        LOG("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
        bootTimingDone = true;
    }
}

bool startMusicTrackAt(int index, const char *reason)
{
    if (!hasMusicLibrary())
    {
        PLOGF("[MUSIC] Library empty");
        ledFlashWarning();
        return false;
    }

    index = wrapTrackIndex(index);
    if (index < 0)
        return false;

    String path = musicLibrary[index];
    if (!SD.exists(path))
    {
        PLOGF("[MUSIC] Missing file: %s", path.c_str());
        ledFlashWarning();
        return false;
    }

    currentTrackIndex = index;

    if (!ensureJblReady())
    {
        LOGLN("[MUSIC] JBL not ready - deferring until BT connects");
        pendingPlaybackPath = path;
        pendingPlaybackUid = "";
        musicAutostartPending = true;
        ledSetWaitBt();
        return false;
    }

    clearPendingPlayback();
    musicAutostartPending = false;
    audioStartFile(path.c_str());
    markPlaybackStarted(reason);
    return true;
}

void startMusicFromFirst(const char *reason)
{
    if (!hasMusicLibrary())
        return;
    startMusicTrackAt(0, reason);
}

void announceModeChange(bool musicMode)
{
    ledShowModeChange(musicMode);
    playSystemSoundSync(musicMode ? "music_mode" : "nfc_mode");
    trackEndedFlag = false;
}

void enterMusicMode()
{
    currentMode = PlaybackMode::MUSIC;
    savePlaybackMode();
    audioStop();
    clearNfcState();
    clearMusicState();
    announceModeChange(true);
    musicAutostartPending = true;
    if (g_btConnected)
        startMusicFromFirst("music mode");
}

void enterNfcMode()
{
    currentMode = PlaybackMode::NFC;
    savePlaybackMode();
    audioStop();
    clearMusicState();
    clearNfcState();
    announceModeChange(false);

    if (!nfcReady || !sdReady)
    {
        ledSetIdle();
        return;
    }

    char uidBuf[30] = {};
    if (nfcPrescan(uidBuf, sizeof(uidBuf)))
        startPlayback(String(uidBuf));
    else
        ledSetIdle();
}

} // namespace

bool ensureJblReady()
{
    if (g_btConnected)
        return true;

    static unsigned long lastPulseMs = 0;
    if (!isJblOn() && millis() - lastPulseMs > 10000)
    {
        LOGLN("[JBL] ADC says OFF - pressing power");
        lastPulseMs = millis();
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);
        digitalWrite(JBL_POWER, LOW);
    }

    return false;
}

void playbackInit()
{
    loadPlaybackMode();
    refreshMusicLibrary();
    musicAutostartPending = (currentMode == PlaybackMode::MUSIC);
}

PlaybackMode playbackGetMode()
{
    return currentMode;
}

bool playbackIsNfcMode()
{
    return currentMode == PlaybackMode::NFC;
}

bool playbackIsMusicMode()
{
    return currentMode == PlaybackMode::MUSIC;
}

void playbackToggleMode()
{
    if (currentMode == PlaybackMode::NFC)
        enterMusicMode();
    else
        enterNfcMode();
}

void playbackHandleBtConnected()
{
    if (playbackIsMusicMode())
    {
        if (musicAutostartPending)
        {
            if (currentTrackIndex >= 0)
                startMusicTrackAt(currentTrackIndex, "music mode");
            else
                startMusicFromFirst("music mode");
        }
        else if (currentTrackIndex < 0)
        {
            startMusicFromFirst("music mode");
        }
        return;
    }

    if (!pendingPlaybackPath.isEmpty() && sdReady)
    {
        audioStartFile(pendingPlaybackPath.c_str());
        strlcpy((char*)lastNfcUid, pendingPlaybackUid.c_str(), sizeof(lastNfcUid));
        markPlaybackStarted("deferred NFC");
        clearPendingPlayback();
    }
}

void playbackHandleTrackEnded()
{
    if (playbackIsMusicMode())
    {
        if (musicLibrary.empty())
        {
            ledSetIdle();
            return;
        }
        startMusicTrackAt(currentTrackIndex + 1, "music next");
        return;
    }

    ledSetIdle();
}

void playbackHandleNfcTagPresent(const char *uid)
{
    if (!uid || !*uid || playbackIsMusicMode())
        return;

    if (strcmp(uid, (const char*)lastNfcUid) != 0)
        startPlayback(String(uid));
}

void playbackHandleNfcTagRemoved()
{
    if (playbackIsMusicMode())
        return;

    clearNfcState();
    if (isPlaying || isPaused)
        stopPlayback();
}

void playbackTogglePlayPause()
{
    if (!playbackIsMusicMode())
        return;

    if (audioIsPaused())
    {
        audioResume();
        isPlaying = true;
        isPaused = false;
        return;
    }

    if (isPlaying)
    {
        audioPause();
        isPlaying = false;
        isPaused = true;
        return;
    }

    if (currentTrackIndex >= 0)
        startMusicTrackAt(currentTrackIndex, "music resume");
    else
        startMusicFromFirst("music mode");
}

void playbackNextTrack()
{
    if (!playbackIsMusicMode())
        return;
    startMusicTrackAt(currentTrackIndex + 1, "music next");
}

void playbackPrevTrack()
{
    if (!playbackIsMusicMode())
        return;
    startMusicTrackAt(currentTrackIndex - 1, "music prev");
}

void startPlayback(const String &uid)
{
    PLOGF("[PLAY] startPlayback uid=%s", uid.c_str());
    if (!sdReady)
    {
        LOGLN("SD not ready");
        return;
    }

    auto it = figurineMap.find(uid);
    if (it == figurineMap.end())
    {
        LOG("No mapping for UID: %s\n", uid.c_str());
        ledFlashWarning();
        return;
    }

    String path = "/music/" + it->second;
    if (!SD.exists(path))
    {
        LOG("File missing: %s\n", path.c_str());
        ledFlashWarning();
        return;
    }

    if (!ensureJblReady())
    {
        LOGLN("[PLAY] JBL not ready - deferring until BT connects");
        pendingPlaybackPath = path;
        pendingPlaybackUid = uid;
        ledSetWaitBt();
        return;
    }

    clearPendingPlayback();
    audioStartFile(path.c_str());
    strlcpy((char*)lastNfcUid, uid.c_str(), sizeof(lastNfcUid));
    markPlaybackStarted("NFC trigger");
}

void stopPlayback()
{
    PLOGF("[STOP] stopPlayback");
    audioStop();
    isPaused = false;
    lastNfcUid[0] = '\0';
}

void playSystemSoundSync(const char *name, uint32_t timeoutMs)
{
    auto it = systemSoundMap.find(String(name));
    if (it == systemSoundMap.end()) return;
    if (!SD.exists(it->second) || !g_btConnected || !audioIsReady()) return;

    audioStop();
    delay(100);

    audioStartFile(it->second.c_str());
    isPlaying = true;
    isPaused = false;

    unsigned long start = millis();
    while (isPlaying && (millis() - start) < timeoutMs)
        delay(50);
}
