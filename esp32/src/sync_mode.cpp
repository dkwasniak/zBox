#include "sync_mode.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "sd_storage.h"
#include "helpers.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <map>
#include <set>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <esp_bt.h>

static String syncServerIP;

static void loadSyncMeta(std::map<String, uint32_t> &meta)
{
    meta.clear();
    JsonDocument doc;
    if (!readJsonFromSd("/data/sync_meta.json", doc)) return;
    for (JsonPair kv : doc.as<JsonObject>())
        meta[kv.key().c_str()] = kv.value().as<uint32_t>();
}

static void saveSyncMeta(const std::map<String, uint32_t> &meta)
{
    if (SD.exists("/data/sync_meta.json")) SD.remove("/data/sync_meta.json");
    File f = SD.open("/data/sync_meta.json", FILE_WRITE);
    if (!f) return;
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    for (auto &kv : meta) obj[kv.first] = kv.second;
    serializeJson(doc, f);
    f.close();
}

static bool ensureHttpConnected(WiFiClient& client, const String& ip)
{
    if (client.connected()) return true;
    if (!client.connect(ip.c_str(), SERVER_PORT)) return false;
    client.setNoDelay(true);
    client.setTimeout(HTTP_TIMEOUT);
    return true;
}

static bool readHttpHeaders(WiFiClient &client, int &outContentLength)
{
    String statusLine = client.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0) return false;
    outContentLength = -1;
    while (client.connected())
    {
        String line = client.readStringUntil('\n');
        if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
            outContentLength = line.substring(line.indexOf(':') + 1).toInt();
        if (line == "\r" || line.length() == 0) break;
    }
    return true;
}

static String httpGet(WiFiClient &client, const String &path)
{
    if (!ensureHttpConnected(client, syncServerIP))
    {
        LOG("[SYNC] Reconnect failed for GET %s\n", path.c_str());
        return "";
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n",
                  path.c_str(), syncServerIP.c_str());

    unsigned long start = millis();
    while (client.connected() && !client.available())
    {
        if (millis() - start > HTTP_TIMEOUT)
        {
            LOGLN("[SYNC] Timeout waiting for response");
            client.stop();
            return "";
        }
        delay(10);
    }

    int contentLength = -1;
    if (!readHttpHeaders(client, contentLength))
    {
        LOG("[SYNC] HTTP error on GET %s\n", path.c_str());
        client.stop();
        return "";
    }

    String body;
    if (contentLength > 0)
    {
        body.reserve(contentLength);
        int bytesRead = 0;
        uint8_t buf[512];
        unsigned long lastData = millis();
        while (bytesRead < contentLength)
        {
            int avail = client.available();
            if (avail > 0)
            {
                lastData = millis();
                int toRead = min(avail, min((int)sizeof(buf), contentLength - bytesRead));
                int got = client.readBytes(buf, toRead);
                body.concat((char *)buf, got);
                bytesRead += got;
            }
            else
            {
                if (millis() - lastData > HTTP_TIMEOUT) break;
                delay(1);
            }
        }
    }
    else
    {
        body = client.readString();
    }

    return body;
}

