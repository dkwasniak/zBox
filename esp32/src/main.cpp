#include <Arduino.h>
#include <SD.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

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
#include "diagnostics.h"
#include "diagnostic_mode.h"

void setup()
{
    bootStart = millis();
    Serial.begin(115200);
    LOGLN("\n\n=== MusicBox ===");
    LOG("[T+%4lu] Boot start\n", 0UL);

    handleWakeFromDeepSleep();
    bootStart = millis();

    ledInit();
    buttonsInit();

    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);
    LOG("[T+%4lu] GPIO ready\n", millis() - bootStart);

    sdReady = initSD();
    if (!sdReady)
    {
        LOGLN("WARNING: No SD card");
    }
    LOG("[T+%4lu] SD %s\n", millis() - bootStart, sdReady ? "OK" : "FAIL");
    ledSetBootProgress(0); // SD done

    plogInit();
    plogMark("BOOT");
    {
        esp_reset_reason_t reason = esp_reset_reason();
        PLOGF("[BOOT] reset_reason=%d(%s) wake_cause=%d",
              (int)reason, resetReasonName(reason), (int)esp_sleep_get_wakeup_cause());
    }

    bool syncPending = SD.exists("/data/sync_pending");
    bool diagPending = SD.exists(DIAG_PENDING_PATH);
    LOG("[BOOT] diag_pending flag: %d\n", diagPending);
    LOG("[BOOT] sync_pending flag: %d\n", syncPending);

    if (diagPending)
    {
        runDiagnosticMode();
        return;
    }

    if (syncPending)
    {
        runSyncMode();
        return;
    }

    LOGLN("\n--- Normal mode (fast boot) ---");

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

    LOG("[NFC] init: wake_cause=%d\n", (int)esp_sleep_get_wakeup_cause());
    if (!nfcInit())
        PLOGF("ERROR: PN532 not found! (setup)");
    LOG("[T+%4lu] NFC %s\n", millis() - bootStart, nfcReady ? "OK" : "FAIL");
    ledSetBootProgress(1); // NFC done

    if (sdReady)
    {
        loadMappings();
        loadSystemSounds();
    }
    playbackInit();
    LOG("[T+%4lu] Mappings loaded (%d)\n", millis() - bootStart, figurineMap.size());
    ledSetBootProgress(2); // Mappings done

    if (playbackIsNfcMode() && nfcReady && sdReady)
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

    loadBtVolume();

    ledSetBootProgress(4); // BT step
    LOG("[T+%4lu] BT A2DP starting -> %s\n", millis() - bootStart, BT_SPEAKER_NAME);
    ledSetWaitBt(); // PRZED audioInit() - bo a2dp.begin() może blokować
    audioInit();

    btWaitStart = millis();
    if (jblNeedsPower) jblRecoveryDone = true;

    nfcStartTask();

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

    lastActivityMs = millis();
    LOGLN("Ready! Waiting for BT connection...");
}

void loop()
{
    esp_task_wdt_reset();
    static volatile uint8_t loopStep = 0;

    audioPollBtConnection();

    loopStep = 1;
    if (trackEndedFlag) {
        trackEndedFlag = false;
        playbackHandleTrackEnded();
    }

    loopStep = 2;
    if (btVolumeApplied && !g_btConnected)
    {
        btVolumeApplied = false;
        if (!isPlaying)
            ledSetWaitBt();
    }

    loopStep = 3;
    if (!btVolumeApplied && g_btConnected)
    {
        LOG("[T+%4lu] BT connected!\n", millis() - bootStart);
        btVolumeApplied = true;
        applyBtVolume();
        ledSetIdle();
        playbackHandleBtConnected();
    }

    loopStep = 4;
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
    volumeTick();
    loopStep = 6;
    vTaskDelay(pdMS_TO_TICKS(5));

    loopStep = 7;
    {
        NfcEvent nfcEvt;
        while (nfcGetEvent(&nfcEvt, 0))
        {
            if (nfcEvt.tagPresent)
            {
                playbackHandleNfcTagPresent(nfcEvt.uid);
            }
            else
            {
                playbackHandleNfcTagRemoved();
            }
        }
    }

    loopStep = 8;
    if (isPlaying)
        lastActivityMs = millis();
    else if (lastActivityMs > 0 && millis() - lastActivityMs > IDLE_TIMEOUT_MS)
    {
        LOGLN("[IDLE] Timeout - entering deep sleep");
        enterDeepSleep();
    }

    loopStep = 9;
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        PLOGF("[LOOP] alive step=%u isPlaying=%d btConn=%d", loopStep, (int)isPlaying, (int)g_btConnected);
        plogFlushToSd();
    }

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
