#include "diagnostic_mode.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "battery.h"
#include "diagnostics.h"
#include "audio.h"
#include "nfc_module.h"
#include "buttons_isr.h"
#include "sleep.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <esp_idf_version.h>
#include <esp_bt.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <string.h>

static WebServer httpServer(DIAG_HTTP_PORT);
static File uploadFile;
static String uploadTempPath;
static String uploadFinalPath;
static String uploadError;
static uint32_t uploadSourceMtime = 0;
static String diagHostname;
static String diagDeviceId;

static const char *DIAG_SYNC_META_PATH = "/data/diag_sync_meta.json";
static const char *DIAG_LOG_NAMES[] = {"debug.log", "debug.log.old"};
static const size_t DIAG_LOG_COUNT = sizeof(DIAG_LOG_NAMES) / sizeof(DIAG_LOG_NAMES[0]);

static void diagLogf(const char *fmt, ...)
{
    char uptime[16];
    char msg[192];
    char line[224];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    formatUptime(uptime, sizeof(uptime), millis());
    snprintf(line, sizeof(line), "[%s] %s", uptime, msg);
    Serial.println(line);
}

static String buildDeviceId()
{
    uint64_t mac = ESP.getEfuseMac();
    char buf[17];
    snprintf(buf, sizeof(buf), "%04X%08lX",
             (uint16_t)(mac >> 32), (unsigned long)(mac & 0xFFFFFFFFULL));
    return String(buf);
}

static void clearDiagnosticFlag()
{
    if (SD.exists(DIAG_PENDING_PATH))
        SD.remove(DIAG_PENDING_PATH);
}

static void disableBtForDiagnostic()
{
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
}

static void configureDiagnosticWatchdog()
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

