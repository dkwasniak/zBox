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
#include "peripheral_power.h"
#include "sleep.h"
#include "sd_storage.h"
#include "state.h"
#include "sync_mode.h"
#include "volume.h"
#include "zbox_config.h"

namespace {
constexpr uint32_t NFC_RETRY_SHORT_DELAYS_MS[] = {500, 1500, 3000};
constexpr uint32_t NFC_RETRY_BACKOFF_MS = 30000;
static volatile bool s_nfcWanted = false;
static volatile bool s_nfcRuntimeStarted = false;
static TaskHandle_t s_nfcBootTaskHandle = nullptr;

void deferredNfcBootTask(void *param)
{
    (void)param;
    s_nfcBootTaskHandle = xTaskGetCurrentTaskHandle();
    nfcPowerSwitchOn();
    vTaskDelay(pdMS_TO_TICKS(NFC_BOOT_INIT_DELAY_MS));
    if (!s_nfcWanted) {
        nfcBusHiZForPowerOff();
        nfcPowerSwitchOff();
        s_nfcBootTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    bool ready = false;
    uint8_t attempt = 0;
    while (s_nfcWanted) {
        attempt++;
        LOGI("[NFC] deferred init attempt %u wake_cause=%d\n",
             (unsigned)attempt, (int)esp_sleep_get_wakeup_cause());
        ready = nfcInit();
        LOGC("[BOOT] nfc_init=%s deferred=1 attempt=%u\n",
             ready ? "OK" : "FAIL", (unsigned)attempt);

        if (ready)
            break;

        uint32_t delayMs = NFC_RETRY_BACKOFF_MS;
        if (attempt <= (sizeof(NFC_RETRY_SHORT_DELAYS_MS) / sizeof(NFC_RETRY_SHORT_DELAYS_MS[0]))) {
            delayMs = NFC_RETRY_SHORT_DELAYS_MS[attempt - 1];
        }
        LOGW("[NFC] deferred init failed, retry in %lu ms\n", (unsigned long)delayMs);
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }

    if (!s_nfcWanted) {
        if (ready) {
            nfcPrepareForPowerOff();
        }
        nfcBusHiZForPowerOff();
        nfcPowerSwitchOff();
        s_nfcBootTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(NFC_BOOT_MAPPING_DELAY_MS));
    if (!s_nfcWanted) {
        nfcPrepareForPowerOff();
        nfcBusHiZForPowerOff();
        nfcPowerSwitchOff();
        s_nfcBootTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    if (sdReady) {
        loadMappings();
        postEventFromTask(makeMappingsLoadedEvent(mappingCount()));
        LOGC("[BOOT] mappings=%d system_sounds=%d deferred=1\n",
             (int)mappingCount(), (int)systemSoundCount());
    }

    if (playbackIsNfcMode() && sdReady) {
        char preUidBuf[30] = {};
        if (nfcPrescan(preUidBuf, sizeof(preUidBuf))) {
            LOGI("NFC pre-scan: %s\n", preUidBuf);
            postEventFromTask(makeNfcPrescanEvent(true, preUidBuf));
        } else {
            LOGI("NFC pre-scan: no tag\n");
            postEventFromTask(makeNfcPrescanEvent(false, nullptr));
        }
    }

    nfcStartTask();
    s_nfcRuntimeStarted = true;
    s_nfcBootTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void startDeferredNfcBootTask()
{
    if (!ENABLE_NFC) return;
    s_nfcWanted = true;
    if (s_nfcRuntimeStarted || s_nfcBootTaskHandle) return;
    xTaskCreatePinnedToCore(deferredNfcBootTask, "nfc_boot", 4096, NULL, 1, &s_nfcBootTaskHandle, 1);
}

void stopNfcForMusicMode()
{
    if (!ENABLE_NFC) return;
    if (!s_nfcWanted && !s_nfcRuntimeStarted && !nfcIsReady()) return;
    s_nfcWanted = false;
    nfcStopTaskForSleep();
    s_nfcRuntimeStarted = false;
    nfcPrepareForPowerOff();
    nfcBusHiZForPowerOff();
    nfcPowerSwitchOff();
}

void syncNfcToPlaybackMode(PlaybackMode mode)
{
    if (!ENABLE_NFC || runtimeIsNightLight()) return;
    if (mode == PlaybackMode::Nfc) {
        startDeferredNfcBootTask();
    } else {
        stopNfcForMusicMode();
    }
}
}

static void configureLoopWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
#else
    esp_task_wdt_init(15, true);
#endif
    esp_task_wdt_add(nullptr);
}

void setup() {
    bootStart = millis();
#if defined(LOG_ENABLED)
    // Keep debug UART at the ROM/ESP-IDF boot baud so early boot and app logs
    // stay readable in one monitor session.
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
#else
    Serial.begin(115200);
#endif
    plogInit(false);
    LOGI("\n\n=== zBox ===\n");
    LOGI("Boot start\n");
    peripheralPowerInitEarly();

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

    if (!ENABLE_NFC) {
        LOGC("[BOOT] nfc_init=SKIPPED test_disabled=1\n");
        LOGI("NFC disabled for test\n");
    }

    if (sdReady) {
        loadSystemSounds();
    }
    playbackInit();
    postEventFromTask(makePlaybackModeLoadedEvent(playbackGetMode()));
    LOGC("[BOOT] system_sounds=%d mappings_deferred=%d\n",
         (int)systemSoundCount(), (ENABLE_NFC && playbackIsNfcMode()) ? 1 : 0);

    loadOutputVolume();
    postEventFromTask(makeVolumeLoadedEvent(getOutputVolumeLevel()));
    postEventFromTask(makeEvent(EventType::WakeCauseResolvedNormal));

    audioInit();
    applyOutputVolume();
    btAdapterInit();

    buttonAdapterStartTask();
    postEventFromTask(makeEvent(EventType::BootInitCompleted));
    dispatcherStartTask();
    configureLoopWatchdog();

    if (ENABLE_NFC && playbackIsNfcMode()) {
        startDeferredNfcBootTask();
    } else if (ENABLE_NFC) {
        LOGC("[BOOT] nfc_init=SKIPPED playback_mode=music\n");
    }

    LOGC("[BOOT] loop_core=%d setup_ms=%lu\n", xPortGetCoreID(), millis() - bootStart);
    LOGI("Ready! Local audio active.\n");
}

void loop() {
    esp_task_wdt_reset();
    const AppState snap = getDiagnosticSnapshot();
    if (snap.boot_state == BootState::Ready) {
        syncNfcToPlaybackMode(snap.playback_mode);
    }

    btAdapterPoll();
    vTaskDelay(pdMS_TO_TICKS(5));

    if (!runtimeIsNightLight() && ENABLE_NFC &&
        snap.playback_mode == PlaybackMode::Nfc && s_nfcRuntimeStarted) {
        nfcAdapterDrain();
    }

    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
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
