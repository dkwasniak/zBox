/*
 * MusicBox - Muzyczne Pudełko dla Dzieci
 *
 * ESP32 Lolin D32 Pro + PN532 (NFC Software SPI) + Bluetooth A2DP + SD Card
 * Offline mode: muzyka i mappingi na karcie SD
 * Audio: SD -> MP3 decoder -> Bluetooth A2DP Source -> JBL Go 2
 *
 * Sync: oba przyciski 2s -> flaga sync_pending -> restart -> WiFi sync -> restart
 * Pierwsze WiFi: automatyczny portal AP "MusicBox-Setup" do konfiguracji
 *
 * OPTYMALIZACJA STARTU:
 * - Usunięte zbędne delay() z inicjalizacji SD, NFC, ADC
 * - Nieblokujące włączanie JBL (puls w tle, bez czekania na boot)
 * - BT A2DP startuje jak najwcześniej (async, łączy się w tle)
 * - NFC pre-scan przy boot - jeśli figurka już stoi, plik gotowy do odtwarzania
 * - Odtwarzanie startuje natychmiast po połączeniu BT (deferred playback)
 */

#include <Arduino.h>
#include <SD.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>

#include "persistent_log.h"

#include "musicbox_config.h"
#include "logging.h"
#include "shared_types.h"
#include "state.h"
#include "leds.h"
#include "jbl.h"
#include "volume.h"
#include "sd_storage.h"
#include "audio.h"
#include "nfc_module.h"
#include "buttons_isr.h"
#include "playback.h"
#include "sleep.h"
#include "buttons.h"
#include "sync_mode.h"

// =============================================================================
// SETUP - ZOPTYMALIZOWANY
// =============================================================================
//
// Stara kolejność (sekwencyjna, ~5.5s samych delay):
//   SD(+100ms) → sync_check → NFC(+1000ms) → ADC(+500ms) → JBL_power(+500-3500ms)
//   → loadVolume → BT_begin → mappings → [test file]
//
// Nowa kolejność (równoległa, ~150ms delay):
//   SD(0ms) → sync_check → NFC(+100ms) → mappings → NFC_prescan(+200ms)
//   → JBL_async(0ms) → loadVolume → BT_begin → [deferred playback]
//
// BT startuje ~5s wcześniej. JBL bootuje w tle równolegle z BT.
// Jeśli figurka stoi na padzie - plik gotowy do odtwarzania od razu po BT connect.
//

void setup()
{
    bootStart = millis();
    Serial.begin(115200);
    LOGLN("\n\n=== MusicBox ===");
    LOG("[T+%4lu] Boot start\n", 0UL);

    // Hold-to-wake: jeśli boot pochodzi z deep sleep, wymaga przytrzymania
    // BTN_D przez LONG_PRESS_MS. Inicjalizuje minimalnie LEDy do animacji
    // postępu i wraca do snu jeśli przycisk puszczony za wcześnie.
    handleWakeFromDeepSleep();
    // Reset bootStart - hold-to-wake może zabrać ~2s, nie chcemy żeby
    // wszystkie późniejsze logi [T+...] były przesunięte o czas trzymania.
    bootStart = millis();

    // LED - jako pierwsze, żeby pokazać że urządzenie żyje
    ledInit(); // uruchamia task + wyświetla dim niebieski

    // GPIO - natychmiast
    buttonsInit();

    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);
    LOG("[T+%4lu] GPIO ready\n", millis() - bootStart);

    // SD Card - bez delay
    sdReady = initSD();
    if (!sdReady)
    {
        LOGLN("WARNING: No SD card");
    }
    LOG("[T+%4lu] SD %s\n", millis() - bootStart, sdReady ? "OK" : "FAIL");
    ledSetBootProgress(0); // SD done

    plogInit();
    plogMark("BOOT");

    // Sprawdź flagę sync PRZED inicjalizacją BT
    bool syncPending = SD.exists("/data/sync_pending");
    LOG("[BOOT] sync_pending flag: %d\n", syncPending);

    if (syncPending)
    {
        runSyncMode();
        return;
    }

    // === Normalny tryb ===
    LOGLN("\n--- Normal mode (fast boot) ---");

    // --- JBL Power ON: puls startuje tutaj, inne operacje wypełniają czas ---
    // Sprawdzamy status i startujemy puls PRZED NFC/mappings,
    // dzięki czemu 500ms pulsu mija w trakcie inicjalizacji
    bool jblNeedsPower = !isJblOn();
    unsigned long jblPulseStart = 0;
    if (jblNeedsPower)
    {
        LOG("[T+%4lu] JBL OFF - starting power pulse\n", millis() - bootStart);
        digitalWrite(JBL_POWER, HIGH);
        jblPulseStart = millis();
    }
    else
    {
        LOG("[T+%4lu] JBL already ON\n", millis() - bootStart);
    }

    // NFC init — nfcInit() obsługuje cold boot, deep sleep wake i reset.
    // Po znanym PowerDown robi raw SPI wake przed nfc.begin(), bo Adafruit_PN532
    // samo zarządza SS i nie utrzyma NSS low przez T_wake_up przed pierwszą ramką.
    // NFC critical sekcja zawiesza LED task — FreeRTOS preemption może przerwać
    // Software SPI, a mutex blokuje równoległe użycie PN532 z innych kontekstów.
    LOG("[NFC] init: wake_cause=%d\n", (int)esp_sleep_get_wakeup_cause());
    if (!nfcInit())
        PLOGF("ERROR: PN532 not found! (setup)");
    LOG("[T+%4lu] NFC %s\n", millis() - bootStart, nfcReady ? "OK" : "FAIL");
    ledSetBootProgress(1); // NFC done

    // Mappings - ładowane wcześniej (potrzebne do NFC pre-scan)
    if (sdReady)
    {
        loadMappings();
        loadSystemSounds();
    }
    LOG("[T+%4lu] Mappings loaded (%d)\n", millis() - bootStart, figurineMap.size());
    ledSetBootProgress(2); // Mappings done

