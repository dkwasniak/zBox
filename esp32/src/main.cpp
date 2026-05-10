#include <Arduino.h>
#include <SD.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

#include "persistent_log.h"

#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "jbl.h"
#include "volume.h"
#include "sd_storage.h"
#include "audio.h"
#include "nfc_module.h"
#include "playback.h"
#include "sleep.h"
#include "diagnostics.h"
#include "sync_mode.h"
#include "night_light.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"
#include "bt_adapter.h"
#include "nfc_adapter.h"
#include "button_adapter.h"

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
    buttonAdapterInit();

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

    bool syncPending = SD.exists(SYNC_PENDING_PATH);
    LOGI("[BOOT] sync_pending flag: %d\n", syncPending);
    if (syncPending)
        LOGC("[BOOT] sync mode requested via %s\n", SYNC_PENDING_PATH);

    if (syncPending)
    {
        runSyncMode();
        return;
    }

    dispatcherInit();  // creates event queue; must precede any adapter that posts events

    if (!runtimeIsNightLight())
        ledSetBootProgress(0); // SD done

    if (runtimeIsNightLight())
    {
        LOGI("\n--- Night light mode ---\n");
        LOGI("[NIGHT] Booting night light session\n");
        LOGI("[NIGHT] Skipping JBL wake pulse for night light session\n");

        nightLightInit();
        audioInit();
        btAdapterInit();
        buttonAdapterStartTask();
        btWaitStart = millis();

        postEventFromTask(makeEvent(EventType::WakeCauseResolvedNightLight));
        postEventFromTask(makeBrightnessLoadedEvent(nightLightGetBrightnessPercent()));
        postEventFromTask(makeEvent(EventType::BootInitCompleted));
        dispatcherStartTask();
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
    dispatcherSetInitialPlaybackMode(playbackGetMode());
    LOGC("[BOOT] mappings=%d system_sounds=%d\n", (int)figurineMap.size(), (int)systemSoundMap.size());
    LOGI("Mappings loaded (%d)\n", figurineMap.size());
    ledSetBootProgress(2); // Mappings done

    if (playbackIsNfcMode() && nfcReady && sdReady)
    {
        char preUidBuf[30] = {};
        if (nfcPrescan(preUidBuf, sizeof(preUidBuf)))
        {
            LOGI("NFC pre-scan: %s\n", preUidBuf);
            postEventFromTask(makeNfcPrescanEvent(true, preUidBuf));
        }
        else
        {
            LOGI("NFC pre-scan: no tag\n");
            postEventFromTask(makeNfcPrescanEvent(false, nullptr));
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
    dispatcherSetInitialVolume(getBtVolume());

    ledSetBootProgress(4); // BT step
    LOGI("BT A2DP starting -> %s\n", BT_SPEAKER_NAME);
    ledSetWaitBt(); // BEFORE audioInit() - because a2dp.begin() may block
    audioInit();
    btAdapterInit();

    btWaitStart = millis();
    if (jblNeedsPower) jblRecoveryDone = true;

    nfcStartTask();
    buttonAdapterStartTask();
    postEventFromTask(makeEvent(EventType::BootInitCompleted));
    dispatcherStartTask();

    configureLoopWatchdog();  // NULL = current task (loop task)

    LOGC("[BOOT] bt_init=started loop_core=%d setup_ms=%lu\n", xPortGetCoreID(), millis() - bootStart);
    LOGI("BT A2DP initiated\n");
    LOGI("[BOOT] Loop task core: %d\n", xPortGetCoreID());
    LOGI("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);

    LOGI("Ready! Waiting for BT connection...\n");
}

void loop()
{
    esp_task_wdt_reset();
    static volatile uint8_t loopStep = 0;
    static bool btDiscoveryFallbackDone = false;

    btAdapterPoll();

    loopStep = 4;
    {
        bool btConnected = getDiagnosticSnapshot().bt_state == BtState::Connected;
        if (!runtimeIsNightLight() && btConnected) {
            jblRecoveryDone = true;
            btDiscoveryFallbackDone = false;
        }
        else if (!runtimeIsNightLight() && !jblRecoveryDone && btWaitStart > 0 &&
                 millis() - btWaitStart > 5000)
        {
            jblRecoveryDone = true;
            LOGW("[JBL] BT timeout - ADC false positive, pressing power\n");
            digitalWrite(JBL_POWER, HIGH);
            delay(JBL_POWER_PRESS_MS);   // 500ms, one-time; ISRs remain active
            digitalWrite(JBL_POWER, LOW);
            LOGC("[RECOVERY] JBL recovery power pulse after BT timeout\n");
            LOGI("JBL power pulse (recovery)\n");
        }
        else if (!runtimeIsNightLight() && !btConnected && !btDiscoveryFallbackDone && btWaitStart > 0 &&
                 millis() - btWaitStart > 15000)
        {
            btDiscoveryFallbackDone = true;
            LOGW("[BT] Initial reconnect timed out - restarting with discovery\n");
            if (audioRestartDiscovery())
            {
                btWaitStart = millis();
                LOGC("[RECOVERY] BT restart with discovery requested\n");
            }
        }
    }

    loopStep = 5;
    vTaskDelay(pdMS_TO_TICKS(5));

    loopStep = 6;
    if (!runtimeIsNightLight())
        nfcAdapterDrain();

    loopStep = 7;
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        AppState snap = getDiagnosticSnapshot();
        LOGI("[LOOP] alive step=%u audio=%d bt=%d\n", loopStep,
             (int)snap.audio_state, (int)snap.bt_state);
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
