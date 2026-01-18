/*
 * MusicBox - Muzyczne Pudełko dla Dzieci
 *
 * ESP32 + PN532 (NFC I2C) + PCM5102A (DAC) + JBL Go
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

// =============================================================================
// PINY
// =============================================================================

// PN532 NFC (SPI)
#define PN532_SCK  18
#define PN532_MISO 19
#define PN532_MOSI 23
#define PN532_SS   5

// PCM5102A DAC (I2S) - docelowo
#define I2S_BCK   26
#define I2S_LCK   25
#define I2S_DOUT  27

// Tryb testowy - wbudowany DAC (słuchawki na GPIO25/26)
// true = internal DAC, false = external I2S DAC (PCM5102A)
#define USE_INTERNAL_DAC true

// Przyciski
#define BTN_VOL_UP    32
#define BTN_VOL_DOWN  33

// LED
#define LED_PIN 2

// =============================================================================
// KONFIGURACJA
// =============================================================================

#define SERVER_HOST        "<musicbox-server>"
#define SERVER_IP_FALLBACK "<musicbox-server-ip>"
#define SERVER_PORT        8000

#define WIFI_AP_NAME      "MusicBox-Setup"
#define WIFI_AP_PASSWORD  ""

#define VOLUME_MIN     0
#define VOLUME_MAX     21
#define VOLUME_DEFAULT 10

#define LONG_PRESS_MS      2000
#define DEBOUNCE_MS        50
#define NFC_READ_INTERVAL  2000  // 2 sekundy między odczytami
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD   2

// =============================================================================
// OBIEKTY
// =============================================================================

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);  // Software SPI
Audio audio(USE_INTERNAL_DAC);  // true = internal DAC (GPIO25/26), false = I2S
Preferences preferences;
WiFiManager wifiManager;

// =============================================================================
// STAN
// =============================================================================

int currentVolume = VOLUME_DEFAULT;
String currentNfcUid = "";
String lastNfcUid = "";
bool isPlaying = false;
bool nfcReady = false;

unsigned long lastNfcRead = 0;
unsigned long btnVolUpPressTime = 0;
unsigned long btnVolDownPressTime = 0;

bool btnVolUpPressed = false;
bool btnVolDownPressed = false;
bool btnVolUpHandled = false;
bool btnVolDownHandled = false;

String serverIP = "";
int noTagCount = 0;
int nfcErrorCount = 0;

// =============================================================================
// HELPERS
// =============================================================================

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
    if (ip != IPAddress(0,0,0,0)) {
        serverIP = ip.toString();
        return true;
    }
    serverIP = SERVER_IP_FALLBACK;
    return true;
}

void saveVolume() {
    preferences.putInt("volume", currentVolume);
}

void loadVolume() {
    currentVolume = preferences.getInt("volume", VOLUME_DEFAULT);
}

void setVolume(int vol) {
    currentVolume = constrain(vol, VOLUME_MIN, VOLUME_MAX);
    audio.setVolume(currentVolume);
}

void blinkLed(int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

void enterDeepSleep() {
    audio.stopSong();
    saveVolume();
    blinkLed(3, 100);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_VOL_UP, LOW);
    esp_deep_sleep_start();
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

    Serial.print(".");  // Kropka przy każdym skanowaniu

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
// SERVER / AUDIO
// =============================================================================

String getStreamUrl(String uid) {
    if (serverIP.isEmpty()) resolveMdns();

    HTTPClient http;
    String url = "http://" + serverIP + ":" + SERVER_PORT + "/api/play/" + uid;
    Serial.print("Fetching: ");
    Serial.println(url);

    http.begin(url);
    http.setTimeout(5000);

    String result = "";
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            const char* trackTitle = doc["track_title"];
            const char* figurineName = doc["figurine_name"];
            result = String((const char*)doc["stream_url"]);
            result.replace(SERVER_HOST, serverIP);

            Serial.print("Figurka: ");
            Serial.println(figurineName ? figurineName : "nieznana");
            Serial.print("Utwor: ");
            Serial.println(trackTitle ? trackTitle : "brak");
            Serial.print("Stream: ");
            Serial.println(result);
        }
    } else if (httpCode == 404) {
        Serial.println("Brak figurki lub utworu dla tego NFC!");
    } else {
        Serial.print("HTTP error: ");
        Serial.println(httpCode);
        serverIP = "";
    }

    http.end();
    return result;
}

void startPlayback(String uid) {
    Serial.println("--- Rozpoczynam odtwarzanie ---");
    String url = getStreamUrl(uid);
    if (!url.isEmpty()) {
        audio.stopSong();
        audio.connecttohost(url.c_str());
        isPlaying = true;
        lastNfcUid = uid;
        Serial.println("Odtwarzanie rozpoczete!");
    } else {
        Serial.println("Nie mozna odtworzyc - brak URL");
    }
}

void stopPlayback() {
    Serial.println("--- Zatrzymuje odtwarzanie ---");
    audio.stopSong();
    isPlaying = false;
    lastNfcUid = "";
}

// =============================================================================
// BUTTONS
// =============================================================================

void handleButtons() {
    unsigned long now = millis();

    bool up = digitalRead(BTN_VOL_UP) == LOW;
    bool down = digitalRead(BTN_VOL_DOWN) == LOW;

    if (up && !btnVolUpPressed) {
        btnVolUpPressed = true;
        btnVolUpPressTime = now;
    } else if (!up && btnVolUpPressed) {
        setVolume(currentVolume + 1);
        btnVolUpPressed = false;
    }

    if (down && !btnVolDownPressed) {
        btnVolDownPressed = true;
        btnVolDownPressTime = now;
    } else if (down && now - btnVolDownPressTime >= LONG_PRESS_MS) {
        enterDeepSleep();
    } else if (!down && btnVolDownPressed) {
        setVolume(currentVolume - 1);
        btnVolDownPressed = false;
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    pinMode(BTN_VOL_UP, INPUT_PULLUP);
    pinMode(BTN_VOL_DOWN, INPUT_PULLUP);

    preferences.begin("musicbox", false);
    loadVolume();

    wifiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    MDNS.begin("musicbox");
    resolveMdns();
    Serial.print("Server IP: ");
    Serial.println(serverIP);

    // NFC SPI
    Serial.println("Initializing NFC (SPI)...");
    Serial.print("  SCK=GPIO");  Serial.println(PN532_SCK);
    Serial.print("  MISO=GPIO"); Serial.println(PN532_MISO);
    Serial.print("  MOSI=GPIO"); Serial.println(PN532_MOSI);
    Serial.print("  SS=GPIO");   Serial.println(PN532_SS);

    Serial.println("Calling nfc.begin()...");
    nfc.begin();
    delay(1000);

    Serial.println("Getting firmware version...");
    uint32_t versiondata = nfc.getFirmwareVersion();
    Serial.print("Firmware: 0x");
    Serial.println(versiondata, HEX);

    if (versiondata) {
        Serial.println("PN532 found!");
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);  // 16 prób
        nfcReady = true;
        Serial.println("NFC ready!");
    } else {
        Serial.println("ERROR: PN532 not found!");
        blinkLed(10, 100);
    }

    // Audio
    #if USE_INTERNAL_DAC
        Serial.println("Audio: Internal DAC (GPIO25/GPIO26)");
    #else
        audio.setPinout(I2S_BCK, I2S_LCK, I2S_DOUT);
        Serial.println("Audio: External I2S DAC (BCK=26, LCK=25, DOUT=27)");
    #endif
    audio.setVolume(currentVolume);

    blinkLed(2, 200);
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    audio.loop();
    handleButtons();

    if (millis() - lastNfcRead > NFC_READ_INTERVAL) {
        lastNfcRead = millis();

        currentNfcUid = readNfcTag();
        if (!currentNfcUid.isEmpty()) {
            if (currentNfcUid != lastNfcUid) {
                startPlayback(currentNfcUid);
            }
        } else if (isPlaying && ++noTagCount >= NO_TAG_THRESHOLD) {
            stopPlayback();
        }
    }
}

// =============================================================================
// AUDIO CALLBACKS
// =============================================================================

void audio_info(const char* info) {
    Serial.println(info);
}

void audio_eof_mp3(const char* info) {
    isPlaying = false;
}
