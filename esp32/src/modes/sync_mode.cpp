#include "sync_mode.h"

#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "battery.h"
#include "diagnostics.h"
#include "audio.h"
#include "nfc_module.h"
#include "sleep.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <string.h>

static WebServer httpServer(SYNC_HTTP_PORT);
static File uploadFile;
static String uploadTempPath;
static String uploadFinalPath;
static String uploadError;
static uint32_t uploadSourceMtime = 0;
static bool uploadInProgress = false;
static size_t uploadBytesWritten = 0;
static unsigned long uploadStartedAtMs = 0;
static String syncHostname;
static String syncDeviceId;

static const char *SYNC_META_PATH = "/data/sync_meta.json";
static const char *SYNC_LOG_NAMES[] = {"debug.log", "debug.log.old"};
static const size_t SYNC_LOG_COUNT = sizeof(SYNC_LOG_NAMES) / sizeof(SYNC_LOG_NAMES[0]);

// ── BT scan state ────────────────────────────────────────────────────────────
// Results held in memory only — no SD write. Protected by s_scanMux because
// the GAP callback runs in the Bluedroid task while HTTP handlers run in the
// main loop (different execution contexts on the same core).

struct BtScanResult {
    char name[BT_SCAN_NAME_MAX];
    char mac[18]; // "AA:BB:CC:DD:EE:FF\0"
};

static portMUX_TYPE s_scanMux = portMUX_INITIALIZER_UNLOCKED;
static BtScanResult s_scanResults[BT_SCAN_MAX_RESULTS];
static int s_scanCount = 0;
static bool s_scanRunning = false;      // user intent: keep scanning
static bool s_btInquiryActive = false;  // BT stack is actively running an inquiry
static bool s_controllerInitialized = false;
static bool s_bluedroidInitialized = false;
static bool s_bleMemoryReleased = false;
static bool s_syncWatchdogRegistered = false;

static const int SYNC_RECENT_LOG_SIZE = 48;
static char s_recentLogBuf[SYNC_RECENT_LOG_SIZE][160];
// Volatile: written lock-free from any task/callback, readers accept occasional torn line.
static volatile int s_recentLogTotal = 0;

static void syncLogf(const char *fmt, ...)
{
    char line[224];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    Serial.println(line);

    int idx = s_recentLogTotal % SYNC_RECENT_LOG_SIZE;
    strlcpy(s_recentLogBuf[idx], line, sizeof(s_recentLogBuf[0]));
    s_recentLogTotal++;
}

// Lightweight log for BT GAP callbacks — NO Serial (Serial mutex blocks BT stack → SW reset).
static void btGapLogf(const char *fmt, ...)
{
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    int idx = s_recentLogTotal % SYNC_RECENT_LOG_SIZE;
    memcpy(s_recentLogBuf[idx], line, sizeof(line));
    s_recentLogTotal++;
}

static String buildDeviceId()
{
    uint64_t mac = ESP.getEfuseMac();
    char buf[17];
    snprintf(buf, sizeof(buf), "%04X%08lX",
             (uint16_t)(mac >> 32), (unsigned long)(mac & 0xFFFFFFFFULL));
    return String(buf);
}

static void clearSyncFlag()
{
    if (SD.exists(SYNC_PENDING_PATH))
        SD.remove(SYNC_PENDING_PATH);
}

// Called when BT scan was never used: free BT memory so WiFi gets more SRAM.
// If BT was initialized for scanning, we skip the release — BT memory stays
// reserved but that is acceptable since we'll restart before normal playback.
static void releaseBtMemoryIfUnused()
{
    if (s_controllerInitialized)
        return; // BT was (or is being) used — skip release
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
}

static void releaseBleMemoryForClassicBt()
{
    if (s_bleMemoryReleased || s_controllerInitialized)
        return;

    esp_err_t err = esp_bt_mem_release(ESP_BT_MODE_BLE);
    if (err == ESP_OK)
    {
        s_bleMemoryReleased = true;
        syncLogf("[BT] released BLE memory");
    }
    else
    {
        syncLogf("[BT] BLE memory release skipped: %d", err);
    }
}