static bool syncDownloadFile(WiFiClient &client, const String &urlPath, const String &sdPath,
                      uint32_t totalExpectedBytes, uint32_t &syncBytesDownloaded, uint32_t &lastLedUpdate)
{
    LOG("[SYNC] Download: %s\n", sdPath.c_str());

    if (!ensureHttpConnected(client, syncServerIP))
    {
        LOGLN("[SYNC] Reconnect failed");
        return false;
    }

    unsigned long dlStart = millis();
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n",
                  urlPath.c_str(), syncServerIP.c_str());
    while (client.connected() && !client.available())
    {
        if (millis() - dlStart > HTTP_TIMEOUT)
        {
            LOGLN("[SYNC] Timeout waiting for response");
            client.stop();
            return false;
        }
        delay(10);
    }

    int contentLength = -1;
    if (!readHttpHeaders(client, contentLength))
    {
        LOG("[SYNC] HTTP error downloading %s\n", sdPath.c_str());
        client.stop();
        return false;
    }

    LOG("[SYNC] Size: %d bytes\n", contentLength);

    if (SD.exists(sdPath)) SD.remove(sdPath);
    File f = SD.open(sdPath, FILE_WRITE);
    if (!f) { LOG("[SYNC] Cannot create %s\n", sdPath.c_str()); client.stop(); return false; }

    static uint8_t buf[DOWNLOAD_BUF_SIZE];
    int bufPos = 0;
    int totalWritten = 0;
    int lastLoggedKB = 0;
    unsigned long lastDataMs = millis();
    unsigned long timeReading = 0;
    unsigned long timeWriting = 0;
    unsigned long timeWaiting = 0;

    uint32_t zeroAvailCount = 0;
    uint32_t nonZeroAvailCount = 0;
    uint32_t minAvail = UINT32_MAX;
    uint32_t maxAvail = 0;
    unsigned long lastDataReceivedMs = 0;
    unsigned long maxGapMs = 0;

    while (client.connected() || client.available())
    {
        int available = client.available();
        if (available > 0)
        {
            lastDataMs = millis();
            nonZeroAvailCount++;
            if ((uint32_t)available < minAvail) minAvail = (uint32_t)available;
            if ((uint32_t)available > maxAvail) maxAvail = (uint32_t)available;
            unsigned long _now = millis();
            if (lastDataReceivedMs > 0 && _now - lastDataReceivedMs > maxGapMs)
                maxGapMs = _now - lastDataReceivedMs;
            lastDataReceivedMs = _now;
            int toRead = min(available, DOWNLOAD_BUF_SIZE - bufPos);
            unsigned long t0 = millis();
            int got = client.readBytes((char *)(buf + bufPos), toRead);
            timeReading += millis() - t0;
            bufPos += got;
            totalWritten += got;

            // Byte-based LED progress
            syncBytesDownloaded += got;
            if (totalExpectedBytes > 0 && syncBytesDownloaded / 32768 > lastLedUpdate / 32768)
            {
                lastLedUpdate = syncBytesDownloaded;
                ledSetSyncProgress(syncBytesDownloaded, totalExpectedBytes);
            }

            if (bufPos >= DOWNLOAD_BUF_SIZE)
            {
                unsigned long tw = millis();
                f.write(buf, bufPos);
                timeWriting += millis() - tw;
                bufPos = 0;
                vTaskDelay(pdMS_TO_TICKS(1)); // yield po zapisie — IDLE task reset WDT
            }

            if (contentLength > 0 && totalWritten >= contentLength)
                break;

            int currentKB = totalWritten / 1024;
            if (currentKB / 500 > lastLoggedKB / 500)
            {
                lastLoggedKB = currentKB;
                if (contentLength > 0)
                    LOG("[SYNC] %d / %d KB\n", currentKB, contentLength / 1024);
                else
                    LOG("[SYNC] %d KB\n", currentKB);
            }
        }
        else
        {
            if (millis() - lastDataMs > HTTP_TIMEOUT)
            {
                LOG("[SYNC] Data timeout after %d bytes\n", totalWritten);
                break;
            }
            zeroAvailCount++;
            unsigned long tw = millis();
            taskYIELD();
            timeWaiting += millis() - tw;
        }
    }

    if (bufPos > 0) f.write(buf, bufPos);
    f.close();

    if (contentLength > 0 && totalWritten != contentLength)
    {
        LOG("[SYNC] Size mismatch: got %d, expected %d\n", totalWritten, contentLength);
        SD.remove(sdPath);
        client.stop();
        return false;
    }

    unsigned long dlMs = millis() - dlStart;
    uint32_t kbs = dlMs > 0 ? (uint32_t)((uint64_t)totalWritten * 1000 / dlMs / 1024) : 0;
    LOG("[SYNC] avail stats: zero=%lu nonzero=%lu min=%lu max=%lu maxGap=%lu ms\n",
        zeroAvailCount, nonZeroAvailCount, minAvail == UINT32_MAX ? 0 : minAvail, maxAvail, maxGapMs);
    LOG("[SYNC] Timing: read=%lums write=%lums wait=%lums total=%lums\n",
        timeReading, timeWriting, timeWaiting, dlMs);
    LOG("[SYNC] OK: %d bytes in %lu ms (%lu KB/s)\n", totalWritten, dlMs, kbs);
    return totalWritten > 0;
}