static void disableDiagnosticWatchdog()
{
    esp_task_wdt_delete(NULL);
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

static bool isDiagnosticLogName(const String &name)
{
    for (size_t i = 0; i < DIAG_LOG_COUNT; i++)
    {
        if (name == DIAG_LOG_NAMES[i])
            return true;
    }
    return false;
}

static String diagnosticLogPath(const String &name)
{
    if (!isDiagnosticLogName(name))
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

static void loadDiagSyncMeta(JsonDocument &doc)
{
    doc.clear();
    File f = SD.open(DIAG_SYNC_META_PATH, FILE_READ);
    if (!f)
        return;
    deserializeJson(doc, f);
    f.close();
    if (!doc.is<JsonObject>())
        doc.to<JsonObject>();
}

static void saveDiagSyncMeta(JsonDocument &doc)
{
    if (SD.exists(DIAG_SYNC_META_PATH))
        SD.remove(DIAG_SYNC_META_PATH);
    File f = SD.open(DIAG_SYNC_META_PATH, FILE_WRITE);
    if (!f)
        return;
    serializeJson(doc, f);
    f.close();
}

static uint64_t getTrackedMtime(const String &path, File &file)
{
    JsonDocument doc;
    loadDiagSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    if (root[path].is<uint64_t>())
        return root[path].as<uint64_t>();
    return (uint64_t)file.getLastWrite();
}

static void setTrackedMtime(const String &path, uint32_t mtime)
{
    JsonDocument doc;
    loadDiagSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    root[path] = mtime;
    saveDiagSyncMeta(doc);
}

static void removeTrackedMtime(const String &path)
{
    JsonDocument doc;
    loadDiagSyncMeta(doc);
    JsonObject root = doc.as<JsonObject>();
    root.remove(path);
    saveDiagSyncMeta(doc);
}

static void sendDiagnosticSnapshot()
{
    BatteryReading bat = readBatteryReading();
    esp_reset_reason_t reason = esp_reset_reason();
    diagLogf("[DIAG] uptime_ms=%lu reset=%d(%s) ip=%s rssi=%d",
             millis(), (int)reason, resetReasonName(reason),
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    diagLogf("[DIAG] battery adc_pin_v=%.3f battery_v=%.3f bars=%d color=%s charging_known=false",
             bat.adcPinVoltage, bat.batteryVoltage, bat.bars, bat.color);
    diagLogf("[DIAG] heap free=%u min=%u largest=%u sketch_free=%u",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreeSketchSpace());
    diagLogf("[DIAG] hwm loop=%u led=%u audio=%u nfc=%u",
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

enum DiagSleepAction
{
    DIAG_SLEEP_NONE = 0,
    DIAG_SLEEP_NORMAL,
    DIAG_SLEEP_EMERGENCY
};

static DiagSleepAction handleSleepButton()
{
    Button &btnC = buttons[2];
    bool cDown = digitalRead(btnC.pin) == LOW;
    bool dDown = digitalRead(BTN_D) == LOW;

    if (cDown && !dDown && btnC.pressed && btnC.pressStart == 0)
    {
        btnC.pressStart = millis();
        btnC.longHandled = false;
        btnC.clickSuppressed = true;
    }

    if (cDown && !dDown && btnC.pressStart > 0)
    {
        unsigned long heldMs = millis() - btnC.pressStart;
        if (!btnC.longHandled && heldMs >= EMERGENCY_SLEEP_MS)
        {
            btnC.longHandled = true;
            btnC.pressed = false;
            diagLogf("[DIAG] emergency sleep requested by long BTN_C");
            return DIAG_SLEEP_EMERGENCY;
        }

        if (!btnC.longHandled && heldMs >= LONG_PRESS_MS)
        {
            btnC.longHandled = true;
            diagLogf("[DIAG] sleep armed - release BTN_C for deep sleep");
            ledSetSleepReady();
        }

        return DIAG_SLEEP_NONE;
    }

    if (!cDown && btnC.pressStart > 0)
    {
        unsigned long heldMs = millis() - btnC.pressStart;
        DiagSleepAction action = (btnC.longHandled && heldMs >= LONG_PRESS_MS) ? DIAG_SLEEP_NORMAL : DIAG_SLEEP_NONE;
        btnC.pressed = false;
        btnC.longHandled = false;
        btnC.pressStart = 0;
        return action;
    }

    if (!cDown)
        btnC.pressed = false;

    return DIAG_SLEEP_NONE;
}

static void exitDiagnosticMode(const char *reason)
{
    diagLogf("[DIAG] exit requested: %s", reason);
    ledFlashDiagnosticTransition(false);
    clearDiagnosticFlag();
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
    doc["device_id"] = diagDeviceId;
    doc["hostname"] = diagHostname;
    doc["mode"] = "diagnostic";
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
    JsonDocument doc;
    populateStatus(doc);
    sendJson(200, doc);
}

static void handleFiles()
{
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
    if (!requireSdReady())
        return;
    httpServer.send(200, "application/json", ledGetConfigJson());
}

static void handleSaveLedConfig()
{
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
    JsonDocument doc;
    doc["restart"] = true;
    sendJson(200, doc);
    delay(200);
    exitDiagnosticMode("http restart");
}

static void appendDiagnosticLogInfo(JsonArray logs, const char *name)
{
    String path = diagnosticLogPath(name);
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
    if (!requireSdReady())
        return;

    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    for (size_t i = 0; i < DIAG_LOG_COUNT; i++)
        appendDiagnosticLogInfo(logs, DIAG_LOG_NAMES[i]);
    sendJson(200, doc);
}

static void handleLogContent()
{
    if (!requireSdReady())
        return;

    String name = httpServer.arg("name");
    if (!isDiagnosticLogName(name))
    {
        sendError(400, "invalid log name");
        return;
    }

    int tail = httpServer.arg("tail").toInt();
    if (tail <= 0)
        tail = 200;
    if (tail > 500)
        tail = 500;

    String path = diagnosticLogPath(name);
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
    if (!requireSdReady())
        return;

    String name = httpServer.arg("name");
    if (!isDiagnosticLogName(name))
    {
        sendError(400, "invalid log name");
        return;
    }

    String path = diagnosticLogPath(name);
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
        sendError(400, uploadError.c_str());
        uploadError = "";
        return;
    }

    JsonDocument doc;
    doc["uploaded"] = uploadFinalPath;
    sendJson(200, doc);
}

static void handleUploadStream()
{
    HTTPUpload &upload = httpServer.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        esp_task_wdt_reset();
        uploadError = "";
        uploadTempPath = "";
        uploadFinalPath = "";
        uploadSourceMtime = 0;
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
        }
        else if (uploadTempPath.length())
        {
            SD.remove(uploadTempPath);
        }
        uploadFile = File();
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        esp_task_wdt_reset();
        if (uploadFile)
            uploadFile.close();
        if (uploadTempPath.length())
            SD.remove(uploadTempPath);
        uploadError = "upload aborted";
        uploadFile = File();
    }
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
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();
}

static void setupOta()
{
    ArduinoOTA.setHostname(diagHostname.c_str());
    ArduinoOTA.setPort(DIAG_OTA_PORT);
    ArduinoOTA.onStart([]() {
        diagLogf("[OTA] start");
        ledSetDiagnostic();
    });
    ArduinoOTA.onEnd([]() {
        diagLogf("[OTA] complete");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        esp_task_wdt_reset();  // flash write blokuje pętlę — resetuj WDT przy każdym chunku
        static unsigned int lastPct = 0;
        unsigned int pct = total ? (progress * 100U) / total : 0;
        if (pct >= lastPct + 10 || pct == 100)
        {
            lastPct = pct;
            diagLogf("[OTA] %u%%", pct);
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        diagLogf("[OTA] error=%u", (unsigned int)error);
    });
    ArduinoOTA.begin();
}

void runDiagnosticMode()
{
    LOGLN("\n=== MusicBox DIAGNOSTIC MODE ===\n");
    ledFlashDiagnosticTransition(true);
    ledSetDiagnostic();
    disableBtForDiagnostic();
    ensureDataDirs();

    diagDeviceId = buildDeviceId();
    diagHostname = String("zbox-") + diagDeviceId.substring(max(0, (int)diagDeviceId.length() - 6));

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);
    wm.setAPCallback([](WiFiManager *mgr) {
        (void)mgr;
        diagLogf("[DIAG] WiFi connect failed - starting config portal SSID=%s ip=%s",
                 DIAG_AP_NAME, WiFi.softAPIP().toString().c_str());
    });

    disableDiagnosticWatchdog();
    diagLogf("[DIAG] Connecting to stored WiFi or starting config portal");

    if (!wm.autoConnect(DIAG_AP_NAME))
    {
        diagLogf("[DIAG] Config portal timed out - returning to normal mode");
        LOGLN("[DIAG] WiFi not connected");
        ledFlashResult(false);
        clearDiagnosticFlag();
        delay(1000);
        ESP.restart();
    }

    configureDiagnosticWatchdog();
    diagLogf("[DIAG] WiFi connected ssid=%s ip=%s",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    setupHttpRoutes();
    setupOta();

    LOG("[DIAG] device=%s ip=%s ota=%d http=%d\n",
        diagDeviceId.c_str(), WiFi.localIP().toString().c_str(),
        DIAG_OTA_PORT, DIAG_HTTP_PORT);

    unsigned long lastSnapshot = 0;
    for (;;)
    {
        esp_task_wdt_reset();
        ArduinoOTA.handle();
        httpServer.handleClient();

        if (handleButtonExit())
            exitDiagnosticMode("buttons A+B long");
        DiagSleepAction sleepAction = handleSleepButton();
        if (sleepAction == DIAG_SLEEP_NORMAL)
        {
            diagLogf("[DIAG] deep sleep requested by BTN_C");
            enterDeepSleep();
        }
        else if (sleepAction == DIAG_SLEEP_EMERGENCY)
        {
            enterEmergencyDeepSleep();
        }

        unsigned long now = millis();
        if (now - lastSnapshot >= DIAG_LOG_INTERVAL_MS)
        {
            sendDiagnosticSnapshot();
            lastSnapshot = now;
        }

        delay(20);
    }
}