static void cleanupBtForSyncExit()
{
    if (s_scanRunning || s_btInquiryActive)
    {
        esp_bt_gap_cancel_discovery();
        s_scanRunning = false;
        s_btInquiryActive = false;
        delay(100);
    }

    if (s_bluedroidInitialized)
    {
        esp_bluedroid_status_t bdStatus = esp_bluedroid_get_status();
        if (bdStatus == ESP_BLUEDROID_STATUS_ENABLED)
        {
            esp_err_t err = esp_bluedroid_disable();
            syncLogf("[BT] bluedroid disable: %d", err);
            unsigned long started = millis();
            while (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED &&
                   millis() - started < 1000)
            {
                esp_task_wdt_reset();
                delay(20);
            }
            bdStatus = esp_bluedroid_get_status();
        }

        if (bdStatus == ESP_BLUEDROID_STATUS_INITIALIZED)
        {
            esp_err_t err = esp_bluedroid_deinit();
            syncLogf("[BT] bluedroid deinit: %d", err);
            esp_task_wdt_reset();
        }
        s_bluedroidInitialized = false;
    }

    if (s_controllerInitialized)
    {
        esp_bt_controller_status_t ctlStatus = esp_bt_controller_get_status();
        if (ctlStatus == ESP_BT_CONTROLLER_STATUS_ENABLED)
        {
            esp_err_t err = esp_bt_controller_disable();
            syncLogf("[BT] controller disable: %d", err);
            unsigned long started = millis();
            while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED &&
                   millis() - started < 1000)
            {
                esp_task_wdt_reset();
                delay(20);
            }
            ctlStatus = esp_bt_controller_get_status();
        }

        if (ctlStatus == ESP_BT_CONTROLLER_STATUS_INITED)
        {
            esp_err_t err = esp_bt_controller_deinit();
            syncLogf("[BT] controller deinit: %d", err);
            esp_task_wdt_reset();
        }
        s_controllerInitialized = false;
    }
}

// ── GAP inquiry callback ─────────────────────────────────────────────────────

static void btScanGapCb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_DISC_RES_EVT)
    {
        char name[BT_SCAN_NAME_MAX] = {};

        // Pass 1: BDNAME property — authoritative, sent by most speakers
        for (int i = 0; i < param->disc_res.num_prop; i++)
        {
            esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
            if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0)
            {
                size_t copy = p->len < (BT_SCAN_NAME_MAX - 1)
                    ? p->len : (BT_SCAN_NAME_MAX - 1);
                memcpy(name, p->val, copy);
                name[copy] = '\0';
                break;
            }
        }

        // Pass 2: EIR — fallback for devices that omit BDNAME
        if (name[0] == '\0')
        {
            for (int i = 0; i < param->disc_res.num_prop; i++)
            {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR)
                {
                    uint8_t *rmt = nullptr;
                    uint8_t rmt_len = 0;
                    rmt = esp_bt_gap_resolve_eir_data((uint8_t *)p->val,
                        ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_len);
                    if (!rmt)
                        rmt = esp_bt_gap_resolve_eir_data((uint8_t *)p->val,
                            ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_len);
                    if (rmt && rmt_len > 0)
                    {
                        size_t copy = rmt_len < (BT_SCAN_NAME_MAX - 1)
                            ? rmt_len : (BT_SCAN_NAME_MAX - 1);
                        memcpy(name, rmt, copy);
                        name[copy] = '\0';
                    }
                    break;
                }
            }
        }

        if (name[0] == '\0')
            return; // no name in any property — skip

        // Format MAC address
        const uint8_t *bda = param->disc_res.bda;
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        portENTER_CRITICAL_SAFE(&s_scanMux);
        // Dedup by MAC
        bool duplicate = false;
        for (int i = 0; i < s_scanCount; i++)
        {
            if (strncmp(s_scanResults[i].mac, mac, 17) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && s_scanCount < BT_SCAN_MAX_RESULTS)
        {
            strlcpy(s_scanResults[s_scanCount].name, name, sizeof(s_scanResults[0].name));
            strlcpy(s_scanResults[s_scanCount].mac,  mac,  sizeof(s_scanResults[0].mac));
            s_scanCount++;
            portEXIT_CRITICAL_SAFE(&s_scanMux);
            btGapLogf("[BT] found: %s %s", name, mac);
        }
        else
        {
            portEXIT_CRITICAL_SAFE(&s_scanMux);
        }
    }
    else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT)
    {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
        {
            btGapLogf("[BT] inquiry cycle done, found=%d total", s_scanCount);
            s_btInquiryActive = false;
            // Restart is handled by the main loop — calling esp_bt_gap_start_discovery
            // from within this callback causes a BT stack assertion / SW reset.
        }
    }
}

static bool initBtForScan()
{
    if (s_controllerInitialized)
        return true;

    releaseBleMemoryForClassicBt();

    syncLogf("[BT] initializing controller...");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK)
    {
        syncLogf("[BT] controller init failed");
        return false;
    }
    esp_task_wdt_reset();
    s_controllerInitialized = true;

    if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK)
    {
        syncLogf("[BT] controller enable failed");
        return false;
    }
    esp_task_wdt_reset();

    syncLogf("[BT] initializing bluedroid...");
    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if (esp_bluedroid_init_with_cfg(&bd_cfg) != ESP_OK)
    {
        syncLogf("[BT] bluedroid init failed");
        return false;
    }
    esp_task_wdt_reset();
    s_bluedroidInitialized = true;

    if (esp_bluedroid_enable() != ESP_OK)
    {
        syncLogf("[BT] bluedroid enable failed");
        return false;
    }
    esp_task_wdt_reset();

    esp_bt_gap_register_callback(btScanGapCb);
    syncLogf("[BT] ready for inquiry");
    return true;
}