static void syncCleanDir(const String &dirPath, std::set<String> &expected)
{
    File dir = SD.open(dirPath);
    if (!dir)
        return;

    File entry = dir.openNextFile();
    while (entry)
    {
        if (!entry.isDirectory())
        {
            String name = entry.name();
            int lastSlash = name.lastIndexOf('/');
            if (lastSlash >= 0)
                name = name.substring(lastSlash + 1);

            if (expected.find(name) == expected.end())
            {
                String fullPath = dirPath + "/" + name;
                LOG("[SYNC] Removing: %s\n", fullPath.c_str());
                SD.remove(fullPath);
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

static bool performSync()
{
    LOGLN("\n[SYNC] Fetching manifest...");

    WiFiClient client;
    if (!client.connect(syncServerIP.c_str(), SERVER_PORT))
    {
        LOG("[SYNC] Connection failed: %s:%d\n", syncServerIP.c_str(), SERVER_PORT);
        return false;
    }
    client.setNoDelay(true);
    client.setTimeout(HTTP_TIMEOUT);

    String payload = httpGet(client, "/api/sync");
    if (payload.isEmpty())
    {
        LOGLN("[SYNC] Failed to fetch manifest");
        client.stop();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        LOG("[SYNC] JSON error: %s\n", err.c_str());
        client.stop();
        return false;
    }

    JsonArray figurines = doc["figurines"].as<JsonArray>();
    JsonArray tracks = doc["tracks"].as<JsonArray>();

    LOG("[SYNC] Manifest: %d figurines, %d tracks\n",
                  figurines.size(), tracks.size());

    std::map<String, uint32_t> localMtime;
    loadSyncMeta(localMtime);

    std::set<String> expectedMusic;
    for (JsonObject t : tracks)
        expectedMusic.insert(t["filename"].as<String>());

    // Policz oczekiwane bajty tylko dla plików do pobrania (do LED progress)
    uint32_t totalExpectedBytes = 0;
    for (JsonObject t : tracks)
    {
        String filename = t["filename"].as<String>();
        uint32_t remoteMtime = t["mtime"].as<uint32_t>();
        String sdPath = "/music/" + filename;
        bool needsDownload = true;
        if (SD.exists(sdPath))
        {
            auto it = localMtime.find(filename);
            if (it != localMtime.end() && it->second == remoteMtime)
                needsDownload = false;
        }
        if (needsDownload)
            totalExpectedBytes += t["size"].as<uint32_t>();
    }

    LOGLN("[SYNC] Checking music files...");
    int downloaded = 0, skipped = 0, failed = 0;
    uint32_t syncBytesDownloaded = 0;
    uint32_t lastLedUpdate = 0;

    for (JsonObject t : tracks)
    {
        String filename = t["filename"].as<String>();
        uint32_t remoteMtime = t["mtime"].as<uint32_t>();
        String sdPath = "/music/" + filename;
        String urlPath = "/api/stream/file/" + urlEncode(filename);

        bool needsDownload = true;
        if (SD.exists(sdPath))
        {
            auto it = localMtime.find(filename);
            if (it != localMtime.end() && it->second == remoteMtime)
                needsDownload = false;
        }
        if (!needsDownload)
        {
            skipped++;
            continue;
        }

        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++)
        {
            if (attempt > 0)
            {
                LOG("[SYNC] Retry %d/3 for %s\n", attempt + 1, filename.c_str());
                delay(1000);
                if (!client.connected())
                    client.connect(syncServerIP.c_str(), SERVER_PORT);
            }
            ok = syncDownloadFile(client, urlPath, sdPath, totalExpectedBytes, syncBytesDownloaded, lastLedUpdate);
        }

        if (ok)
        {
            downloaded++;
            localMtime[filename] = remoteMtime;
            saveSyncMeta(localMtime);
        }
        else
        {
            failed++;
        }
    }

    if (totalExpectedBytes > 0)
        ledSetSyncProgress(totalExpectedBytes, totalExpectedBytes);
    LOG("[SYNC] Music: %d new, %d existing, %d failed\n", downloaded, skipped, failed);

    // Usuń nieaktualne pliki
    LOGLN("[SYNC] Cleaning obsolete files...");
    syncCleanDir("/music", expectedMusic);

    // Usuń z localMtime pliki których nie ma już w expectedMusic
    for (auto it = localMtime.begin(); it != localMtime.end(); )
    {
        if (expectedMusic.find(it->first) == expectedMusic.end())
            it = localMtime.erase(it);
        else
            ++it;
    }
    saveSyncMeta(localMtime);

    // Dźwięki systemowe
    LOGLN("[SYNC] Checking system sounds...");
    JsonArray systemSounds = doc["system_sounds"].as<JsonArray>();
    if (!SD.exists("/data/system"))
        SD.mkdir("/data/system");

    std::set<String> expectedSounds;
    JsonDocument soundsDoc;
    uint32_t dummy1 = 0, dummy2 = 0;

    for (JsonObject s : systemSounds)
    {
        String name = s["name"].as<String>();
        String filename = s["filename"].as<String>();
        String sdPath = "/data/system/" + filename;
        expectedSounds.insert(filename);
        soundsDoc[name] = sdPath;

        if (!SD.exists(sdPath))
        {
            String urlPath = "/api/stream/file/" + urlEncode(filename);
            syncDownloadFile(client, urlPath, sdPath, 0, dummy1, dummy2);
            LOG("[SYNC] Sound '%s': downloaded\n", name.c_str());
        }
        else
        {
            LOG("[SYNC] Sound '%s': exists\n", name.c_str());
        }
    }

    if (SD.exists("/data/system_sounds.json"))
        SD.remove("/data/system_sounds.json");
    File sf = SD.open("/data/system_sounds.json", FILE_WRITE);
    if (sf) { serializeJson(soundsDoc, sf); sf.close(); LOGLN("[SYNC] system_sounds.json saved"); }

    syncCleanDir("/data/system", expectedSounds);

    // Wygeneruj mappings.json
    LOGLN("[SYNC] Generating mappings.json...");

    JsonDocument mappingsDoc;
    JsonObject mFigurines = mappingsDoc["figurines"].to<JsonObject>();
    for (JsonObject f : figurines)
    {
        String uid = f["nfc_uid"].as<String>();
        JsonObject entry = mFigurines[uid].to<JsonObject>();
        entry["file"] = f["track_filename"].as<String>();
    }

    if (SD.exists("/data/mappings.json"))
        SD.remove("/data/mappings.json");

    File mf = SD.open("/data/mappings.json", FILE_WRITE);
    if (!mf)
    {
        LOGLN("[SYNC] Cannot write mappings.json");
        client.stop();
        return false;
    }

    serializeJson(mappingsDoc, mf);
    mf.close();
    LOGLN("[SYNC] mappings.json saved!");

    client.stop();
    return failed == 0;
}

static void clearSyncFlag()
{
    if (SD.exists("/data/sync_pending"))
        SD.remove("/data/sync_pending");
}

void runSyncMode()
{
    LOGLN("\n=== MusicBox SYNC MODE ===\n");
    ledSetSyncWifi();

    // Wyłącz BT kontroler żeby zwolnić radio dla WiFi (koegzystencja BT/WiFi)
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
    LOGLN("[SYNC] BT controller released");

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);

    if (!wm.autoConnect("MusicBox-Setup"))
    {
        LOGLN("[SYNC] WiFi not connected!");
        ledFlashResult(false);
        clearSyncFlag();
        delay(3000);
        ESP.restart();
    }

    LOG("[SYNC] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    esp_err_t psResult = esp_wifi_set_ps(WIFI_PS_NONE);
    LOG("[SYNC] esp_wifi_set_ps(NONE) -> %d\n", psResult);

    wifi_ps_type_t psMode;
    esp_wifi_get_ps(&psMode);
    LOG("[SYNC] Verified PS mode: %d (0=NONE, 1=MIN, 2=MAX)\n", psMode);

    syncServerIP = SERVER_HOST;
    WiFiClient testClient;
    if (!testClient.connect(syncServerIP.c_str(), SERVER_PORT))
    {
        LOGLN("[SYNC] Cannot reach server!");
        testClient.stop();
        ledFlashResult(false);
        clearSyncFlag();
        delay(3000);
        ESP.restart();
    }
    testClient.stop();
    LOGLN("[SYNC] Server reachable!");
    ledSetSyncProgress(0, 1);

    bool success = performSync();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    LOG("%s\n", success ? "\n[SYNC] COMPLETE!" : "\n[SYNC] FAILED");
    ledFlashResult(success);

    clearSyncFlag();
    delay(2000);
    ESP.restart();
}
