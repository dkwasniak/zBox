#include "playback.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "jbl.h"
#include "leds.h"
#include "audio.h"
#include "persistent_log.h"
#include <SD.h>

bool ensureJblReady()
{
    if (g_btConnected)
        return true;

    // JBL nie jest połączony. Jeśli ADC mówi OFF - wciśnij power (z debounce).
    static unsigned long lastPulseMs = 0;
    if (!isJblOn() && millis() - lastPulseMs > 10000)
    {
        LOGLN("[JBL] ADC says OFF - pressing power");
        lastPulseMs = millis();
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS); // 500ms — konieczne fizycznie
        digitalWrite(JBL_POWER, LOW);
    }

    // Zwracamy false — caller ustawi pendingPlaybackPath, auto_reconnect zadba o resztę
    return false;
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

    // Auto-recovery: JBL mógł się sam wyłączyć po bezczynności.
    // ensureJblReady() sprawdza ADC, w razie potrzeby wciska power i czeka na A2DP.
    if (!ensureJblReady())
    {
        LOGLN("[PLAY] JBL not ready - deferring until BT connects");
        pendingPlaybackPath = path;
        pendingPlaybackUid = uid;
        ledSetWaitBt();
        return;
    }

    audioStartFile(path.c_str());
    strlcpy((char*)lastNfcUid, uid.c_str(), sizeof(lastNfcUid));
    isPlaying = true;
    if (!bootTimingDone)
    {
        LOG("[T+%4lu] >>> PLAYBACK START (NFC trigger)\n", millis() - bootStart);
        LOG("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
        bootTimingDone = true;
    }
}

void stopPlayback()
{
    PLOGF("[STOP] stopPlayback");
    audioStop();
    lastNfcUid[0] = '\0';
}

// Odtwarza dźwięk systemowy przez BT i czeka na zakończenie (blokujące).
// Zwraca natychmiast jeśli dźwięk nie istnieje, BT nie podłączony lub timeout.
void playSystemSoundSync(const char *name, uint32_t timeoutMs)
{
    auto it = systemSoundMap.find(String(name));
    if (it == systemSoundMap.end()) return;
    if (!SD.exists(it->second) || !g_btConnected || !audioIsReady()) return;

    // Zatrzymaj bieżący utwór
    audioStop();
    delay(100);

    // Zacznij odtwarzać dźwięk systemowy
    audioStartFile(it->second.c_str());
    isPlaying = true;

    // Czekaj na zakończenie (audio task ustawi isPlaying=false gdy plik się skończy)
    unsigned long start = millis();
    while (isPlaying && (millis() - start) < timeoutMs)
        delay(50);
}