static void configureSyncWatchdog()
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
    if (!s_syncWatchdogRegistered)
    {
        esp_err_t err = esp_task_wdt_add(NULL);
        if (err == ESP_OK)
            s_syncWatchdogRegistered = true;
        else
            syncLogf("[WDT] add failed: %d", err);
    }
}

static void disableSyncWatchdog()
{
    if (!s_syncWatchdogRegistered)
        return;

    esp_err_t err = esp_task_wdt_delete(NULL);
    if (err == ESP_OK)
        s_syncWatchdogRegistered = false;
    else
        syncLogf("[WDT] delete failed: %d", err);
}

static bool isPathSafe(const String &path)
{
    return path.startsWith("/") && path.indexOf("..") < 0 &&
           path.indexOf('\\') < 0 && path.indexOf("//") < 0;
}

static bool isManagedPath(const String &path)
{
    if (!isPathSafe(path))
        return false;
    return path == "/data/mappings.json" ||
           path == "/data/system_sounds.json" ||
           path == "/data/led_config.json" ||
           path.startsWith("/music/") ||
           path.startsWith("/data/system/");
}

static bool isUploadPath(const String &path)
{
    if (!isManagedPath(path))
        return false;
    return path.startsWith("/music/") || path.startsWith("/data/system/");
}

static bool isSyncLogName(const String &name)
{
    for (size_t i = 0; i < SYNC_LOG_COUNT; i++)
    {
        if (name == SYNC_LOG_NAMES[i])
            return true;
    }
    return false;
}

static String syncLogPath(const String &name)
{
    if (!isSyncLogName(name))
        return String();
    return "/data/" + name;
}

static void ensureDataDirs()
{
    if (!SD.exists("/music"))
        SD.mkdir("/music");
    if (!SD.exists("/data"))
        SD.mkdir("/data");
    if (!SD.exists("/data/system"))
        SD.mkdir("/data/system");
}

static void loadSyncMeta(JsonDocument &doc)
{
    doc.clear();
    File f = SD.open(SYNC_META_PATH, FILE_READ);
    if (!f)
        return;
    deserializeJson(doc, f);
    f.close();
    if (!doc.is<JsonObject>())
        doc.to<JsonObject>();
}

static void saveSyncMeta(JsonDocument &doc)
{
    if (SD.exists(SYNC_META_PATH))
        SD.remove(SYNC_META_PATH);
    File f = SD.open(SYNC_META_PATH, FILE_WRITE);
    if (!f)
        return;
    serializeJson(doc, f);
    f.close();
}

static uint64_t getTrackedMtime(const String &path, File &file)
{
    JsonDocument doc;
    loadSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    if (root[path].is<uint64_t>())
        return root[path].as<uint64_t>();
    return (uint64_t)file.getLastWrite();
}

static void setTrackedMtime(const String &path, uint32_t mtime)
{
    JsonDocument doc;
    loadSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    root[path] = mtime;
    saveSyncMeta(doc);
}

static void removeTrackedMtime(const String &path)
{
    JsonDocument doc;
    loadSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    root.remove(path);
    saveSyncMeta(doc);
}

