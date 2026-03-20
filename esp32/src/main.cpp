/*
 * MusicBox - Muzyczne Pudełko dla Dzieci
 *
 * ESP32 Lolin D32 Pro + PN532 (NFC Software SPI) + PCM5102A (DAC) + SD (SPI) + JBL Go
 * Offline mode: music and mappings stored on SD card
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <Audio.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <map>

// =============================================================================
// PINY
// =============================================================================

// SD Card (Hardware SPI - wbudowany slot na Lolin D32 Pro)
// SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, CS=GPIO4 (na płytce, nie zmieniać)
#define SD_CS      4

// PN532 NFC (Software SPI - piny nie mogą kolidować z SD!)
#define PN532_SCK  22
#define PN532_MISO 21
#define PN532_MOSI 12
#define PN532_SS   5

// PCM5102A DAC (I2S)
#define I2S_BCK   26
#define I2S_LCK   25
#define I2S_DOUT  27

// Przyciski
#define BTN_A     32   // VOL+
#define BTN_B     33   // VOL-

// Sterowanie JBL Go (przez tranzystory NPN BC547)
// Collector -> lewa nóżka przycisku JBL (ta z napięciem ~4V)
// Emitter  -> prawa nóżka przycisku JBL (0V)
// Base     -> rezystor 2.2kΩ -> GPIO ESP32
// GND ESP32 musi być połączony z GND JBL (star ground z zewnętrznego PSU)
#define JBL_POWER    13   // Tranzystor -> przycisk POWER na JBL
#define JBL_VOL_UP   14   // Tranzystor -> przycisk VOL+ na JBL
#define JBL_VOL_DOWN 15   // Tranzystor -> przycisk VOL- na JBL (wymaga pull-down 10kΩ do GND!)
#define JBL_STATUS   34   // ADC input - linia statusowa JBL (~4V gdy włączony, przez dzielnik 10k/22k)

#define JBL_BTN_PRESS_MS     80    // Czas symulacji naciśnięcia przycisku JBL
#define JBL_POWER_PRESS_MS   500   // Czas naciśnięcia power on/off
#define JBL_STATUS_THRESHOLD 2000  // Próg ADC (~2.0V po dzielniku = JBL włączony)
#define JBL_BOOT_WAIT_MS     3000  // Czas czekania na uruchomienie JBL po włączeniu

// Tryb testowy - auto-play z SD (loop)
#define TEST_AUDIO_MODE true
#define TEST_SD_FILE "/music/9383471d_babajaga.mp3"

// =============================================================================
// KONFIGURACJA
// =============================================================================

#define SERVER_HOST        "<musicbox-server>"
#define SERVER_IP_FALLBACK "<musicbox-server-ip>"
#define SERVER_PORT        8000

#define WIFI_AP_NAME      "MusicBox-Setup"
#define WIFI_AP_PASSWORD  ""

#define VOLUME_FIXED   21  // Stała głośność na DAC (max), regulacja fizycznie przez JBL

#define LONG_PRESS_MS      2000
#define DEBOUNCE_MS        50
#define NFC_READ_INTERVAL  300
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD   1

// =============================================================================
// OBIEKTY
// =============================================================================

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);  // Software SPI
Audio audio;
Preferences preferences;
WiFiManager wifiManager;

// =============================================================================
// STAN
// =============================================================================

String currentNfcUid = "";
String lastNfcUid = "";
bool isPlaying = false;
bool nfcReady = false;
bool sdReady = false;
bool syncMode = false;

unsigned long lastNfcRead = 0;
unsigned long btnAPressTime = 0;
unsigned long btnBPressTime = 0;

volatile bool btnAPressed = false;
volatile bool btnBPressed = false;
bool btnAHandled = false;
bool btnBHandled = false;
bool bothHandled = false;
volatile unsigned long lastBtnAInterrupt = 0;
volatile unsigned long lastBtnBInterrupt = 0;

void IRAM_ATTR btnAISR() {
    unsigned long now = millis();
    if (now - lastBtnAInterrupt > DEBOUNCE_MS) {
        btnAPressed = true;
        lastBtnAInterrupt = now;
    }
}

void IRAM_ATTR btnBISR() {
    unsigned long now = millis();
    if (now - lastBtnBInterrupt > DEBOUNCE_MS) {
        btnBPressed = true;
        lastBtnBInterrupt = now;
    }
}

String serverIP = "";
int noTagCount = 0;
int nfcErrorCount = 0;

// Mappings from SD card
std::map<String, String> figurineMap;      // nfc_uid -> filename
std::map<String, String> systemSoundMap;   // sound name -> filename

// =============================================================================
// HELPERS
// =============================================================================

String urlEncode(const String& str) {
    String encoded;
    for (unsigned int i = 0; i < str.length(); i++) {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
            encoded += buf;
        }
    }
    return encoded;
}

String uidToString(uint8_t* uid, uint8_t uidLength) {
    String r;
    for (uint8_t i = 0; i < uidLength; i++) {
        if (i) r += ":";
        if (uid[i] < 0x10) r += "0";
        r += String(uid[i], HEX);
    }
    r.toUpperCase();
    return r;
}

bool resolveMdns() {
    IPAddress ip = MDNS.queryHost(SERVER_HOST);
    if (ip != IPAddress(0, 0, 0, 0)) {
        serverIP = ip.toString();
        return true;
    }
    serverIP = SERVER_IP_FALLBACK;
    return true;
}

void blinkLed(int times, int delayMs) {
    // LED wyłączony - GPIO4 zajęty przez SD na Lolin D32 Pro
    (void)times;
    (void)delayMs;
}

// =============================================================================
// JBL GO CONTROL (przez tranzystory)
// =============================================================================

void jblPressButton(int pin, int durationMs) {
    digitalWrite(pin, HIGH);
    delay(durationMs);
    digitalWrite(pin, LOW);
}

bool isJblOn() {
    int adcValue = analogRead(JBL_STATUS);
    Serial.print("[JBL] Status ADC: ");
    Serial.println(adcValue);
    return adcValue > JBL_STATUS_THRESHOLD;
}

void jblPowerOn() {
    if (isJblOn()) {
        Serial.println("[JBL] Already ON");
        return;
    }
    Serial.println("[JBL] Powering ON...");
    jblPressButton(JBL_POWER, JBL_POWER_PRESS_MS);
    unsigned long start = millis();
    while (millis() - start < JBL_BOOT_WAIT_MS) {
        delay(200);
        if (isJblOn()) {
            Serial.println("[JBL] ON - ready");
            return;
        }
    }
    Serial.println("[JBL] WARNING: Power on timeout");
}

void jblPowerOff() {
    if (!isJblOn()) {
        Serial.println("[JBL] Already OFF");
        return;
    }
    Serial.println("[JBL] Powering OFF...");
    jblPressButton(JBL_POWER, JBL_POWER_PRESS_MS);
    delay(500);
}

void jblVolumeUp() {
    Serial.println("[JBL] VOL+");
    jblPressButton(JBL_VOL_UP, JBL_BTN_PRESS_MS);
}

void jblVolumeDown() {
    Serial.println("[JBL] VOL-");
    jblPressButton(JBL_VOL_DOWN, JBL_BTN_PRESS_MS);
}

// =============================================================================
// SD CARD
// =============================================================================

bool initSD() {
    Serial.println("Initializing SD card (SPI, CS=GPIO4)...");
    SPI.begin(18, 19, 23, SD_CS);  // Jawnie: SCK=18, MISO=19, MOSI=23, SS=4
    delay(100);

    if (!SD.begin(SD_CS)) {
        Serial.println("ERROR: SD mount failed!");
        Serial.print("Card type: ");
        uint8_t ct = SD.cardType();
        if (ct == CARD_NONE)       Serial.println("NONE - no card detected");
        else if (ct == CARD_MMC)   Serial.println("MMC");
        else if (ct == CARD_SD)    Serial.println("SD");
        else if (ct == CARD_SDHC)  Serial.println("SDHC");
        else                       Serial.println("UNKNOWN");
        return false;
    }

    Serial.print("SD Card size: ");
    Serial.print(SD.cardSize() / (1024 * 1024));
    Serial.println(" MB");

    // Create directories if missing
    if (!SD.exists("/music"))  SD.mkdir("/music");
    if (!SD.exists("/system")) SD.mkdir("/system");
    if (!SD.exists("/data"))   SD.mkdir("/data");

    return true;
}

// =============================================================================
// MAPPINGS (JSON on SD)
// =============================================================================

bool loadMappings() {
    figurineMap.clear();
    systemSoundMap.clear();

    if (!SD.exists("/data/mappings.json")) {
        Serial.println("No mappings.json found on SD");
        return false;
    }

    File f = SD.open("/data/mappings.json", FILE_READ);
    if (!f) {
        Serial.println("Failed to open mappings.json");
        return false;
    }

    // Sprawdź rozmiar pliku
    size_t fileSize = f.size();
    Serial.print("mappings.json size: ");
    Serial.print(fileSize);
    Serial.println(" bytes");
    Serial.flush();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.print("mappings.json parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    Serial.println("mappings.json parsed OK");
    Serial.flush();

    // Load figurines
    JsonObject figurines = doc["figurines"].as<JsonObject>();
    if (figurines) {
        for (JsonPair kv : figurines) {
            String uid = kv.key().c_str();
            String file = kv.value()["file"].as<String>();
            figurineMap[uid] = file;
            Serial.print("  Figurine: ");
            Serial.print(uid);
            Serial.print(" -> ");
            Serial.println(file);
            Serial.flush();
        }
    }

    // Load system sounds
    JsonObject sounds = doc["system_sounds"].as<JsonObject>();
    if (sounds) {
        for (JsonPair kv : sounds) {
            String name = kv.key().c_str();
            String file = kv.value().as<String>();
            systemSoundMap[name] = file;
            Serial.print("  System sound: ");
            Serial.print(name);
            Serial.print(" -> ");
            Serial.println(file);
            Serial.flush();
        }
    }

    Serial.print("Loaded ");
    Serial.print(figurineMap.size());
    Serial.print(" figurines, ");
    Serial.print(systemSoundMap.size());
    Serial.println(" system sounds");

    return true;
}

bool saveMappings() {
    JsonDocument doc;

    JsonObject figurines = doc["figurines"].to<JsonObject>();
    for (auto& kv : figurineMap) {
        JsonObject entry = figurines[kv.first].to<JsonObject>();
        entry["file"] = kv.second;
    }

    JsonObject sounds = doc["system_sounds"].to<JsonObject>();
    for (auto& kv : systemSoundMap) {
        sounds[kv.first] = kv.second;
    }

    File f = SD.open("/data/mappings.json", FILE_WRITE);
    if (!f) {
        Serial.println("Failed to write mappings.json");
        return false;
    }

    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("Saved mappings.json");
    return true;
}

// =============================================================================
// SYSTEM SOUNDS (from SD)
// =============================================================================

void playSystemSound(String soundName) {
    if (!sdReady) return;

    auto it = systemSoundMap.find(soundName);
    if (it == systemSoundMap.end()) {
        Serial.print("System sound not mapped: ");
        Serial.println(soundName);
        return;
    }

    String path = "/system/" + it->second;
    if (!SD.exists(path)) {
        Serial.print("System sound file missing: ");
        Serial.println(path);
        return;
    }

    Serial.print("Playing system sound: ");
    Serial.println(path);

    audio.stopSong();
    bool connected = audio.connecttoFS(SD, path.c_str());
    if (!connected) {
        Serial.println("Failed to play system sound!");
        return;
    }

    // Wait for short sound to finish (max 2s)
    unsigned long start = millis();
    while (millis() - start < 2000) {
        audio.loop();
        if (!audio.isRunning()) break;
        delay(10);
    }

    audio.stopSong();
    delay(50);
}

// =============================================================================
// NFC
// =============================================================================

void reinitNfc() {
    nfc.begin();
    delay(500);
    if (nfc.getFirmwareVersion()) {
        nfc.SAMConfig();
        nfcReady = true;
        nfcErrorCount = 0;
    } else {
        nfcReady = false;
    }
}

String readNfcTag() {
    if (!nfcReady) {
        if (++nfcErrorCount > NFC_ERROR_THRESHOLD) {
            reinitNfc();
        }
        return "";
    }

    uint8_t uid[7];
    uint8_t uidLength;

    Serial.print(".");

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
        noTagCount = 0;
        String uidStr = uidToString(uid, uidLength);
        Serial.print("\nNFC Tag: ");
        Serial.println(uidStr);
        return uidStr;
    }
    return "";
}

// =============================================================================
// PLAYBACK (offline from SD)
// =============================================================================

void startPlayback(String uid) {
    if (!sdReady) {
        Serial.println("SD not ready, cannot play");
        return;
    }

    auto it = figurineMap.find(uid);
    if (it == figurineMap.end()) {
        Serial.print("No mapping for NFC UID: ");
        Serial.println(uid);
        return;
    }

    String path = "/music/" + it->second;
    if (!SD.exists(path)) {
        Serial.print("Music file missing: ");
        Serial.println(path);
        return;
    }

    Serial.println("--- Rozpoczynam odtwarzanie ---");
    Serial.print("File: ");
    Serial.println(path);

    audio.stopSong();
    bool connected = audio.connecttoFS(SD, path.c_str());
    if (connected) {
        isPlaying = true;
        lastNfcUid = uid;
        Serial.println("Odtwarzanie rozpoczete!");
    } else {
        Serial.println("Failed to play file from SD");
    }
}

void stopPlayback() {
    Serial.println("--- Zatrzymuje odtwarzanie ---");
    audio.stopSong();
    isPlaying = false;
    lastNfcUid = "";
}

// =============================================================================
// DEEP SLEEP
// =============================================================================

void enterDeepSleep() {
    audio.stopSong();
    delay(100);
    jblPowerOff();
    Serial.println("Entering deep sleep...");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_A, LOW);
    esp_deep_sleep_start();
}

// =============================================================================
// SYNC FROM SERVER
// =============================================================================

bool downloadFile(const String& url, const String& sdPath) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.print("Download failed (HTTP ");
        Serial.print(httpCode);
        Serial.print("): ");
        Serial.println(url);
        http.end();
        return false;
    }

    File f = SD.open(sdPath, FILE_WRITE);
    if (!f) {
        Serial.print("Cannot create file: ");
        Serial.println(sdPath);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    int totalSize = http.getSize();
    int downloaded = 0;
    uint8_t buf[1024];

    while (http.connected() && (totalSize < 0 || downloaded < totalSize)) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = (available > sizeof(buf)) ? sizeof(buf) : available;
            size_t bytesRead = stream->readBytes(buf, toRead);
            f.write(buf, bytesRead);
            downloaded += bytesRead;
        } else {
            delay(1);
        }
    }

    f.close();
    http.end();

    Serial.print("Downloaded ");
    Serial.print(downloaded);
    Serial.print(" bytes -> ");
    Serial.println(sdPath);
    return true;
}

void deleteStaleFiles(const String& dirPath, std::map<String, bool>& validFiles) {
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) return;

    File f = dir.openNextFile();
    while (f) {
        String name = f.name();
        // f.name() may return full path or just filename depending on version
        // Extract just the filename
        int lastSlash = name.lastIndexOf('/');
        if (lastSlash >= 0) {
            name = name.substring(lastSlash + 1);
        }
        f.close();

        if (validFiles.find(name) == validFiles.end()) {
            String fullPath = dirPath + "/" + name;
            Serial.print("Deleting stale file: ");
            Serial.println(fullPath);
            SD.remove(fullPath);
        }

        f = dir.openNextFile();
    }
    dir.close();
}

void syncFromServer() {
    if (!sdReady) {
        Serial.println("SD not ready, cannot sync");
        return;
    }

    // Zapisz flagę sync i zrestartuj - WiFi + Audio nie mieszczą się w RAM jednocześnie
    Serial.println("\n=== RESTART W TRYBIE SYNC ===");
    Serial.flush();
    preferences.putBool("sync_pending", true);
    delay(100);
    ESP.restart();
}

// Właściwa logika sync - wywoływana po restarcie (bez audio w pamięci)
void runSync() {
    Serial.println("[1/5] Laczenie z WiFi...");
    Serial.flush();
    wifiManager.setConfigPortalTimeout(120);
    if (!wifiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD)) {
        Serial.println("[BLAD] Nie udalo sie polaczyc z WiFi!");
        Serial.println("       Sprawdz siec lub polacz z AP 'MusicBox-Setup'");
        Serial.println("       Restart za 3s...");
        delay(3000);
        ESP.restart();
    }

    Serial.print("[OK]   WiFi: ");
    Serial.print(WiFi.SSID());
    Serial.print(" (");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm)");

    // Step 2: Resolve server
    Serial.println("\n[2/5] Szukanie serwera...");
    MDNS.begin("musicbox");
    resolveMdns();
    Serial.print("[OK]   Serwer: ");
    Serial.println(serverIP);

    // Step 3: Fetch manifest
    Serial.println("\n[3/5] Pobieranie manifestu...");
    String syncUrl = "http://" + serverIP + ":" + SERVER_PORT + "/api/sync";
    Serial.print("       URL: ");
    Serial.println(syncUrl);

    HTTPClient http;
    http.begin(syncUrl);
    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode != 200) {
        Serial.print("[BLAD] Manifest - HTTP ");
        Serial.println(httpCode);
        http.end();
        Serial.println("       Restart za 3s...");
        delay(3000);
        ESP.restart();
    }

    String payload = http.getString();
    http.end();
    Serial.print("[OK]   Manifest pobrany (");
    Serial.print(payload.length());
    Serial.println(" bajtow)");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[BLAD] Parsowanie manifestu: ");
        Serial.println(err.c_str());
        Serial.println("       Restart za 3s...");
        delay(3000);
        ESP.restart();
    }

    String baseUrl = "http://" + serverIP + ":" + SERVER_PORT;

    // Track valid files for cleanup
    std::map<String, bool> validMusicFiles;
    std::map<String, bool> validSystemFiles;

    // Step 4: Sync tracks + system sounds
    JsonArray tracks = doc["tracks"].as<JsonArray>();
    int trackCount = tracks ? tracks.size() : 0;
    int trackDownloaded = 0;
    int trackSkipped = 0;
    Serial.print("\n[4/5] Synchronizacja utworow (");
    Serial.print(trackCount);
    Serial.println(")...");

    if (tracks) {
        int idx = 0;
        for (JsonVariant t : tracks) {
            idx++;
            String filename = t["filename"].as<String>();
            String title = t["title"].as<String>();
            validMusicFiles[filename] = true;
            String sdPath = "/music/" + filename;

            if (!SD.exists(sdPath)) {
                Serial.print("       [");
                Serial.print(idx);
                Serial.print("/");
                Serial.print(trackCount);
                Serial.print("] Pobieram: ");
                Serial.println(title);
                String url = baseUrl + "/api/stream/file/" + urlEncode(filename);
                if (downloadFile(url, sdPath)) {
                    trackDownloaded++;
                }
            } else {
                trackSkipped++;
            }
        }
    }
    Serial.print("[OK]   Utwory: ");
    Serial.print(trackDownloaded);
    Serial.print(" pobrane, ");
    Serial.print(trackSkipped);
    Serial.println(" juz na SD");

    JsonArray sysSounds = doc["system_sounds"].as<JsonArray>();
    int soundCount = sysSounds ? sysSounds.size() : 0;
    int soundDownloaded = 0;
    int soundSkipped = 0;

    if (sysSounds) {
        for (JsonVariant s : sysSounds) {
            String name = s["name"].as<String>();
            String filename = s["filename"].as<String>();
            validSystemFiles[filename] = true;
            String sdPath = "/system/" + filename;

            if (!SD.exists(sdPath)) {
                Serial.print("       Pobieram dzwiek: ");
                Serial.println(name);
                String url = baseUrl + "/api/stream/file/" + urlEncode(filename);
                if (downloadFile(url, sdPath)) {
                    soundDownloaded++;
                }
            } else {
                soundSkipped++;
            }
        }
    }
    Serial.print("[OK]   Dzwieki: ");
    Serial.print(soundDownloaded);
    Serial.print(" pobrane, ");
    Serial.print(soundSkipped);
    Serial.println(" juz na SD");

    // Step 5: Cleanup + mappings
    Serial.println("\n[5/5] Zapis mappings + czyszczenie...");
    deleteStaleFiles("/music", validMusicFiles);
    deleteStaleFiles("/system", validSystemFiles);

    figurineMap.clear();
    systemSoundMap.clear();

    JsonArray figurines = doc["figurines"].as<JsonArray>();
    if (figurines) {
        for (JsonVariant f : figurines) {
            String uid = f["nfc_uid"].as<String>();
            String file = f["track_filename"].as<String>();
            figurineMap[uid] = file;
        }
    }

    if (sysSounds) {
        for (JsonVariant s : sysSounds) {
            String name = s["name"].as<String>();
            String file = s["filename"].as<String>();
            systemSoundMap[name] = file;
        }
    }

    saveMappings();
    Serial.print("[OK]   Zapisano ");
    Serial.print(figurineMap.size());
    Serial.print(" figurek, ");
    Serial.print(systemSoundMap.size());
    Serial.println(" dzwiekow");

    Serial.println("\n=== SYNC ZAKONCZONA - restart ===");
    Serial.flush();
    delay(1000);
    ESP.restart();
}

// =============================================================================
// BUTTONS
// =============================================================================

void handleButtons() {
    unsigned long now = millis();

    bool a = digitalRead(BTN_A) == LOW;
    bool b = digitalRead(BTN_B) == LOW;

    // ISR ustawiło flagę -> zapamiętaj czas + natychmiastowa akcja VOL
    if (btnAPressed && btnAPressTime == 0) {
        btnAPressTime = now;
        btnAHandled = false;
        Serial.println("[BTN] A (VOL+) pressed");
        jblVolumeUp();
        btnAHandled = true;
    }
    if (btnBPressed && btnBPressTime == 0) {
        btnBPressTime = now;
        btnBHandled = false;
        Serial.println("[BTN] B (VOL-) pressed");
        jblVolumeDown();
        btnBHandled = true;
    }

    // Both buttons held for 2s -> sync
    if (a && b && !bothHandled) {
        unsigned long earliest = (btnAPressTime < btnBPressTime) ? btnBPressTime : btnAPressTime;
        if (earliest > 0 && now - earliest >= LONG_PRESS_MS) {
            bothHandled = true;
            Serial.println("\n>>> Oba przyciski przytrzymane 2s -> SYNC");
            syncFromServer();
        }
    }

    // Long press BTN_B only (2s) -> deep sleep
    if (b && !a && btnBPressTime > 0 && now - btnBPressTime >= LONG_PRESS_MS) {
        Serial.println("\n>>> BTN_B przytrzymany 2s -> DEEP SLEEP");
        enterDeepSleep();
    }

    // Release BTN_A
    if (!a && btnAPressed) {
        btnAPressed = false;
        btnAPressTime = 0;
        btnAHandled = false;
        if (!b) bothHandled = false;
    }

    // Release BTN_B
    if (!b && btnBPressed) {
        btnBPressed = false;
        btnBPressTime = 0;
        btnBHandled = false;
        if (!a) bothHandled = false;
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== MusicBox (Offline Mode) ===");

    // Przyciski ESP32
    pinMode(BTN_A, INPUT_PULLUP);
    pinMode(BTN_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_A), btnAISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_B), btnBISR, FALLING);

    // JBL Go - piny sterujące (LOW = tranzystor wyłączony = przycisk nie wciśnięty)
    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_VOL_UP, OUTPUT);
    digitalWrite(JBL_VOL_UP, LOW);
    pinMode(JBL_VOL_DOWN, OUTPUT);
    digitalWrite(JBL_VOL_DOWN, LOW);
    pinMode(JBL_STATUS, INPUT);

    preferences.begin("musicbox", false);

    // SD Card (zawsze potrzebna)
    sdReady = initSD();
    if (!sdReady) {
        Serial.println("WARNING: No SD card - limited functionality");
    }

    // Sprawdź czy to restart w trybie sync
    if (preferences.getBool("sync_pending", false)) {
        preferences.putBool("sync_pending", false);
        Serial.println("=== TRYB SYNCHRONIZACJI ===");
        syncMode = true;
        runSync();  // Nie wraca - restartuje po sync
        return;
    }

    // Normalny tryb - bez WiFi
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi OFF (offline mode)");

    // NFC SPI
    Serial.println("Initializing NFC (Software SPI)...");
    nfc.begin();
    delay(1000);

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
        Serial.print("PN532 found! Firmware: 0x");
        Serial.println(versiondata, HEX);
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);
        nfcReady = true;
    } else {
        Serial.println("ERROR: PN532 not found!");
    }

    // Audio I2S (PCM5102A)
    Serial.println("Initializing Audio I2S (PCM5102A)...");
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" PSRAM: ");
    Serial.println(ESP.getFreePsram());
    audio.setPinout(I2S_BCK, I2S_LCK, I2S_DOUT);
    audio.setVolume(VOLUME_FIXED);
    Serial.print("Volume (fixed): ");
    Serial.println(VOLUME_FIXED);

    // Load mappings from SD
    if (sdReady) {
        loadMappings();
    }

    // Check wake reason + power on JBL
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    if (wakeReason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Woke from deep sleep (BTN_A)");
        jblPowerOn();
    } else {
        // Cold boot - upewnij się że JBL jest włączony
        jblPowerOn();
    }

    blinkLed(2, 200);

    #if TEST_AUDIO_MODE
    Serial.println("\n=== TEST MODE ===");
    if (sdReady && SD.exists(TEST_SD_FILE)) {
        Serial.print("Playing test file: ");
        Serial.println(TEST_SD_FILE);
        audio.connecttoFS(SD, TEST_SD_FILE);
        isPlaying = true;
    } else {
        Serial.println("Test file not found on SD, skipping");
    }
    #else
    Serial.println("\nReady! Place NFC tag...");
    #endif
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    audio.loop();
    handleButtons();

    #if !TEST_AUDIO_MODE
    if (millis() - lastNfcRead > NFC_READ_INTERVAL) {
        lastNfcRead = millis();

        currentNfcUid = readNfcTag();
        if (!currentNfcUid.isEmpty()) {
            noTagCount = 0;
            if (currentNfcUid != lastNfcUid) {
                startPlayback(currentNfcUid);
            }
        } else if (isPlaying && ++noTagCount >= NO_TAG_THRESHOLD) {
            stopPlayback();
            noTagCount = 0;
        }
    }
    #endif
}

// =============================================================================
// AUDIO CALLBACKS
// =============================================================================

void audio_info(const char* info) {
    Serial.println(info);
}

void audio_eof_mp3(const char* info) {
    #if TEST_AUDIO_MODE
    Serial.println("Track ended, restarting (loop mode)...");
    if (sdReady) {
        audio.connecttoFS(SD, TEST_SD_FILE);
    }
    #else
    isPlaying = false;
    #endif
}
