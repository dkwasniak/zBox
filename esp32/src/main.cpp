/*
 * MusicBox - Muzyczne Pudełko dla Dzieci
 *
 * ESP32 Lolin D32 Pro + PN532 (NFC Software SPI) + Bluetooth A2DP + SD Card
 * Offline mode: muzyka i mappingi na karcie SD
 * Audio: SD -> MP3 decoder -> Bluetooth A2DP Source -> JBL Go 2
 *
 * Sync: oba przyciski 2s -> flaga sync_pending -> restart -> WiFi sync -> restart
 * Pierwsze WiFi: automatyczny portal AP "MusicBox-Setup" do konfiguracji
 *
 * OPTYMALIZACJA STARTU:
 * - Usunięte zbędne delay() z inicjalizacji SD, NFC, ADC
 * - Nieblokujące włączanie JBL (puls w tle, bez czekania na boot)
 * - BT A2DP startuje jak najwcześniej (async, łączy się w tle)
 * - NFC pre-scan przy boot - jeśli figurka już stoi, plik gotowy do odtwarzania
 * - Odtwarzanie startuje natychmiast po połączeniu BT (deferred playback)
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <SD.h>
#include <FastLED.h>
#include <map>
#include <set>

#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

// WiFi (tylko do sync - nie inicjalizowane jednocześnie z BT)
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>

// =============================================================================
// PINY
// =============================================================================

#define SD_CS 4 // SD Card CS (wbudowany slot Lolin D32 Pro)

// PN532 NFC (Software SPI)
#define PN532_SCK 22
#define PN532_MISO 21
#define PN532_MOSI 12
#define PN532_SS 5

#define LED_PIN 14 // WS2812B DIN
#define LED_COUNT 5
#define LED_BRIGHTNESS 40

#define BTN_A 32 // VOL+   (RTC, wake-up z deep sleep przez ext0)
#define BTN_B 33 // VOL-   (RTC)
#define BTN_C 25 // rezerwa (RTC, wolny po rezygnacji z I2S)
#define BTN_D 26 // rezerwa (RTC, wolny po rezygnacji z I2S)
#define BTN_COUNT 4

#define JBL_POWER 13  // Tranzystor NPN -> przycisk POWER na JBL
#define JBL_STATUS 34 // ADC - linia statusowa JBL (dzielnik 10k/22k)

// =============================================================================
// KONFIGURACJA
// =============================================================================

#define BT_SPEAKER_NAME "JBL GO 2"

#define TEST_AUDIO_MODE false
#define TEST_SD_FILE "/music/9383471d_babajaga.mp3"

#define LONG_PRESS_MS 2000
#define DEBOUNCE_MS 50
#define NFC_READ_INTERVAL 1000 // było 300 - szybsza detekcja tagu
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD 2
#define AUDIO_BUF_SIZE 512

// JBL
#define JBL_POWER_PRESS_MS 500
#define JBL_STATUS_THRESHOLD 180
#define JBL_BOOT_WAIT_MS 5000 // timeout na cold boot JBL + A2DP reconnect

// Głośność Bluetooth (AVRCP)
#define BT_VOL_STEP 5
#define BT_VOL_MIN 0
#define BT_VOL_MAX 100
#define BT_VOL_DEFAULT 50

// Sync
#define SERVER_HOST "<musicbox-server-ip>"
#define SERVER_PORT 8000
#define HTTP_TIMEOUT 15000
#define DOWNLOAD_BUF_SIZE 4096

// =============================================================================
// OBIEKTY
// =============================================================================

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Preferences preferences;

A2DPStream a2dp;
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoderStream(&a2dp, &mp3Decoder);
File currentAudioFile;
uint8_t audioBuf[AUDIO_BUF_SIZE];

// =============================================================================
// LED
// =============================================================================

CRGB leds[LED_COUNT];
TaskHandle_t ledTaskHandle = NULL;
bool fastLedInitialized = false;

enum LedMode
{
    LED_OFF,
    LED_BOOT,
    LED_WAIT_BT,
    LED_IDLE,
    LED_PLAYING,
    LED_VOLUME,
    LED_SYNC_WIFI,
    LED_SYNC_PROGRESS
};

LedMode ledMode = LED_OFF;
unsigned long ledLastUpdate = 0;
int ledAnimStep = 0;
int ledBootStep = -1;
unsigned long ledVolumeShowTime = 0;
int ledSyncLit = 0; // ile diod zapalonych w pasku postępu

// =============================================================================
// STAN
// =============================================================================

String currentNfcUid;
String lastNfcUid;
bool isPlaying = false;
bool nfcReady = false;
bool sdReady = false;
int btVolume = BT_VOL_DEFAULT;
bool btVolumeApplied = false;

unsigned long lastNfcRead = 0;

// --- Buttons (generic, 4x) ---
struct Button
{
    uint8_t pin;
    const char *name;
    volatile bool pressed;                // ISR flag, czyszczone w handleButtons po release
    volatile unsigned long lastInterrupt; // debounce timestamp (ISR)
    unsigned long pressStart;             // millis() pierwszego naciśnięcia, 0 gdy zwolniony
    bool longHandled;                     // akcja long-press już wystrzeliła
};

Button buttons[BTN_COUNT] = {
    {BTN_A, "A(VOL+)", false, 0, 0, false},
    {BTN_B, "B(VOL-)", false, 0, 0, false},
    {BTN_C, "C", false, 0, 0, false},
    {BTN_D, "D", false, 0, 0, false},
};

bool bothABHandled = false; // flaga dla kombinacji A+B (sync)

int noTagCount = 0;
int nfcErrorCount = 0;

std::map<String, String> figurineMap; // nfc_uid -> filename

// Deferred playback - plik gotowy do odtwarzania po połączeniu BT
String pendingPlaybackPath;
String pendingPlaybackUid;

// Timing - pomiar czasu startu
unsigned long bootStart = 0;
bool bootTimingDone = false;

// =============================================================================
// ISR
// =============================================================================

// Jedna wspólna ISR dla wszystkich przycisków, parametryzowana przez wskaźnik
// na strukturę Button (attachInterruptArg).
void IRAM_ATTR btnISR(void *arg)
{
    Button *b = (Button *)arg;
    unsigned long now = millis();
    if (now - b->lastInterrupt > DEBOUNCE_MS)
    {
        b->pressed = true;
        b->lastInterrupt = now;
    }
}

// =============================================================================
// HELPERS
// =============================================================================

String urlEncode(const String &str)
{
    const char *hex = "0123456789ABCDEF";
    String encoded;
    for (unsigned int i = 0; i < str.length(); i++)
    {
        char c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
        {
            encoded += c;
        }
        else
        {
            uint8_t b = (uint8_t)c;
            encoded += '%';
            encoded += hex[b >> 4];
            encoded += hex[b & 0x0F];
        }
    }
    return encoded;
}

String uidToString(uint8_t *uid, uint8_t uidLength)
{
    String r;
    for (uint8_t i = 0; i < uidLength; i++)
    {
        if (i)
            r += ":";
        if (uid[i] < 0x10)
            r += "0";
        r += String(uid[i], HEX);
    }
    r.toUpperCase();
    return r;
}

// =============================================================================
// LED FUNCTIONS
// =============================================================================

void ledTaskFunc(void *param); // forward declaration

void initLeds()
{
    if (!fastLedInitialized)
    {
        FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
        FastLED.setBrightness(LED_BRIGHTNESS);
        fastLedInitialized = true;
    }
    FastLED.clear();
    FastLED.show();
    ledMode = LED_OFF;

    // Osobny task FreeRTOS - animacje LED niezależne od loop()
    xTaskCreatePinnedToCore(ledTaskFunc, "led", 2048, NULL, 1, &ledTaskHandle, 0);
}

void ledSetBootProgress(int step)
{
    ledMode = LED_BOOT;
    ledBootStep = step;
    // Zakończone kroki świecą na stałe
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < step) ? CRGB(0, 0, 80) : CRGB::Black;
    }
    // Aktualny krok - 3 szybkie mignięcia
    for (int flash = 0; flash < 3; flash++)
    {
        leds[step] = CRGB(0, 0, 80);
        FastLED.show();
        delay(80);
        leds[step] = CRGB::Black;
        FastLED.show();
        delay(80);
    }
    // Zostaw zapalony po mignięciach
    leds[step] = CRGB(0, 0, 80);
    FastLED.show();
}

void ledSetWaitBt()
{
    ledMode = LED_WAIT_BT;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledSetIdle()
{
    ledMode = LED_IDLE;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledSetPlaying()
{
    ledMode = LED_PLAYING;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledShowVolume(int volumePercent)
{
    ledMode = LED_VOLUME;
    int lit = map(volumePercent, BT_VOL_MIN, BT_VOL_MAX, 0, LED_COUNT);
    if (volumePercent > BT_VOL_MIN && lit == 0)
        lit = 1;
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < lit) ? CRGB(80, 80, 80) : CRGB::Black;
    }
    FastLED.show();
    ledVolumeShowTime = millis();
}

void ledSetSyncWifi()
{
    ledMode = LED_SYNC_WIFI;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledSetSyncProgress(int current, int total)
{
    ledMode = LED_SYNC_PROGRESS;
    if (total <= 0)
    {
        ledSyncLit = LED_COUNT;
    }
    else
    {
        ledSyncLit = ((current + 1) * LED_COUNT) / total;
        if (ledSyncLit < 1)
            ledSyncLit = 1;
        if (ledSyncLit > LED_COUNT)
            ledSyncLit = LED_COUNT;
    }
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < ledSyncLit) ? CRGB(0, 0, 120) : CRGB(0, 0, 15);
    }
    FastLED.show();
}

void ledFlashResult(bool success)
{
    CRGB color = success ? CRGB(0, 120, 0) : CRGB(120, 0, 0);
    for (int flash = 0; flash < 3; flash++)
    {
        fill_solid(leds, LED_COUNT, color);
        FastLED.show();
        delay(200);
        FastLED.clear();
        FastLED.show();
        delay(150);
    }
}

void ledFlashWarning()
{
    for (int flash = 0; flash < 2; flash++)
    {
        fill_solid(leds, LED_COUNT, CRGB(120, 60, 0));
        FastLED.show();
        delay(200);
        FastLED.clear();
        FastLED.show();
        delay(150);
    }
}

void ledShutdownAnim()
{
    for (int i = LED_COUNT - 1; i >= 0; i--)
    {
        leds[i] = CRGB(60, 0, 80);
        FastLED.show();
        delay(100);
    }
    for (int i = LED_COUNT - 1; i >= 0; i--)
    {
        leds[i] = CRGB::Black;
        FastLED.show();
        delay(100);
    }
}

void ledTaskFunc(void *param)
{
    for (;;)
    {
        unsigned long now = millis();

        // Volume overlay - powrót do poprzedniego trybu po 1s
        if (ledMode == LED_VOLUME && now - ledVolumeShowTime >= 1000)
        {
            if (isPlaying)
                ledSetPlaying();
            else
                ledSetIdle();
        }

        switch (ledMode)
        {
        case LED_WAIT_BT:
        {
            ledAnimStep = (ledAnimStep + 1) % 256;
            uint8_t val = cubicwave8(ledAnimStep);
            uint8_t b = map(val, 0, 255, 5, 80);
            fill_solid(leds, LED_COUNT, CRGB(0, 0, b));
            FastLED.show();
            break;
        }
        case LED_IDLE:
        {
            ledAnimStep = (ledAnimStep + 1) % 256;
            uint8_t val = cubicwave8(ledAnimStep);
            uint8_t g = map(val, 0, 255, 5, 80);
            fill_solid(leds, LED_COUNT, CRGB(0, g, 0));
            FastLED.show();
            break;
        }
        case LED_PLAYING:
        {
            ledAnimStep = (ledAnimStep + 1) % LED_COUNT;
            for (int i = 0; i < LED_COUNT; i++)
            {
                int dist = (i - ledAnimStep + LED_COUNT) % LED_COUNT;
                switch (dist)
                {
                case 0:
                    leds[i] = CRGB(0, 100, 60);
                    break;
                case 1:
                    leds[i] = CRGB(0, 60, 30);
                    break;
                case 2:
                    leds[i] = CRGB(0, 25, 15);
                    break;
                default:
                    leds[i] = CRGB(0, 8, 5);
                    break;
                }
            }
            FastLED.show();
            break;
        }
        case LED_SYNC_WIFI:
        {
            ledAnimStep = !ledAnimStep;
            CRGB color = ledAnimStep ? CRGB(100, 80, 0) : CRGB::Black;
            fill_solid(leds, LED_COUNT, color);
            FastLED.show();
            break;
        }
        default:
            break;
        }

        // Delay zależny od trybu
        int delayMs = 15;
        if (ledMode == LED_PLAYING)
            delayMs = 80;
        else if (ledMode == LED_SYNC_WIFI)
            delayMs = 400;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}

// =============================================================================
// JBL GO CONTROL
// =============================================================================

void jblPressButtonBlocking(int pin, int durationMs)
{
    digitalWrite(pin, HIGH);
    delay(durationMs);
    digitalWrite(pin, LOW);
}

bool isJblOn()
{
    int maxVal = 0;
    for (int i = 0; i < 5; i++)
    {
        int v = analogRead(JBL_STATUS);
        if (v > maxVal)
            maxVal = v;
        delay(10);
    }
    Serial.printf("[JBL] Status ADC: %d\n", maxVal);
    return maxVal > JBL_STATUS_THRESHOLD;
}

// UWAGA: włączanie JBL przy boot jest robione inline w setup() (nieblokująco,
// puls interleaved z NFC init). Runtime recovery (auto-power-off JBL po
// bezczynności) obsługuje ensureJblReady() w sekcji PLAYBACK.

// Blokujące wyłączanie - używane tylko przed deep sleep
void jblPowerOff()
{
    if (!isJblOn())
    {
        Serial.println("[JBL] Already OFF");
        return;
    }
    Serial.println("[JBL] Powering OFF...");
    jblPressButtonBlocking(JBL_POWER, JBL_POWER_PRESS_MS);
    delay(500);
}

// =============================================================================
// VOLUME (AVRCP over Bluetooth)
// =============================================================================

void saveBtVolume()
{
    preferences.begin("musicbox", false);
    preferences.putInt("bt_volume", btVolume);
    preferences.end();
}

void loadBtVolume()
{
    preferences.begin("musicbox", true);
    btVolume = preferences.getInt("bt_volume", BT_VOL_DEFAULT);
    preferences.end();
    Serial.printf("[VOL] Restored: %d%%\n", btVolume);
}

void applyBtVolume()
{
    a2dp.setVolume(btVolume / 100.0);
    Serial.printf("[VOL] Applied: %d%%\n", btVolume);
}

void volumeUp()
{
    btVolume = min(btVolume + BT_VOL_STEP, BT_VOL_MAX);
    applyBtVolume();
    saveBtVolume();
    ledShowVolume(btVolume);
}

void volumeDown()
{
    btVolume = max(btVolume - BT_VOL_STEP, BT_VOL_MIN);
    applyBtVolume();
    saveBtVolume();
    ledShowVolume(btVolume);
}

// =============================================================================
// SD CARD
// =============================================================================

bool initSD()
{
    Serial.println("Initializing SD card...");
    SPI.begin(18, 19, 23, SD_CS);
    // Usunięto delay(100) - SPI.begin() i SD.begin() obsługują timing wewnętrznie

    if (!SD.begin(SD_CS))
    {
        Serial.println("ERROR: SD mount failed!");
        return false;
    }

    Serial.printf("SD Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    if (!SD.exists("/music"))
        SD.mkdir("/music");
    if (!SD.exists("/data"))
        SD.mkdir("/data");

    return true;
}

// =============================================================================
// MAPPINGS (JSON on SD)
// =============================================================================

bool loadMappings()
{
    figurineMap.clear();

    if (!SD.exists("/data/mappings.json"))
    {
        Serial.println("No mappings.json on SD");
        return false;
    }

    File f = SD.open("/data/mappings.json", FILE_READ);
    if (!f)
    {
        Serial.println("Failed to open mappings.json");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        Serial.printf("mappings.json parse error: %s\n", err.c_str());
        return false;
    }

    JsonObject figurines = doc["figurines"].as<JsonObject>();
    if (figurines)
    {
        for (JsonPair kv : figurines)
        {
            String uid = kv.key().c_str();
            String file = kv.value()["file"].as<String>();
            figurineMap[uid] = file;
            Serial.printf("  %s -> %s\n", uid.c_str(), file.c_str());
        }
    }

    Serial.printf("Loaded %d figurines\n", figurineMap.size());
    return true;
}

// =============================================================================
// AUDIO (SD -> MP3 -> Bluetooth A2DP)
// =============================================================================

void audioLoop()
{
    if (!currentAudioFile)
        return;

    if (currentAudioFile.available())
    {
        int bytesRead = currentAudioFile.read(audioBuf, AUDIO_BUF_SIZE);
        if (bytesRead > 0)
        {
            decoderStream.write(audioBuf, bytesRead);
        }
    }
    else
    {
        currentAudioFile.close();
#if TEST_AUDIO_MODE
        Serial.println("Track ended, restarting (loop mode)...");
        if (sdReady)
        {
            currentAudioFile = SD.open(TEST_SD_FILE);
        }
#else
        Serial.println("Track ended");
        isPlaying = false;
#endif
    }
}

// =============================================================================
// NFC
// =============================================================================

void reinitNfc()
{
    nfc.begin();
    delay(100); // było 500 - PN532 wymaga max ~2ms na wakeup, 100ms to bezpieczny margines
    if (nfc.getFirmwareVersion())
    {
        nfc.SAMConfig();
        nfcReady = true;
        nfcErrorCount = 0;
    }
    else
    {
        nfcReady = false;
    }
}

String readNfcTag()
{
    if (!nfcReady)
    {
        if (++nfcErrorCount > NFC_ERROR_THRESHOLD)
        {
            reinitNfc();
        }
        return "";
    }

    uint8_t uid[7];
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50))
    {
        noTagCount = 0;
        String uidStr = uidToString(uid, uidLength);
        Serial.printf("\nNFC Tag: %s\n", uidStr.c_str());
        return uidStr;
    }
    return "";
}

// =============================================================================
// PLAYBACK
// =============================================================================

// Zapewnia że JBL jest ON (ADC) i A2DP jest podłączone.
// Wywoływane przed każdym odtworzeniem - obsługuje scenariusz:
// 1. JBL wyłączył się sam po bezczynności (auto-power-off po ~15 min)
// 2. BT się rozłączyło i auto_reconnect jeszcze się nie wpiął
// Zwraca true gdy wszystko gotowe, false gdy przekroczono timeout.
bool ensureJblReady()
{
    bool adcOn = isJblOn();
    bool btConn = a2dp.source().is_connected();

    // Szybka ścieżka - wszystko działa, wracamy od razu
    if (adcOn && btConn)
        return true;

    // JBL wyłączony wg ADC - wciśnij power (blokujące, 500ms)
    if (!adcOn)
    {
        Serial.println("[JBL] ADC says OFF - pressing power");
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);
        digitalWrite(JBL_POWER, LOW);
        // JBL potrzebuje ~1-2s na boot zanim zacznie akceptować BT
    }

    // Poczekaj na A2DP (auto_reconnect zadziała w tle)
    Serial.println("[JBL] Waiting for A2DP reconnect...");
    ledSetWaitBt();
    unsigned long start = millis();
    while (!a2dp.source().is_connected())
    {
        if (millis() - start > JBL_BOOT_WAIT_MS)
        {
            Serial.printf("[JBL] A2DP reconnect timeout after %lu ms\n", millis() - start);
            return false;
        }
        delay(50);
    }
    Serial.printf("[JBL] Ready after %lu ms\n", millis() - start);

    // Po reconnect trzeba ponownie zaaplikować głośność
    applyBtVolume();
    btVolumeApplied = true;
    return true;
}

void startPlayback(const String &uid)
{
    if (!sdReady)
    {
        Serial.println("SD not ready");
        return;
    }

    auto it = figurineMap.find(uid);
    if (it == figurineMap.end())
    {
        Serial.printf("No mapping for UID: %s\n", uid.c_str());
        ledFlashWarning();
        return;
    }

    String path = "/music/" + it->second;
    if (!SD.exists(path))
    {
        Serial.printf("File missing: %s\n", path.c_str());
        ledFlashWarning();
        return;
    }

    // Auto-recovery: JBL mógł się sam wyłączyć po bezczynności.
    // ensureJblReady() sprawdza ADC, w razie potrzeby wciska power i czeka na A2DP.
    if (!ensureJblReady())
    {
        Serial.println("[PLAY] JBL not ready - aborting playback");
        ledFlashWarning();
        return;
    }

    if (currentAudioFile)
        currentAudioFile.close();
    currentAudioFile = SD.open(path);
    if (currentAudioFile)
    {
        isPlaying = true;
        lastNfcUid = uid;
        ledSetPlaying();
        Serial.printf("Playing: %s\n", path.c_str());
        if (!bootTimingDone)
        {
            Serial.printf("[T+%4lu] >>> PLAYBACK START (NFC trigger)\n", millis() - bootStart);
            Serial.printf("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
            bootTimingDone = true;
        }
    }
    else
    {
        Serial.printf("Failed to open: %s\n", path.c_str());
    }
}

void stopPlayback()
{
    Serial.println("Stopping playback");
    if (currentAudioFile)
        currentAudioFile.close();
    isPlaying = false;
    lastNfcUid = "";
    ledSetIdle();
}

// =============================================================================
// DEEP SLEEP
// =============================================================================

void enterDeepSleep()
{
    if (currentAudioFile)
        currentAudioFile.close();
    ledShutdownAnim();
    delay(100);
    jblPowerOff();
    Serial.println("Entering deep sleep...");
    Serial.flush();
    // Wake tylko na BTN_A (VOL+). ESP32 classic nie wspiera oficjalnie
    // ext1 ANY_LOW, a wszystkie przyciski są pull-up do GND. BTN_C/D działają
    // tylko gdy urządzenie jest awake - nie wybudzają z deep sleep.
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_A, LOW);
    esp_deep_sleep_start();
}

// Wybudzanie z deep sleep wymaga przytrzymania BTN_A przez LONG_PRESS_MS.
// ESP32 ext0 wybudza się natychmiast po wykryciu LOW, więc "hold-to-wake"
// musi być zaimplementowane w software: tu odpytujemy przycisk i wracamy
// do snu jeśli zostanie puszczony za wcześnie. Animacja LED (skalowana do
// LED_COUNT) pokazuje postęp przytrzymania. Funkcja musi być wywołana na
// samym początku setup(), PRZED initLeds() (które uruchamia FreeRTOS task).
void handleWakeFromDeepSleep()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
        return;

    Serial.println("[WAKE] Hold BTN_A to confirm wake-up...");

    pinMode(BTN_A, INPUT_PULLUP);

    // Minimalny init FastLED bez taska animacji - sam panel + jasność.
    // initLeds() później pominie addLeds dzięki fastLedInitialized.
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
    fastLedInitialized = true;

    // Debounce po wybudzeniu - kontaktron mechaniczny może bouncować do ~30ms.
    // 50ms daje bezpieczny margines żeby pierwszy glitch nie ubił legalnego holdu.
    delay(50);

    const unsigned long holdStart = millis();
    while (true)
    {
        bool pressed = (digitalRead(BTN_A) == LOW);
        if (!pressed)
        {
            // Potwierdź zwolnienie po krótkim opóźnieniu (debounce)
            delay(10);
            if (digitalRead(BTN_A) != LOW)
                break; // naprawdę puszczony
        }

        unsigned long elapsed = millis() - holdStart;
        if (elapsed >= LONG_PRESS_MS)
        {
            // Przytrzymanie kompletne - kontynuuj normalny boot.
            // LEDy zostaną nadpisane przez initLeds()/ledSetBootProgress().
            Serial.println("[WAKE] Hold confirmed - booting");
            return;
        }

        // Pasek postępu skalowany do dowolnej liczby diod.
        // Lerp od 1 do LED_COUNT w zależności od czasu trzymania.
        int lit = (int)((elapsed * (unsigned long)LED_COUNT) / LONG_PRESS_MS);
        if (lit < 1)
            lit = 1;
        if (lit > LED_COUNT)
            lit = LED_COUNT;
        for (int i = 0; i < LED_COUNT; i++)
        {
            leds[i] = (i < lit) ? CRGB(80, 40, 0) : CRGB::Black; // ciepłe pomarańczowe
        }
        FastLED.show();
        delay(20);
    }

    // Puszczony za wcześnie - cicho z powrotem do deep sleep.
    Serial.println("[WAKE] Released too early - back to deep sleep");
    Serial.flush();
    FastLED.clear();
    FastLED.show();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_A, LOW);
    esp_deep_sleep_start();
}

// =============================================================================
// SYNC (WiFi - uruchamiane ZAMIAST BT, nigdy jednocześnie)
// =============================================================================

String syncServerIP;

String httpGet(const String &path)
{
    WiFiClient client;
    client.setTimeout(HTTP_TIMEOUT / 1000);

    if (!client.connect(syncServerIP.c_str(), SERVER_PORT))
    {
        Serial.printf("[SYNC] Connection failed: %s:%d\n", syncServerIP.c_str(), SERVER_PORT);
        return "";
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  path.c_str(), syncServerIP.c_str());

    unsigned long start = millis();
    while (client.connected() && !client.available())
    {
        if (millis() - start > HTTP_TIMEOUT)
        {
            Serial.println("[SYNC] Timeout");
            client.stop();
            return "";
        }
        delay(10);
    }

    String statusLine = client.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0)
    {
        Serial.printf("[SYNC] HTTP error: %s\n", statusLine.c_str());
        client.stop();
        return "";
    }

    int contentLength = -1;
    while (client.connected())
    {
        String line = client.readStringUntil('\n');
        if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
        {
            contentLength = line.substring(line.indexOf(':') + 1).toInt();
        }
        if (line == "\r" || line.length() == 0)
            break;
    }

    String body;
    if (contentLength > 0)
    {
        body.reserve(contentLength);
        int bytesRead = 0;
        uint8_t buf[512];
        while (bytesRead < contentLength && (client.connected() || client.available()))
        {
            int avail = client.available();
            if (avail > 0)
            {
                int toRead = min(avail, min((int)sizeof(buf), contentLength - bytesRead));
                int got = client.readBytes(buf, toRead);
                body.concat((char *)buf, got);
                bytesRead += got;
            }
            else
            {
                delay(1);
            }
        }
    }
    else
    {
        body = client.readString();
    }

    client.stop();
    return body;
}

bool syncDownloadFile(const String &urlPath, const String &sdPath)
{
    Serial.printf("[SYNC] Download: %s\n", sdPath.c_str());

    WiFiClient client;
    client.setTimeout(HTTP_TIMEOUT / 1000);

    if (!client.connect(syncServerIP.c_str(), SERVER_PORT))
    {
        Serial.println("[SYNC] Connection failed");
        return false;
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  urlPath.c_str(), syncServerIP.c_str());

    unsigned long dlStart = millis();
    while (client.connected() && !client.available())
    {
        if (millis() - dlStart > HTTP_TIMEOUT)
        {
            Serial.println("[SYNC] Timeout");
            client.stop();
            return false;
        }
        delay(10);
    }

    String statusLine = client.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0)
    {
        Serial.printf("[SYNC] HTTP error: %s\n", statusLine.c_str());
        client.stop();
        return false;
    }

    int contentLength = -1;
    while (client.connected())
    {
        String line = client.readStringUntil('\n');
        if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
        {
            contentLength = line.substring(line.indexOf(':') + 1).toInt();
        }
        if (line == "\r" || line.length() == 0)
            break;
    }

    Serial.printf("[SYNC] Size: %d bytes\n", contentLength);

    if (SD.exists(sdPath))
        SD.remove(sdPath);

    File f = SD.open(sdPath, FILE_WRITE);
    if (!f)
    {
        Serial.printf("[SYNC] Cannot create %s\n", sdPath.c_str());
        client.stop();
        return false;
    }

    uint8_t buf[DOWNLOAD_BUF_SIZE];
    int totalWritten = 0;

    while (client.connected() || client.available())
    {
        int available = client.available();
        if (available > 0)
        {
            int toRead = min(available, (int)DOWNLOAD_BUF_SIZE);
            int got = client.readBytes(buf, toRead);
            f.write(buf, got);
            totalWritten += got;

            if (contentLength > 0 && totalWritten >= contentLength)
                break;

            if (totalWritten % (100 * 1024) < DOWNLOAD_BUF_SIZE)
            {
                Serial.printf("[SYNC] %d KB...\n", totalWritten / 1024);
            }
        }
        else
        {
            delay(1);
        }
    }

    f.close();
    client.stop();

    Serial.printf("[SYNC] OK: %d bytes\n", totalWritten);
    return totalWritten > 0;
}

void syncCleanDir(const String &dirPath, std::set<String> &expected)
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
                Serial.printf("[SYNC] Removing: %s\n", fullPath.c_str());
                SD.remove(fullPath);
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

bool performSync()
{
    Serial.println("\n[SYNC] Fetching manifest...");

    String payload = httpGet("/api/sync");
    if (payload.isEmpty())
    {
        Serial.println("[SYNC] Failed to fetch manifest");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Serial.printf("[SYNC] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray figurines = doc["figurines"].as<JsonArray>();
    JsonArray tracks = doc["tracks"].as<JsonArray>();

    Serial.printf("[SYNC] Manifest: %d figurines, %d tracks\n",
                  figurines.size(), tracks.size());

    std::set<String> expectedMusic;
    for (JsonObject t : tracks)
    {
        expectedMusic.insert(t["filename"].as<String>());
    }

    // Pobierz brakujące pliki
    Serial.println("[SYNC] Checking music files...");
    int downloaded = 0, skipped = 0, failed = 0;

    // Policz ile plików do pobrania (do paska postępu)
    int toDownload = 0;
    for (const String &filename : expectedMusic)
    {
        String sdPath = "/music/" + filename;
        if (!SD.exists(sdPath))
            toDownload++;
    }

    int downloadIdx = 0;
    for (const String &filename : expectedMusic)
    {
        String sdPath = "/music/" + filename;
        if (SD.exists(sdPath))
        {
            skipped++;
            continue;
        }
        ledSetSyncProgress(downloadIdx, toDownload);
        String urlPath = "/api/stream/file/" + urlEncode(filename);
        if (syncDownloadFile(urlPath, sdPath))
            downloaded++;
        else
            failed++;
        downloadIdx++;
    }
    if (toDownload > 0)
        ledSetSyncProgress(toDownload, toDownload);
    Serial.printf("[SYNC] Music: %d new, %d existing, %d failed\n", downloaded, skipped, failed);

    // Usuń nieaktualne
    Serial.println("[SYNC] Cleaning obsolete files...");
    syncCleanDir("/music", expectedMusic);

    // Wygeneruj mappings.json
    Serial.println("[SYNC] Generating mappings.json...");

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
        Serial.println("[SYNC] Cannot write mappings.json");
        return false;
    }

    serializeJsonPretty(mappingsDoc, mf);
    mf.close();
    Serial.println("[SYNC] mappings.json saved!");

    return true;
}

void clearSyncFlag()
{
    preferences.begin("musicbox", false);
    preferences.remove("sync_pending");
    preferences.end();
}

void runSyncMode()
{
    Serial.println("\n=== MusicBox SYNC MODE ===\n");
    ledSetSyncWifi();

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);

    if (!wm.autoConnect("MusicBox-Setup"))
    {
        Serial.println("[SYNC] WiFi not connected!");
        ledFlashResult(false);
        clearSyncFlag();
        delay(3000);
        ESP.restart();
    }

    Serial.printf("[SYNC] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

    syncServerIP = SERVER_HOST;
    WiFiClient testClient;
    if (!testClient.connect(syncServerIP.c_str(), SERVER_PORT))
    {
        Serial.println("[SYNC] Cannot reach server!");
        testClient.stop();
        ledFlashResult(false);
        clearSyncFlag();
        delay(3000);
        ESP.restart();
    }
    testClient.stop();
    Serial.println("[SYNC] Server reachable!");
    ledSetSyncProgress(0, 1);

    bool success = performSync();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    Serial.println(success ? "\n[SYNC] COMPLETE!" : "\n[SYNC] FAILED");
    ledFlashResult(success);

    clearSyncFlag();
    delay(2000);
    ESP.restart();
}

// =============================================================================
// BUTTONS
// =============================================================================
//
// Mapowanie akcji:
//   BTN_A (VOL+)  krótki → BT volume +5%
//   BTN_B (VOL-)  krótki → BT volume -5%
//   BTN_B         długi 2s (sam) → deep sleep
//   BTN_A + BTN_B długie 2s → sync mode
//   BTN_C         TODO - akcja nieprzypisana (obecnie tylko log)
//   BTN_D         TODO - akcja nieprzypisana (obecnie tylko log)
//
// Akcje krótkie wykonywane natychmiast na naciśnięcie (nie na puszczenie) -
// szybka reakcja. Długie dopiero po przytrzymaniu przez LONG_PRESS_MS.

void handleButtons()
{
    unsigned long now = millis();

    // Odczyt aktualnego surowego stanu
    bool down[BTN_COUNT];
    for (int i = 0; i < BTN_COUNT; i++)
    {
        down[i] = (digitalRead(buttons[i].pin) == LOW);
    }

    // Obsłuż flagi ISR - rozpocznij timing + fire krótkich akcji
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (b.pressed && b.pressStart == 0)
        {
            b.pressStart = now;
            b.longHandled = false;
            // Akcje krótkie (natychmiast)
            switch (i)
            {
            case 0:
                volumeUp();
                break; // BTN_A
            case 1:
                volumeDown();
                break; // BTN_B
            case 2:    // BTN_C
            case 3:    // BTN_D
                Serial.printf("[BTN] Short press: %s (no action)\n", b.name);
                break;
            }
        }
    }

    // Combo: BTN_A + BTN_B trzymane LONG_PRESS_MS -> SYNC MODE.
    // Wymagamy pressStart>0 dla OBU - inaczej trzymanie BTN_A z hold-to-wake
    // (które omija ISR) + późniejsze BTN_B mogłyby fałszywie wejść w sync.
    if (down[0] && down[1] && !bothABHandled &&
        buttons[0].pressStart > 0 && buttons[1].pressStart > 0)
    {
        unsigned long earliest = max(buttons[0].pressStart, buttons[1].pressStart);
        if (now - earliest >= LONG_PRESS_MS)
        {
            bothABHandled = true;
            Serial.println("\n>>> SYNC MODE");

            if (currentAudioFile)
                currentAudioFile.close();

            preferences.begin("musicbox", false);
            preferences.putBool("sync_pending", true);
            preferences.end();

            preferences.begin("musicbox", true);
            bool verify = preferences.getBool("sync_pending", false);
            preferences.end();
            Serial.printf(">>> Sync flag written & verified: %d\n", verify);

            delay(100);
            ESP.restart();

            delay(100);
            ESP.restart();
        }
    }

    // Długie BTN_B (bez BTN_A) -> deep sleep
    if (down[1] && !down[0] && buttons[1].pressStart > 0 &&
        now - buttons[1].pressStart >= LONG_PRESS_MS && !buttons[1].longHandled)
    {
        buttons[1].longHandled = true;
        Serial.println("\n>>> DEEP SLEEP");
        enterDeepSleep();
    }

    // Zwolnienie przycisków
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (!down[i] && b.pressed)
        {
            b.pressed = false;
            b.pressStart = 0;
            b.longHandled = false;
        }
    }
    // Flaga combo resetuje się gdy którykolwiek z A/B zostanie puszczony
    if (!down[0] || !down[1])
        bothABHandled = false;
}

// =============================================================================
// SETUP - ZOPTYMALIZOWANY
// =============================================================================
//
// Stara kolejność (sekwencyjna, ~5.5s samych delay):
//   SD(+100ms) → sync_check → NFC(+1000ms) → ADC(+500ms) → JBL_power(+500-3500ms)
//   → loadVolume → BT_begin → mappings → [test file]
//
// Nowa kolejność (równoległa, ~150ms delay):
//   SD(0ms) → sync_check → NFC(+100ms) → mappings → NFC_prescan(+200ms)
//   → JBL_async(0ms) → loadVolume → BT_begin → [deferred playback]
//
// BT startuje ~5s wcześniej. JBL bootuje w tle równolegle z BT.
// Jeśli figurka stoi na padzie - plik gotowy do odtwarzania od razu po BT connect.
//

void setup()
{
    bootStart = millis();
    Serial.begin(115200);
    Serial.println("\n\n=== MusicBox ===");
    Serial.printf("[T+%4lu] Boot start\n", 0UL);

    // Hold-to-wake: jeśli boot pochodzi z deep sleep, wymaga przytrzymania
    // BTN_A przez LONG_PRESS_MS. Inicjalizuje minimalnie LEDy do animacji
    // postępu i wraca do snu jeśli przycisk puszczony za wcześnie.
    handleWakeFromDeepSleep();
    // Reset bootStart - hold-to-wake może zabrać ~2s, nie chcemy żeby
    // wszystkie późniejsze logi [T+...] były przesunięte o czas trzymania.
    bootStart = millis();

    // LED - jako pierwsze, żeby pokazać że urządzenie żyje
    initLeds();
    fill_solid(leds, LED_COUNT, CRGB(0, 0, 30));
    FastLED.show();

    // GPIO - natychmiast
    for (int i = 0; i < BTN_COUNT; i++)
    {
        pinMode(buttons[i].pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(buttons[i].pin),
                           btnISR, &buttons[i], FALLING);
    }

    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);
    Serial.printf("[T+%4lu] GPIO ready\n", millis() - bootStart);

    // SD Card - bez delay
    sdReady = initSD();
    if (!sdReady)
    {
        Serial.println("WARNING: No SD card");
    }
    Serial.printf("[T+%4lu] SD %s\n", millis() - bootStart, sdReady ? "OK" : "FAIL");
    ledSetBootProgress(0); // SD done

    // Sprawdź flagę sync PRZED inicjalizacją BT
    preferences.begin("musicbox", true);
    bool syncPending = preferences.getBool("sync_pending", false);
    preferences.end();

    Serial.printf("[BOOT] sync_pending flag: %d\n", syncPending);

    if (syncPending)
    {
        runSyncMode();
        return;
    }

    // === Normalny tryb ===
    Serial.println("\n--- Normal mode (fast boot) ---");

    // --- JBL Power ON: puls startuje tutaj, inne operacje wypełniają czas ---
    // Sprawdzamy status i startujemy puls PRZED NFC/mappings,
    // dzięki czemu 500ms pulsu mija w trakcie inicjalizacji
    bool jblNeedsPower = !isJblOn();
    unsigned long jblPulseStart = 0;
    if (jblNeedsPower)
    {
        Serial.printf("[T+%4lu] JBL OFF - starting power pulse\n", millis() - bootStart);
        digitalWrite(JBL_POWER, HIGH);
        jblPulseStart = millis();
    }
    else
    {
        Serial.printf("[T+%4lu] JBL already ON\n", millis() - bootStart);
    }

    // NFC - oryginalny delay 1000ms, PN532 bywa wolny na cold boot
    nfc.begin();
    delay(1000);

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata)
    {
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);
        nfcReady = true;
    }
    else
    {
        Serial.println("ERROR: PN532 not found!");
    }
    Serial.printf("[T+%4lu] NFC %s\n", millis() - bootStart, nfcReady ? "OK" : "FAIL");
    ledSetBootProgress(1); // NFC done

    // Mappings - ładowane wcześniej (potrzebne do NFC pre-scan)
    if (sdReady)
    {
        loadMappings();
    }
    Serial.printf("[T+%4lu] Mappings loaded (%d)\n", millis() - bootStart, figurineMap.size());
    ledSetBootProgress(2); // Mappings done

// NFC pre-scan - sprawdź czy figurka już stoi na padzie
// Typowy scenariusz: dziecko stawiło figurkę, rodzic włącza urządzenie
#if !TEST_AUDIO_MODE
    if (nfcReady && sdReady)
    {
        uint8_t uid[7];
        uint8_t uidLength;
        if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50))
        {
            String preUid = uidToString(uid, uidLength);
            Serial.printf("[T+%4lu] NFC pre-scan: %s\n", millis() - bootStart, preUid.c_str());

            auto it = figurineMap.find(preUid);
            if (it != figurineMap.end())
            {
                String path = "/music/" + it->second;
                if (SD.exists(path))
                {
                    pendingPlaybackPath = path;
                    pendingPlaybackUid = preUid;
                    Serial.printf("[T+%4lu] Queued: %s\n", millis() - bootStart, path.c_str());
                }
            }
        }
        else
        {
            Serial.printf("[T+%4lu] NFC pre-scan: no tag\n", millis() - bootStart);
        }
    }
#else
    // W test mode - ustaw pending na test file
    if (sdReady && SD.exists(TEST_SD_FILE))
    {
        pendingPlaybackPath = TEST_SD_FILE;
        Serial.printf("[T+%4lu] Test file queued: %s\n", millis() - bootStart, TEST_SD_FILE);
    }
#endif

    // --- Zakończ puls JBL (dopełnij do 500ms jeśli trzeba) ---
    if (jblNeedsPower)
    {
        unsigned long elapsed = millis() - jblPulseStart;
        if (elapsed < JBL_POWER_PRESS_MS)
        {
            delay(JBL_POWER_PRESS_MS - elapsed);
        }
        digitalWrite(JBL_POWER, LOW);
        Serial.printf("[T+%4lu] JBL power pulse done (%lu ms)\n",
                      millis() - bootStart, millis() - jblPulseStart);
    }

    ledSetBootProgress(3); // JBL done

    // Głośność z NVS
    loadBtVolume();

    // Bluetooth A2DP
    ledSetBootProgress(4); // BT step
    Serial.printf("[T+%4lu] BT A2DP starting -> %s\n", millis() - bootStart, BT_SPEAKER_NAME);
    ledSetWaitBt(); // PRZED a2dp.begin() - bo begin() może blokować

    auto cfg = a2dp.defaultConfig(TX_MODE);
    cfg.name = BT_SPEAKER_NAME;
    cfg.auto_reconnect = true;
    a2dp.begin(cfg);
    decoderStream.begin();

    Serial.printf("[T+%4lu] BT A2DP initiated\n", millis() - bootStart);
    Serial.printf("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);

#if TEST_AUDIO_MODE
    Serial.println("=== TEST MODE ===");
#endif
    Serial.println("Ready! Waiting for BT connection...");
}

// =============================================================================
// LOOP
// =============================================================================

void loop()
{
    // Obsługa rozłączenia BT - reset flagi żeby ponowne połączenie ustawiło LED
    if (btVolumeApplied && !a2dp.source().is_connected())
    {
        btVolumeApplied = false;
        if (!isPlaying)
            ledSetWaitBt();
    }

    // Po połączeniu BT: ustaw głośność + uruchom odłożone odtwarzanie
    if (!btVolumeApplied && a2dp.source().is_connected())
    {
        Serial.printf("[T+%4lu] BT connected!\n", millis() - bootStart);
        applyBtVolume();
        btVolumeApplied = true;

        ledSetIdle();

        // Deferred playback - plik wykryty przy boot, czekał na BT
        if (!pendingPlaybackPath.isEmpty() && sdReady)
        {
            currentAudioFile = SD.open(pendingPlaybackPath);
            if (currentAudioFile)
            {
                isPlaying = true;
                lastNfcUid = pendingPlaybackUid;
                ledSetPlaying();
                Serial.printf("[T+%4lu] >>> PLAYBACK START: %s\n", millis() - bootStart, pendingPlaybackPath.c_str());
                Serial.printf("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
                bootTimingDone = true;
            }
            pendingPlaybackPath = "";
            pendingPlaybackUid = "";
        }
    }

    audioLoop();
    handleButtons();

#if !TEST_AUDIO_MODE
    if (millis() - lastNfcRead > NFC_READ_INTERVAL)
    {
        lastNfcRead = millis();

        currentNfcUid = readNfcTag();
        if (!currentNfcUid.isEmpty())
        {
            noTagCount = 0;
            if (currentNfcUid != lastNfcUid)
            {
                startPlayback(currentNfcUid);
            }
        }
        else if (isPlaying && ++noTagCount >= NO_TAG_THRESHOLD)
        {
            stopPlayback();
            noTagCount = 0;
        }
    }
#endif
}