static void sendSyncSnapshot()
{
    BatteryReading bat = readBatteryReading();
    esp_reset_reason_t reason = esp_reset_reason();
    syncLogf("[SYNC] uptime_ms=%lu reset=%d(%s) ip=%s rssi=%d",
             millis(), (int)reason, resetReasonName(reason),
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    syncLogf("[SYNC] battery adc_pin_v=%.3f battery_v=%.3f bars=%d color=%s charging_known=false",
             bat.adcPinVoltage, bat.batteryVoltage, bat.bars, bat.color);
    syncLogf("[SYNC] heap free=%u min=%u largest=%u sketch_free=%u",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreeSketchSpace());
    syncLogf("[SYNC] hwm loop=%u led=%u audio=%u nfc=%u",
             uxTaskGetStackHighWaterMark(NULL), ledGetTaskHWM(), audioGetTaskHWM(), nfcGetTaskHWM());
}

static bool handleButtonExit()
{
    static bool armed = false;
    static unsigned long holdStart = 0;

    bool bothDown = digitalRead(BTN_A) == LOW && digitalRead(BTN_B) == LOW;
    if (!armed)
    {
        if (!bothDown)
            armed = true;
        return false;
    }

    if (!bothDown)
    {
        holdStart = 0;
        return false;
    }

    if (holdStart == 0)
        holdStart = millis();
    return millis() - holdStart >= LONG_PRESS_MS;
}

enum SyncSleepAction
{
    SYNC_SLEEP_NONE = 0,
    SYNC_SLEEP_NORMAL,
};

static SyncSleepAction handleSleepButton()
{
    static bool btnDHeld = false;
    static bool sleepArmed = false;
    static unsigned long holdStart = 0;

    bool dDown = digitalRead(BTN_D) == LOW;

    if (dDown)
    {
        if (!btnDHeld)
        {
            btnDHeld = true;
            sleepArmed = false;
            holdStart = millis();
        }

        unsigned long heldMs = millis() - holdStart;
        if (!sleepArmed && heldMs >= NIGHT_LIGHT_SLEEP_HOLD_MS)
        {
            sleepArmed = true;
            syncLogf("[SYNC] sleep armed - release BTN_D for deep sleep");
        }

        return SYNC_SLEEP_NONE;
    }

    if (btnDHeld)
    {
        bool triggerSleep = sleepArmed && (millis() - holdStart >= NIGHT_LIGHT_SLEEP_HOLD_MS);
        btnDHeld = false;
        sleepArmed = false;
        holdStart = 0;
        return triggerSleep ? SYNC_SLEEP_NORMAL : SYNC_SLEEP_NONE;
    }

    return SYNC_SLEEP_NONE;
}

static void handleBatteryCheckButton()
{
    static bool fired = false;
    static unsigned long holdStart = 0;

    bool cDown = digitalRead(BTN_C) == LOW;
    if (!cDown)
    {
        fired = false;
        holdStart = 0;
        return;
    }
    if (holdStart == 0)
        holdStart = millis();
    if (!fired && (millis() - holdStart) >= LONG_PRESS_MS)
    {
        fired = true;
        BatteryReading bat = readBatteryReading();
        syncLogf("[SYNC] battery check %.2fV %d bars (%s)", bat.batteryVoltage, bat.bars, bat.color);
    }
}

static void exitSyncMode(const char *reason)
{
    syncLogf("[SYNC] exit requested: %s", reason);
    configureSyncWatchdog();
    esp_task_wdt_reset();
    cleanupBtForSyncExit();
    ledFlashResult(true);
    clearSyncFlag();
    delay(500);
    ESP.restart();
}

static void sendJson(int code, JsonDocument &doc)
{
    String body;
    serializeJson(doc, body);
    httpServer.send(code, "application/json", body);
}

static void sendError(int code, const char *message)
{
    JsonDocument doc;
    doc["error"] = message;
    sendJson(code, doc);
}

static bool rejectWhileUploadBusy(const char *routeName)
{
    if (!uploadInProgress)
        return false;

    syncLogf("[SYNC] busy upload blocking route=%s target=%s bytes=%u",
             routeName,
             uploadFinalPath.c_str(),
             (unsigned int)uploadBytesWritten);
    sendError(503, "upload in progress");
    return true;
}

static bool requireSdReady()
{
    if (sdReady)
        return true;
    sendError(503, "sd not ready");
    return false;
}

static void appendFileInfo(JsonArray files, const String &path, File &file)
{
    JsonObject obj = files.add<JsonObject>();
    obj["path"] = path;
    obj["size"] = (uint64_t)file.size();
    obj["mtime"] = getTrackedMtime(path, file);
}

static void appendDirectoryFiles(JsonArray files, const char *dirPath)
{
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory())
        return;

    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
            break;
        if (!entry.isDirectory())
        {
            String path = entry.name();
            if (!path.startsWith("/"))
                path = String(dirPath) + "/" + path;
            appendFileInfo(files, path, entry);
        }
        entry.close();
    }
    dir.close();
}

static void populateStatus(JsonDocument &doc)
{
    BatteryReading bat = readBatteryReading();
    doc["device_id"] = syncDeviceId;
    doc["hostname"] = syncHostname;
    doc["mode"] = "sync";
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["sd_ok"] = sdReady;
    doc["battery_v"] = bat.batteryVoltage;
    doc["battery_bars"] = bat.bars;
    if (sdReady)
    {
        doc["sd_total"] = (uint64_t)SD.totalBytes();
        doc["sd_used"] = (uint64_t)SD.usedBytes();
    }
}

static void handleStatus()
{
    if (rejectWhileUploadBusy("status"))
        return;
    JsonDocument doc;
    populateStatus(doc);
    doc["upload_in_progress"] = uploadInProgress;
    if (uploadInProgress)
    {
        doc["upload_path"] = uploadFinalPath;
        doc["upload_bytes_written"] = (uint64_t)uploadBytesWritten;
        doc["upload_elapsed_ms"] = millis() - uploadStartedAtMs;
    }
    sendJson(200, doc);
}