// NFC pre-scan - sprawdź czy figurka już stoi na padzie
// Typowy scenariusz: dziecko stawiło figurkę, rodzic włącza urządzenie
#if !TEST_AUDIO_MODE
    if (nfcReady && sdReady)
    {
        char preUidBuf[30] = {};
        if (nfcPrescan(preUidBuf, sizeof(preUidBuf)))
        {
            String preUid = preUidBuf;
            LOG("[T+%4lu] NFC pre-scan: %s\n", millis() - bootStart, preUid.c_str());

            auto it = figurineMap.find(preUid);
            if (it != figurineMap.end())
            {
                String path = "/music/" + it->second;
                if (SD.exists(path))
                {
                    pendingPlaybackPath = path;
                    pendingPlaybackUid = preUid;
                    LOG("[T+%4lu] Queued: %s\n", millis() - bootStart, path.c_str());
                }
            }
        }
        else
        {
            LOG("[T+%4lu] NFC pre-scan: no tag\n", millis() - bootStart);
        }
    }
#else
    // W test mode - ustaw pending na test file
    if (sdReady && SD.exists(TEST_SD_FILE))
    {
        pendingPlaybackPath = TEST_SD_FILE;
        LOG("[T+%4lu] Test file queued: %s\n", millis() - bootStart, TEST_SD_FILE);
    }
#endif

    // --- Zakończ puls JBL (dopełnij do 500ms jeśli trzeba) ---
    if (jblNeedsPower)
    {
        unsigned long elapsed = millis() - jblPulseStart;
        if (elapsed < JBL_POWER_PRESS_MS)
        {
            delay(JBL_POWER_PRESS_MS - elapsed);
        }
        digitalWrite(JBL_POWER, LOW);
        LOG("[T+%4lu] JBL power pulse done (%lu ms)\n",
                      millis() - bootStart, millis() - jblPulseStart);
    }

    ledSetBootProgress(3); // JBL done

    // Głośność z NVS
    loadBtVolume();

    // Bluetooth A2DP
    ledSetBootProgress(4); // BT step
    LOG("[T+%4lu] BT A2DP starting -> %s\n", millis() - bootStart, BT_SPEAKER_NAME);
    ledSetWaitBt(); // PRZED audioInit() - bo a2dp.begin() może blokować
    audioInit();

    // === JBL fallback recovery (nieblokujące) ===
    // Jeśli ADC powiedział "JBL ON" ale BT nie łączy się w 5s → ADC kłamało.
    // Puls power wykonywany jest w loop() żeby handleButtons() działało podczas czekania.
    btWaitStart = millis();
    if (jblNeedsPower) jblRecoveryDone = true;

#if !TEST_AUDIO_MODE
    nfcStartTask();
