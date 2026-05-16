#include <Arduino.h>
#include <SD.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "audio.h"
#include "bt_adapter.h"
#include "button_adapter.h"
#include "diagnostics.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"
#include "leds.h"
#include "logging.h"
#include "nfc_adapter.h"
#include "nfc_module.h"
#include "night_light.h"
#include "persistent_log.h"
#include "playback.h"
#include "sleep.h"
#include "sd_storage.h"
#include "state.h"
#include "sync_mode.h"
#include "volume.h"
#include "zbox_config.h"

static void configureLoopWatchdog() {
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
    esp_task_wdt_add(nullptr);
}

void setup() {
    bootStart = millis();
    Serial.begin(115200);
    plogInit(false);
    LOGI("\n\n=== zBox ===\n");
    LOGI("Boot start\n");

    const WakeDecision wakeDecision = handleWakeFromDeepSleep();
    runtimeSetSessionMode(wakeDecision == WakeDecision::NIGHT_LIGHT
                              ? RuntimeSessionMode::NIGHT_LIGHT
                              : RuntimeSessionMode::NORMAL);
    bootStart = millis();

    ledInit();
    buttonAdapterInit();

    sdReady = initSD();
    if (!sdReady) {
        LOGW("[BOOT] No SD card\n");
    } else {
        ledLoadConfigFromSd();
    }
    LOGI("SD %s\n", sdReady ? "OK" : "FAIL");

    plogInit(sdReady);
    plogMark("CRIT", "BOOT");
    {
        const esp_reset_reason_t reason = esp_reset_reason();
        LOGC("[BOOT] reset_reason=%d(%s) wake_cause=%d sd=%s heap=%u min_heap=%u\n",
             (int)reason, resetReasonName(reason), (int)esp_sleep_get_wakeup_cause(),
             sdReady ? "OK" : "FAIL", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }

    const bool syncPending = sdReady && SD.exists(SYNC_PENDING_PATH);
    LOGI("[BOOT] sync_pending flag: %d\n", syncPending);
    if (syncPending) {
        LOGC("[BOOT] sync mode requested via %s\n", SYNC_PENDING_PATH);
        runSyncMode();
        return;
    }

    dispatcherInit();
    postEventFromTask(makeEvent(EventType::BootStarted));

    if (runtimeIsNightLight()) {
        LOGI("\n--- Night light mode ---\n");
        nightLightInit();
        audioInit();
        btAdapterInit();
        buttonAdapterStartTask();

        postEventFromTask(makeBrightnessLoadedEvent(nightLightGetBrightnessPercent()));
        postEventFromTask(makeEvent(EventType::WakeCauseResolvedNightLight));
        postEventFromTask(makeEvent(EventType::BootInitCompleted));
        dispatcherStartTask();
        configureLoopWatchdog();

        LOGC("[BOOT] night_light=1 loop_core=%d setup_ms=%lu\n",
             xPortGetCoreID(), millis() - bootStart);
        LOGI("Ready! Night light active.\n");
        return;
    }

    LOGI("\n--- Normal mode ---\n");

    LOGI("[NFC] init: wake_cause=%d\n", (int)esp_sleep_get_wakeup_cause());
    if (!nfcInit()) {
        LOGE("[NFC] PN532 not found during setup\n");
    }
    LOGC("[BOOT] nfc_init=%s\n", nfcIsReady() ? "OK" : "FAIL");
    LOGI("NFC %s\n", nfcIsReady() ? "OK" : "FAIL");

    if (sdReady) {
        loadMappings();
        loadSystemSounds();
        postEventFromTask(makeMappingsLoadedEvent(mappingCount()));
    }
    playbackInit();
    postEventFromTask(makePlaybackModeLoadedEvent(playbackGetMode()));
    LOGC("[BOOT] mappings=%d system_sounds=%d\n",
         (int)mappingCount(), (int)systemSoundCount());

    if (playbackIsNfcMode() && nfcIsReady() && sdReady) {
        char preUidBuf[30] = {};
        if (nfcPrescan(preUidBuf, sizeof(preUidBuf))) {
            LOGI("NFC pre-scan: %s\n", preUidBuf);
            postEventFromTask(makeNfcPrescanEvent(true, preUidBuf));
        } else {
            LOGI("NFC pre-scan: no tag\n");
            postEventFromTask(makeNfcPrescanEvent(false, nullptr));
        }
    }

    loadOutputVolume();
    postEventFromTask(makeVolumeLoadedEvent(getOutputVolumeLevel()));
    postEventFromTask(makeEvent(EventType::WakeCauseResolvedNormal));

    audioInit();
    applyOutputVolume();
    btAdapterInit();

    nfcStartTask();
    buttonAdapterStartTask();
    postEventFromTask(makeEvent(EventType::BootInitCompleted));
    dispatcherStartTask();
    configureLoopWatchdog();

    LOGC("[BOOT] loop_core=%d setup_ms=%lu\n", xPortGetCoreID(), millis() - bootStart);
    LOGI("Ready! Local audio active.\n");
}

void loop() {
    esp_task_wdt_reset();

    btAdapterPoll();
    vTaskDelay(pdMS_TO_TICKS(5));

    if (!runtimeIsNightLight()) {
        nfcAdapterDrain();
    }

    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        const AppState snap = getDiagnosticSnapshot();
        LOGI("[LOOP] alive audio=%d bt=%d out=%d\n",
             (int)snap.audio_state,
             (int)snap.bt_headphones_state,
             (int)snap.output_mode);
        plogFlushToSd();
    }

    static unsigned long lastDiag = 0;
    if (millis() - lastDiag > 30000) {
        lastDiag = millis();
        LOGI("[DIAG] HWM loop=%u led=%u audio=%u nfc=%u\n",
             uxTaskGetStackHighWaterMark(nullptr),
             ledGetTaskHWM(),
             audioGetTaskHWM(),
             nfcGetTaskHWM());
        LOGI("[DIAG] heap free=%u min=%u largest=%u\n",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
}