static void handleFiles()
{
    if (rejectWhileUploadBusy("files"))
        return;
    if (!requireSdReady())
        return;

    JsonDocument doc;
    populateStatus(doc);
    JsonArray files = doc["files"].to<JsonArray>();
    appendDirectoryFiles(files, "/music");
    appendDirectoryFiles(files, "/data/system");

    const char *singles[] = {
        "/data/mappings.json",
        "/data/system_sounds.json",
        "/data/led_config.json",
    };
    for (const char *path : singles)
    {
        File f = SD.open(path, FILE_READ);
        if (f && !f.isDirectory())
            appendFileInfo(files, path, f);
        if (f)
            f.close();
    }
    sendJson(200, doc);
}

static bool writeJsonFile(const char *path, const String &body)
{
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
        return false;

    String tempPath = String(path) + ".tmp";
    if (SD.exists(tempPath))
        SD.remove(tempPath);

    File f = SD.open(tempPath, FILE_WRITE);
    if (!f)
        return false;
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0)
    {
        SD.remove(tempPath);
        return false;
    }

    if (SD.exists(path))
        SD.remove(path);
    if (!SD.rename(tempPath, path))
    {
        SD.remove(tempPath);
        return false;
    }
    return true;
}

static void handleWriteMappings()
{
    if (rejectWhileUploadBusy("write-mappings"))
        return;
    if (!requireSdReady())
        return;

    String body = httpServer.arg("plain");
    if (body.isEmpty())
    {
        sendError(400, "missing body");
        return;
    }
    if (!writeJsonFile("/data/mappings.json", body))
    {
        sendError(400, "invalid mappings json");
        return;
    }
    JsonDocument doc;
    doc["saved"] = "/data/mappings.json";
    sendJson(200, doc);
}

static void handleWriteSystemSounds()
{
    if (rejectWhileUploadBusy("write-system-sounds"))
        return;
    if (!requireSdReady())
        return;

    String body = httpServer.arg("plain");
    if (body.isEmpty())
    {
        sendError(400, "missing body");
        return;
    }
    if (!writeJsonFile("/data/system_sounds.json", body))
    {
        sendError(400, "invalid system_sounds json");
        return;
    }
    JsonDocument doc;
    doc["saved"] = "/data/system_sounds.json";
    sendJson(200, doc);
}

static void handleGetLedConfig()
{
    if (rejectWhileUploadBusy("get-config"))
        return;
    if (!requireSdReady())
        return;
    httpServer.send(200, "application/json", ledGetConfigJson());
}

static void handleSaveLedConfig()
{
    if (rejectWhileUploadBusy("save-config"))
        return;
    if (!requireSdReady())
        return;

    String body = httpServer.arg("plain");
    if (body.isEmpty())
    {
        sendError(400, "missing body");
        return;
    }
    if (!ledSaveConfigJson(body))
    {
        sendError(400, "invalid led config");
        return;
    }
    JsonDocument doc;
    doc["saved"] = "/data/led_config.json";
    sendJson(200, doc);
}

static void handleDeleteFile()
{
    if (rejectWhileUploadBusy("delete-file"))
        return;
    if (!requireSdReady())
        return;

    String path = httpServer.arg("path");
    if (!isManagedPath(path))
    {
        sendError(400, "invalid path");
        return;
    }
    if (SD.exists(path) && !SD.remove(path))
    {
        sendError(500, "delete failed");
        return;
    }
    removeTrackedMtime(path);

    JsonDocument doc;
    doc["deleted"] = path;
    sendJson(200, doc);
}

static void handleRestart()
{
    if (rejectWhileUploadBusy("restart"))
        return;
    JsonDocument doc;
    doc["restart"] = true;
    sendJson(200, doc);
    delay(200);
    exitSyncMode("http restart");
}

static void appendSyncLogInfo(JsonArray logs, const char *name)
{
    String path = syncLogPath(name);
    if (!path.length())
        return;

    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory())
    {
        if (f)
            f.close();
        return;
    }

    JsonObject obj = logs.add<JsonObject>();
    obj["name"] = name;
    obj["size"] = (uint64_t)f.size();
    obj["mtime"] = (uint64_t)f.getLastWrite();
    f.close();
}

static void handleLogs()
{
    if (rejectWhileUploadBusy("logs"))
        return;
    if (!requireSdReady())
        return;

    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    for (size_t i = 0; i < SYNC_LOG_COUNT; i++)
        appendSyncLogInfo(logs, SYNC_LOG_NAMES[i]);
    sendJson(200, doc);
}

