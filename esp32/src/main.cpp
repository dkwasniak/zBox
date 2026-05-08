#include <Arduino.h>
#include <SD.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

#include "persistent_log.h"

#include "zbox_config.h"
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
#include "diagnostics.h"
#include "diagnostic_mode.h"
#include "night_light.h"

static void configureLoopWatchdog()
{
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
#else
    esp_task_wdt_init(15, false);
#endif
    esp_task_wdt_add(NULL);
}

void setup()
{
    bootStart = millis();
    Serial.begin(115200);
    plogInit(false);
    LOGI("\n\n=== zBox ===\n");
    LOGI("Boot start\n");

    WakeDecision wakeDecision = handleWakeFromDeepSleep();
    runtimeSetSessionMode(wakeDecision == WakeDecision::NIGHT_LIGHT
                              ? RuntimeSessionMode::NIGHT_LIGHT
                              : RuntimeSessionMode::NORMAL);
    bootStart = millis();

    ledInit();
    buttonsInit();

    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);
    LOGI("GPIO ready\n");

    sdReady = initSD();
    if (!sdReady)
    {
        LOGW("[BOOT] No SD card\n");
    }
    else
    {
        ledLoadConfigFromSd();
    }
    LOGI("SD %s\n", sdReady ? "OK" : "FAIL");

    plogInit(sdReady);
    plogMark("CRIT", "BOOT");
    {
        esp_reset_reason_t reason = esp_reset_reason();
        LOGC("[BOOT] reset_reason=%d(%s) wake_cause=%d sd=%s heap=%u min_heap=%u\n",
             (int)reason, resetReasonName(reason), (int)esp_sleep_get_wakeup_cause(),
             sdReady ? "OK" : "FAIL", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }

    bool diagPending = SD.exists(DIAG_PENDING_PATH);
    LOGI("[BOOT] diag_pending flag: %d\n", diagPending);
    if (diagPending)
        LOGC("[BOOT] diagnostic mode requested via %s\n", DIAG_PENDING_PATH);

    if (diagPending)
    {
        runDiagnosticMode();
        return;
    }

    if (!runtimeIsNightLight())
        ledSetBootProgress(0); // SD done

    if (runtimeIsNightLight())
    {
        LOGI("\n--- Night light mode ---\n");
        LOGI("[NIGHT] Booting night light session\n");
        LOGI("[NIGHT] Skipping JBL wake pulse for night light session\n");

        nightLightInit();
        audioInit();
        btWaitStart = millis();

        configureLoopWatchdog();

        LOGC("[BOOT] night_light=1 bt_init=started loop_core=%d setup_ms=%lu\n",
             xPortGetCoreID(), millis() - bootStart);
        LOGI("BT A2DP initiated\n");
        LOGI("[BOOT] Loop task core: %d\n", xPortGetCoreID());
        LOGI("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);
        LOGI("Ready! Night light active.\n");
        return;
    }

    LOGI("\n--- Normal mode (fast boot) ---\n");

    bool jblNeedsPower = false;
    unsigned long jblPulseStart = 0;
    if (!isJblOn())
    {
        jblNeedsPower = true;
        LOGI("JBL OFF - starting power pulse\n");
        digitalWrite(JBL_POWER, HIGH);
        jblPulseStart = millis();
    }
    else
    {
        LOGI("JBL already ON\n");
    }

    LOGI("[NFC] init: wake_cause=%d\n", (int)esp_sleep_get_wakeup_cause());
    if (!nfcInit())
        LOGE("[NFC] PN532 not found during setup\n");
    LOGC("[BOOT] nfc_init=%s\n", nfcReady ? "OK" : "FAIL");
    LOGI("NFC %s\n", nfcReady ? "OK" : "FAIL");
    ledSetBootProgress(1); // NFC done

    if (sdReady)
    {
        loadMappings();
        loadSystemSounds();
    }
    playbackInit();
    LOGC("[BOOT] mappings=%d system_sounds=%d\n", (int)figurineMap.size(), (int)systemSoundMap.size());
    LOGI("Mappings loaded (%d)\n", figurineMap.size());
    ledSetBootProgress(2); // Mappings done

    if (playbackIsNfcMode() && nfcReady && sdReady)
    {
        char preUidBuf[30] = {};
        if (nfcPrescan(preUidBuf, sizeof(preUidBuf)))
        {
            String preUid = preUidBuf;
            LOGI("NFC pre-scan: %s\n", preUid.c_str());

            auto it = figurineMap.find(preUid);
            if (it != figurineMap.end())
            {
                String path = "/music/" + it->second;
                if (SD.exists(path))
                {
                    pendingPlaybackPath = path;
                    pendingPlaybackUid = preUid;
                    LOGI("Queued: %s\n", path.c_str());
                }
            }
        }
        else
        {
            LOGI("NFC pre-scan: no tag\n");
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
        LOGI("JBL power pulse done (%lu ms)\n", millis() - jblPulseStart);
    }

    ledSetBootProgress(3); // JBL done

    loadBtVolume();

    ledSetBootProgress(4); // BT step
    LOGI("BT A2DP starting -> %s\n", BT_SPEAKER_NAME);
    ledSetWaitBt(); // PRZED audioInit() - bo a2dp.begin() może blokować
    audioInit();

    btWaitStart = millis();
    if (jblNeedsPower) jblRecoveryDone = true;

    nfcStartTask();

    configureLoopWatchdog();  // NULL = current task (loop task)

    LOGC("[BOOT] bt_init=started loop_core=%d setup_ms=%lu\n", xPortGetCoreID(), millis() - bootStart);
    LOGI("BT A2DP initiated\n");
    LOGI("[BOOT] Loop task core: %d\n", xPortGetCoreID());
    LOGI("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);

    lastActivityMs = millis();
    LOGI("Ready! Waiting for BT connection...\n");
}

void loop()
{
    esp_task_wdt_reset();
    static volatile uint8_t loopStep = 0;
    static bool btDiscoveryFallbackDone = false;

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
        if (!isPlaying && !runtimeIsNightLight())
            ledSetWaitBt();
    }

    loopStep = 3;
    if (!btVolumeApplied && g_btConnected)
    {
        LOGI("BT connected!\n");
        btVolumeApplied = true;
        btDiscoveryFallbackDone = false;
        if (!runtimeIsNightLight())
        {
            applyBtVolume();
            ledSetIdle();
            playbackHandleBtConnected();
        }
    }

    loopStep = 4;
    if (!runtimeIsNightLight() && g_btConnected) {
        jblRecoveryDone = true;
    }
    else if (!runtimeIsNightLight() && !jblRecoveryDone && btWaitStart > 0 &&
             millis() - btWaitStart > 5000)
    {
        jblRecoveryDone = true;
        LOGW("[JBL] BT timeout - ADC false positive, pressing power\n");
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);   // 500ms, jednorazowe; ISR-y działają
        digitalWrite(JBL_POWER, LOW);
        LOGC("[RECOVERY] JBL recovery power pulse after BT timeout\n");
        LOGI("JBL power pulse (recovery)\n");
    }
    else if (!runtimeIsNightLight() && !g_btConnected && !btDiscoveryFallbackDone && btWaitStart > 0 &&
             millis() - btWaitStart > 15000)
    {
        btDiscoveryFallbackDone = true;
        LOGW("[BT] Initial reconnect timed out - restarting with discovery\n");
        if (audioRestartDiscovery())
        {
            btWaitStart = millis();
            ledSetWaitBt();
            LOGC("[RECOVERY] BT restart with discovery requested\n");
        }
    }

    loopStep = 5;
    handleButtons();
    if (runtimeIsNightLight())
        nightLightTick();
    else
        volumeTick();
    loopStep = 6;
    vTaskDelay(pdMS_TO_TICKS(5));

    loopStep = 7;
    if (!runtimeIsNightLight())
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
    if (!runtimeIsNightLight())
    {
        if (isPlaying)
            lastActivityMs = millis();
        else if (lastActivityMs > 0 && millis() - lastActivityMs > IDLE_TIMEOUT_MS)
        {
            LOGC("[SLEEP] Idle timeout - entering deep sleep\n");
            enterDeepSleep();
        }
    }

    loopStep = 9;
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        LOGI("[LOOP] alive step=%u isPlaying=%d btConn=%d\n", loopStep, (int)isPlaying, (int)g_btConnected);
        plogFlushToSd();
    }

    static unsigned long lastDiag = 0;
    if (millis() - lastDiag > 30000) {
        lastDiag = millis();
        LOGI("[DIAG] HWM loop=%u led=%u audio=%u nfc=%u\n",
            uxTaskGetStackHighWaterMark(NULL),
            ledGetTaskHWM(),
            audioGetTaskHWM(),
            nfcGetTaskHWM());
        LOGI("[DIAG] heap free=%u min=%u largest=%u\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
}