#endif


    // Task Watchdog: monitoruje loop task, reset po 15s bez esp_task_wdt_reset()
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = false  // reset, nie panic — żeby RTC recovery zadziałało
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);  // NULL = current task (loop task)

    LOG("[T+%4lu] BT A2DP initiated\n", millis() - bootStart);
    LOG("[BOOT] Loop task core: %d\n", xPortGetCoreID());
    LOG("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);

#if TEST_AUDIO_MODE
    LOGLN("=== TEST MODE ===");
#endif
    lastActivityMs = millis();
    LOGLN("Ready! Waiting for BT connection...");
}

// =============================================================================
// LOOP
// =============================================================================

void loop()
{
    esp_task_wdt_reset();
    static volatile uint8_t loopStep = 0;

    loopStep = 1;
    // Koniec tracka: zeruj lastNfcUid i LED tu, nie w audio task (eliminuje race)
    if (trackEndedFlag) {
        trackEndedFlag = false;
        lastNfcUid[0] = '\0';
        ledSetIdle();
    }

    loopStep = 2;
    // Obsługa rozłączenia BT - reset flagi żeby ponowne połączenie ustawiło LED
    if (btVolumeApplied && !g_btConnected)
    {
        btVolumeApplied = false;
        if (!isPlaying)
            ledSetWaitBt();
    }

    loopStep = 3;
    // Po połączeniu BT: uruchom odłożone odtwarzanie
    if (!btVolumeApplied && g_btConnected)
    {
        LOG("[T+%4lu] BT connected!\n", millis() - bootStart);
        btVolumeApplied = true;
        applyBtVolume();
        ledSetIdle();

        // Deferred playback - plik wykryty przy boot, czekał na BT
        if (!pendingPlaybackPath.isEmpty() && sdReady)
        {
            audioStartFile(pendingPlaybackPath.c_str());
            strlcpy((char*)lastNfcUid, pendingPlaybackUid.c_str(), sizeof(lastNfcUid));
            isPlaying = true;
            ledSetPlaying();
            LOG("[T+%4lu] >>> PLAYBACK START: %s\n", millis() - bootStart, pendingPlaybackPath.c_str());
            LOG("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
            bootTimingDone = true;
            pendingPlaybackPath = "";
            pendingPlaybackUid = "";
        }
    }

    loopStep = 4;
    // JBL fallback recovery — nieblokujące
    if (g_btConnected) {
        jblRecoveryDone = true;
    }
    else if (!jblRecoveryDone && btWaitStart > 0 &&
             millis() - btWaitStart > 5000)
    {
        jblRecoveryDone = true;
        PLOGF("[JBL] BT timeout - ADC false positive, pressing power");
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);   // 500ms, jednorazowe; ISR-y działają
        digitalWrite(JBL_POWER, LOW);
        LOG("[T+%4lu] JBL power pulse (recovery)\n", millis() - bootStart);
    }

    loopStep = 5;
    handleButtons();
    loopStep = 6;
    vTaskDelay(pdMS_TO_TICKS(5)); // yield — pętla nie może głodzić IDLE1

    loopStep = 7;
#if !TEST_AUDIO_MODE
    {
        NfcEvent nfcEvt;
        while (nfcGetEvent(&nfcEvt, 0))
        {
            if (nfcEvt.tagPresent)
            {
                if (strcmp(nfcEvt.uid, (const char*)lastNfcUid) != 0)
                    startPlayback(String(nfcEvt.uid));
            }
            else
            {
                pendingPlaybackPath = "";
                pendingPlaybackUid = "";
                if (isPlaying)
                    stopPlayback();
            }
        }
    }
#endif

    loopStep = 8;
    // Idle timeout - brak odtwarzania przez IDLE_TIMEOUT_MS → deep sleep
    if (isPlaying)
        lastActivityMs = millis();
    else if (lastActivityMs > 0 && millis() - lastActivityMs > IDLE_TIMEOUT_MS)
    {
        LOGLN("[IDLE] Timeout - entering deep sleep");
        enterDeepSleep();
    }

    loopStep = 9;
    // Heartbeat - diagnostyka zawieszania loop
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        PLOGF("[LOOP] alive step=%u isPlaying=%d btConn=%d", loopStep, (int)isPlaying, (int)g_btConnected);
        plogFlushToSd();
    }

    // Diagnostyka stack HWM + heap co 30s
    static unsigned long lastDiag = 0;
    if (millis() - lastDiag > 30000) {
        lastDiag = millis();
        PLOGF("[DIAG] HWM loop=%u led=%u audio=%u nfc=%u",
            uxTaskGetStackHighWaterMark(NULL),
            ledGetTaskHWM(),
            audioGetTaskHWM(),
            nfcGetTaskHWM());
        PLOGF("[DIAG] heap free=%u min=%u largest=%u",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
}