static void handleLogContent()
{
    if (rejectWhileUploadBusy("log-content"))
        return;
    if (!requireSdReady())
        return;

    String name = httpServer.arg("name");
    if (!isSyncLogName(name))
    {
        sendError(400, "invalid log name");
        return;
    }

    int tail = httpServer.arg("tail").toInt();
    if (tail <= 0)
        tail = 200;
    if (tail > 500)
        tail = 500;

    String path = syncLogPath(name);
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory())
    {
        if (f)
            f.close();
        sendError(404, "log not found");
        return;
    }

    String *ring = new String[tail];
    int totalLines = 0;
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        while (line.endsWith("\n") || line.endsWith("\r"))
            line.remove(line.length() - 1);
        ring[totalLines % tail] = line;
        totalLines++;
    }
    f.close();

    int count = totalLines < tail ? totalLines : tail;
    int start = totalLines > tail ? (totalLines % tail) : 0;
    String text;
    for (int i = 0; i < count; i++)
    {
        const String &line = ring[(start + i) % tail];
        text += line;
        text += '\n';
    }
    delete[] ring;

    JsonDocument doc;
    doc["name"] = name;
    doc["text"] = text;
    doc["truncated"] = totalLines > tail;
    sendJson(200, doc);
}

static void handleLogDownload()
{
    if (rejectWhileUploadBusy("log-download"))
        return;
    if (!requireSdReady())
        return;

    String name = httpServer.arg("name");
    if (!isSyncLogName(name))
    {
        sendError(400, "invalid log name");
        return;
    }

    String path = syncLogPath(name);
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory())
    {
        if (f)
            f.close();
        sendError(404, "log not found");
        return;
    }

    httpServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    httpServer.streamFile(f, "text/plain; charset=utf-8");
    f.close();
}

static void handleUploadResult()
{
    if (!uploadError.isEmpty())
    {
        syncLogf("[SYNC] upload result error target=%s error=%s", uploadFinalPath.c_str(), uploadError.c_str());
        sendError(400, uploadError.c_str());
        uploadError = "";
        uploadInProgress = false;
        return;
    }

    syncLogf("[SYNC] upload result ok target=%s bytes=%u elapsed_ms=%lu",
             uploadFinalPath.c_str(),
             (unsigned int)uploadBytesWritten,
             millis() - uploadStartedAtMs);
    JsonDocument doc;
    doc["uploaded"] = uploadFinalPath;
    sendJson(200, doc);
    uploadInProgress = false;
}

static void handleUploadStream()
{
    HTTPUpload &upload = httpServer.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        esp_task_wdt_reset();
        uploadInProgress = true;
        uploadError = "";
        uploadTempPath = "";
        uploadFinalPath = "";
        uploadSourceMtime = 0;
        uploadBytesWritten = 0;
        uploadStartedAtMs = millis();
        uploadFile = File();

        if (!sdReady)
        {
            uploadError = "sd not ready";
            return;
        }

        uploadFinalPath = httpServer.arg("path");
        if (!isUploadPath(uploadFinalPath))
        {
            uploadError = "invalid upload path";
            return;
        }
        uploadSourceMtime = (uint32_t)httpServer.arg("mtime").toInt();
        syncLogf("[SYNC] upload start target=%s mtime=%u", uploadFinalPath.c_str(), (unsigned int)uploadSourceMtime);

        uploadTempPath = uploadFinalPath + ".tmp";
        if (SD.exists(uploadTempPath))
            SD.remove(uploadTempPath);
        uploadFile = SD.open(uploadTempPath, FILE_WRITE);
        if (!uploadFile)
            uploadError = "cannot open temp file";
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (uploadError.isEmpty() && uploadFile)
        {
            esp_task_wdt_reset();
            if (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize)
                uploadError = "write failed";
            else
                uploadBytesWritten += upload.currentSize;
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        esp_task_wdt_reset();
        if (uploadFile)
            uploadFile.close();
        if (uploadError.isEmpty())
        {
            if (SD.exists(uploadFinalPath))
                SD.remove(uploadFinalPath);
            if (!SD.rename(uploadTempPath, uploadFinalPath))
            {
                SD.remove(uploadTempPath);
                uploadError = "rename failed";
            }
            else if (uploadSourceMtime > 0)
            {
                setTrackedMtime(uploadFinalPath, uploadSourceMtime);
            }
            syncLogf("[SYNC] upload end target=%s bytes=%u elapsed_ms=%lu",
                     uploadFinalPath.c_str(),
                     (unsigned int)uploadBytesWritten,
                     millis() - uploadStartedAtMs);
        }
        else if (uploadTempPath.length())
        {
            SD.remove(uploadTempPath);
            syncLogf("[SYNC] upload failed target=%s error=%s bytes=%u",
                     uploadFinalPath.c_str(),
                     uploadError.c_str(),
                     (unsigned int)uploadBytesWritten);
        }
        uploadFile = File();
        uploadInProgress = false;
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        esp_task_wdt_reset();
        if (uploadFile)
            uploadFile.close();
        if (uploadTempPath.length())
            SD.remove(uploadTempPath);
        uploadError = "upload aborted";
        syncLogf("[SYNC] upload aborted target=%s bytes=%u elapsed_ms=%lu",
                 uploadFinalPath.c_str(),
                 (unsigned int)uploadBytesWritten,
                 millis() - uploadStartedAtMs);
        uploadFile = File();
        uploadInProgress = false;
    }
}

static void handleBtStartScan()
{
    if (s_scanRunning)
    {
        // Already scanning — return current state instead of starting a second inquiry
        // (double-calling esp_bt_gap_start_discovery while running crashes the BT stack)
        JsonDocument doc;
        doc["scanning"] = true;
        sendJson(200, doc);
        return;
    }

    if (!initBtForScan())
    {
        sendError(500, "bt init failed");
        return;
    }

    portENTER_CRITICAL_SAFE(&s_scanMux);
    s_scanCount = 0;
    portEXIT_CRITICAL_SAFE(&s_scanMux);

    // 30-second inquiry; portal polls /bt/devices and can stop early via /bt/stop-scan
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
    if (err != ESP_OK)
    {
        syncLogf("[BT] start_discovery failed: %d", err);
        sendError(500, "discovery start failed");
        return;
    }
    s_scanRunning = true;
    s_btInquiryActive = true;
    syncLogf("[BT] inquiry started (30s)");

    JsonDocument doc;
    doc["scanning"] = true;
    sendJson(200, doc);
}

static void handleBtStopScan()
{
    if (s_scanRunning)
    {
        esp_bt_gap_cancel_discovery();
        s_scanRunning = false;
        s_btInquiryActive = false;
    }

    portENTER_CRITICAL_SAFE(&s_scanMux);
    int count = s_scanCount;
    portEXIT_CRITICAL_SAFE(&s_scanMux);

    JsonDocument doc;
    doc["scanning"] = false;
    doc["count"]    = count;
    sendJson(200, doc);
}

static void handleBtDevices()
{
    // Copy results under lock, then build JSON outside the critical section
    BtScanResult local[BT_SCAN_MAX_RESULTS];
    int count = 0;

    portENTER_CRITICAL_SAFE(&s_scanMux);
    count = s_scanCount;
    if (count > 0)
        memcpy(local, s_scanResults, count * sizeof(BtScanResult));
    portEXIT_CRITICAL_SAFE(&s_scanMux);

    JsonDocument doc;
    doc["scanning"] = s_scanRunning;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (int i = 0; i < count; i++)
    {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = local[i].name;
        obj["mac"]  = local[i].mac;
    }
    sendJson(200, doc);
}

static void handleBtSelect()
{
    String body = httpServer.arg("plain");
    if (body.isEmpty())
    {
        sendError(400, "missing body");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok)
    {
        sendError(400, "invalid json");
        return;
    }

    const char *name = doc["name"] | "";
    const char *mac = doc["mac"] | "";
    size_t nameLen = strlen(name);
    if (nameLen == 0 || nameLen > 63)
    {
        sendError(400, "invalid name");
        return;
    }
    size_t macLen = strlen(mac);
    if (macLen != 0 && macLen != 17)
    {
        sendError(400, "invalid mac");
        return;
    }

    if (s_scanRunning)
    {
        esp_bt_gap_cancel_discovery();
        s_scanRunning = false;
    }

    Preferences prefs;
    prefs.begin("zbox", false);
    prefs.putString(BT_TARGET_NVS_KEY, name);
    if (macLen == 17)
        prefs.putString(BT_TARGET_MAC_NVS_KEY, mac);
    prefs.end();
    syncLogf("[BT] target saved: %s %s", name, macLen == 17 ? mac : "");

    JsonDocument resp;
    resp["saved"] = true;
    resp["name"]  = name;
    resp["mac"]   = macLen == 17 ? mac : "";
    sendJson(200, resp);
}

static void handleRecentLog()
{
    int since = httpServer.arg("since").toInt();
    if (since < 0) since = 0;

    int total = s_recentLogTotal;  // single volatile read — consistent snapshot
    int stored = total < SYNC_RECENT_LOG_SIZE ? total : SYNC_RECENT_LOG_SIZE;
    int oldestTotal = total - stored;
    int from = since < oldestTotal ? oldestTotal : since;

    JsonDocument doc;
    doc["total"] = total;
    JsonArray arr = doc["lines"].to<JsonArray>();
    for (int i = from; i < total; i++)
        arr.add((const char *)s_recentLogBuf[i % SYNC_RECENT_LOG_SIZE]);
    sendJson(200, doc);
}

static void handleNotFound()
{
    sendError(404, "not found");
}

static void setupHttpRoutes()
{
    httpServer.on("/diag/status", HTTP_GET, handleStatus);
    httpServer.on("/diag/files", HTTP_GET, handleFiles);
    httpServer.on("/diag/upload", HTTP_POST, handleUploadResult, handleUploadStream);
    httpServer.on("/diag/file", HTTP_DELETE, handleDeleteFile);
    httpServer.on("/diag/logs", HTTP_GET, handleLogs);
    httpServer.on("/diag/log-content", HTTP_GET, handleLogContent);
    httpServer.on("/diag/log-download", HTTP_GET, handleLogDownload);
    httpServer.on("/diag/write-mappings", HTTP_POST, handleWriteMappings);
    httpServer.on("/diag/write-system-sounds", HTTP_POST, handleWriteSystemSounds);
    httpServer.on("/diag/config", HTTP_GET, handleGetLedConfig);
    httpServer.on("/diag/config", HTTP_POST, handleSaveLedConfig);
    httpServer.on("/diag/restart", HTTP_POST, handleRestart);
    httpServer.on("/bt/start-scan", HTTP_POST, handleBtStartScan);
    httpServer.on("/bt/stop-scan",  HTTP_POST, handleBtStopScan);
    httpServer.on("/bt/devices",    HTTP_GET,  handleBtDevices);
    httpServer.on("/bt/select",     HTTP_POST, handleBtSelect);
    httpServer.on("/diag/recent-log", HTTP_GET, handleRecentLog);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();
}

static void setupOta()
{
    ArduinoOTA.setHostname(syncHostname.c_str());
    ArduinoOTA.setPort(SYNC_OTA_PORT);
    ArduinoOTA.onStart([]() {
        syncLogf("[OTA] start");
        ledSetSyncWifi();
    });
    ArduinoOTA.onEnd([]() {
        syncLogf("[OTA] complete");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        esp_task_wdt_reset();
        static unsigned int lastPct = 0;
        unsigned int pct = total ? (progress * 100U) / total : 0;
        if (pct >= lastPct + 10 || pct == 100)
        {
            lastPct = pct;
            syncLogf("[OTA] %u%%", pct);
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        syncLogf("[OTA] error=%u", (unsigned int)error);
    });
    ArduinoOTA.begin();
}

void runSyncMode()
{
    LOGLN("\n=== zBox SYNC MODE ===\n");
    ledSetSyncWifi();
    // BT memory is intentionally NOT released here. Releasing it would prevent
    // on-demand BT scanning from the web portal. The ~40KB reservation is
    // acceptable with PSRAM available for heap allocations.
    ensureDataDirs();

    syncDeviceId = buildDeviceId();
    syncHostname = WIFI_HOSTNAME;

    WiFiManager wm;
    wm.setHostname(WIFI_HOSTNAME);
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);
    wm.setAPCallback([](WiFiManager *mgr) {
        (void)mgr;
        syncLogf("[SYNC] WiFi connect failed - starting config portal SSID=%s ip=%s",
                 SYNC_AP_NAME, WiFi.softAPIP().toString().c_str());
    });

    disableSyncWatchdog();
    syncLogf("[SYNC] Connecting to stored WiFi or starting config portal");

    if (!wm.autoConnect(SYNC_AP_NAME))
    {
        syncLogf("[SYNC] Config portal timed out - returning to normal mode");
        LOGLN("[SYNC] WiFi not connected");
        configureSyncWatchdog();
        esp_task_wdt_reset();
        ledFlashResult(false);
        clearSyncFlag();
        delay(500);
        ESP.restart();
    }

    configureSyncWatchdog();
    syncLogf("[SYNC] WiFi connected ssid=%s ip=%s",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    setupHttpRoutes();
    setupOta();

    LOG("[SYNC] device=%s ip=%s ota=%d http=%d\n",
        syncDeviceId.c_str(), WiFi.localIP().toString().c_str(),
        SYNC_OTA_PORT, SYNC_HTTP_PORT);

    unsigned long lastSnapshot = 0;
    for (;;)
    {
        esp_task_wdt_reset();
        ArduinoOTA.handle();
        httpServer.handleClient();

        if (handleButtonExit())
            exitSyncMode("buttons A+B long");

        SyncSleepAction sleepAction = handleSleepButton();
        if (sleepAction == SYNC_SLEEP_NORMAL)
        {
            syncLogf("[SYNC] deep sleep requested by BTN_D");
            enterDeepSleep();
        }

        handleBatteryCheckButton();

        // Restart BT inquiry from the main task if the previous cycle ended naturally.
        // Must NOT be done from within the GAP callback — that causes a BT stack assert.
        if (s_scanRunning && !s_btInquiryActive)
        {
            esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
            if (err == ESP_OK)
            {
                s_btInquiryActive = true;
                syncLogf("[BT] inquiry restarted");
            }
            else
            {
                syncLogf("[BT] restart failed: %d", err);
            }
        }

        unsigned long now = millis();
        if (now - lastSnapshot >= SYNC_LOG_INTERVAL_MS)
        {
            sendSyncSnapshot();
            lastSnapshot = now;
        }

        delay(20);
    }
}
