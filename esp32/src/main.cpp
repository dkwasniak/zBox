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
#define ENABLE_LEDS false // wyłączone podczas debug BT, wrócimy później
#if ENABLE_LEDS
#include <FastLED.h>
#endif
#include <map>
#include <set>

#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

// WiFi (tylko do sync - nie inicjalizowane jednocześnie z BT)
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <esp_bt.h>

// Logging z timestampem (ms od bootu)
#define LOG(fmt, ...) Serial.printf("<%lu> " fmt, millis(), ##__VA_ARGS__)
#define LOGLN(msg) LOG(msg "\n")

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

#define BTN_A 32 // wolny  (RTC)
#define BTN_B 33 // wolny  (RTC)
#define BTN_C 25 // VOL-   (RTC)
#define BTN_D 26 // VOL+   (RTC, wake-up z deep sleep przez ext0)
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
#define AUDIO_BUF_SIZE 2048

// JBL
#define JBL_POWER_PRESS_MS 500
#define JBL_STATUS_THRESHOLD 180
#define JBL_BOOT_WAIT_MS 5000 // timeout na cold boot JBL + A2DP reconnect

// Głośność Bluetooth (AVRCP)
#define BT_VOL_STEP 5
#define BT_VOL_MIN 0
#define BT_VOL_MAX 100
#define BT_VOL_DEFAULT 50

// Idle timeout → deep sleep
#define IDLE_TIMEOUT_MS (10UL * 60 * 1000) // 15 minut bez odtwarzania

// Sync
#define SERVER_HOST "<musicbox-server-ip>"
#define SERVER_PORT 8000
#define HTTP_TIMEOUT 15000
#define DOWNLOAD_BUF_SIZE 16384

// =============================================================================
// OBIEKTY
// =============================================================================

struct AudioInfoLogger : public AudioInfoSupport {
    AudioInfo lastInfo;
    void setAudioInfo(AudioInfo info) override {
        lastInfo = info;
        LOG("[AUDIO] Decoder: SR=%d Hz, Ch=%d, Bits=%d\n",
            info.sample_rate, info.channels, info.bits_per_sample);
    }
    AudioInfo audioInfo() override { return lastInfo; }
} audioInfoLogger;

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Preferences preferences;

A2DPStream a2dp;
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoderStream(&a2dp, &mp3Decoder);

// =============================================================================
// LED
// =============================================================================

#if ENABLE_LEDS
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
#endif

// =============================================================================
// STAN
// =============================================================================

String currentNfcUid;
volatile char lastNfcUid[30] = {};
volatile bool isPlaying = false;
bool nfcReady = false;
bool sdReady = false;
int btVolume = BT_VOL_DEFAULT;
bool btVolumeApplied = false;
volatile bool g_btConnected = false;

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
    {BTN_A, "A", false, 0, 0, false},
    {BTN_B, "B", false, 0, 0, false},
    {BTN_C, "C(VOL-)", false, 0, 0, false},
    {BTN_D, "D(VOL+)", false, 0, 0, false},
};

bool bothCDHandled = false; // flaga dla kombinacji C+D (sync)

int noTagCount = 0;
int nfcErrorCount = 0;

std::map<String, String> figurineMap;    // nfc_uid → filename
std::map<String, String> systemSoundMap; // name → /data/system/filename
unsigned long lastActivityMs = 0;        // idle timeout: czas ostatniej aktywności

// Deferred playback - plik gotowy do odtwarzania po połączeniu BT
String pendingPlaybackPath;
String pendingPlaybackUid;

// Timing - pomiar czasu startu
unsigned long bootStart = 0;
bool bootTimingDone = false;

// NFC events queue (nfc task → main loop)
struct NfcEvent
{
    bool tagPresent;
    char uid[30];
};
QueueHandle_t nfcQueue = NULL;

// Audio task
enum class AudioCmdType : uint8_t { PLAY, STOP };
struct AudioCmd {
    AudioCmdType type;
    char path[256];
};
QueueHandle_t audioQueue = NULL;
TaskHandle_t audioTaskHandle = NULL;

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

#if ENABLE_LEDS

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

#else // !ENABLE_LEDS — stubs, wszystkie wywołania w kodzie pozostają bez zmian

inline void initLeds() {}
inline void ledSetBootProgress(int) {}
inline void ledSetWaitBt() {}
inline void ledSetIdle() {}
inline void ledSetPlaying() {}
inline void ledShowVolume(int) {}
inline void ledSetSyncWifi() {}
inline void ledSetSyncProgress(int, int) {}
inline void ledFlashResult(bool) {}
inline void ledFlashWarning() {}
inline void ledShutdownAnim() {}

#endif // ENABLE_LEDS

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
        delayMicroseconds(200);
    }
    LOG("[JBL] Status ADC: %d\n", maxVal);
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
        LOGLN("[JBL] Already OFF");
        return;
    }
    LOGLN("[JBL] Powering OFF...");
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
    LOG("[VOL] Restored: %d%%\n", btVolume);
}

void applyBtVolume()
{
    static unsigned long lastApply = 0;
    unsigned long now = millis();
    if (now - lastApply < 500)
    {
        LOG("[VOL] Skipped (throttle): %d%%\n", btVolume);
        return;
    }
    lastApply = now;
    a2dp.setVolume(btVolume / 100.0);
    LOG("[VOL] Applied: %d%%\n", btVolume);
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
    LOGLN("Initializing SD card...");
    SPI.begin(18, 19, 23, SD_CS);
    // Usunięto delay(100) - SPI.begin() i SD.begin() obsługują timing wewnętrznie

    if (!SD.begin(SD_CS))
    {
        LOGLN("ERROR: SD mount failed!");
        return false;
    }

    LOG("SD Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

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
        LOGLN("No mappings.json on SD");
        return false;
    }

    File f = SD.open("/data/mappings.json", FILE_READ);
    if (!f)
    {
        LOGLN("Failed to open mappings.json");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        LOG("mappings.json parse error: %s\n", err.c_str());
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
            LOG("  %s -> %s\n", uid.c_str(), file.c_str());
        }
    }

    LOG("Loaded %d figurines\n", figurineMap.size());
    return true;
}

void loadSystemSounds()
{
    systemSoundMap.clear();
    File f = SD.open("/data/system_sounds.json", FILE_READ);
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return; }
    f.close();
    for (JsonPair kv : doc.as<JsonObject>())
        systemSoundMap[String(kv.key().c_str())] = kv.value().as<String>();
    LOG("[SYS] Loaded %d system sounds\n", (int)systemSoundMap.size());
}

// =============================================================================
// AUDIO (SD -> MP3 -> Bluetooth A2DP)
// =============================================================================

void audioTaskFunc(void *param)
{
    File f;
    static uint8_t audioBuf[AUDIO_BUF_SIZE];

    // Telemetria — aktywna przez pierwsze 30s każdego tracka (6 okien po 5s)
    uint32_t telSdBytes = 0, telWrittenBytes = 0, telDrops = 0;
    unsigned long telWindowStart = 0;
    int telWindows = 0;

    for (;;)
    {
        AudioCmd cmd;
        if (xQueueReceive(audioQueue, &cmd, 0) == pdTRUE)
        {
            if (f) f.close();
            if (cmd.type == AudioCmdType::PLAY)
            {
                f = SD.open(cmd.path);
                if (f)
                {
                    isPlaying = true;
                    ledSetPlaying();
                    LOG("[AUDIO] Playing: %s\n", cmd.path);
                    telSdBytes = telWrittenBytes = telDrops = 0;
                    telWindowStart = millis();
                    telWindows = 0;
                }
                else
                {
                    LOG("[AUDIO] Open failed: %s\n", cmd.path);
                    a2dp.clear();
                }
            }
            else
            {
                isPlaying = false;
                ledSetIdle();
                LOGLN("[AUDIO] Stopped");
                telWindows = 6; // wyłącz telemetrię po stopie
                a2dp.clear();
            }
        }

        if (f && f.available())
        {
            int n = f.read(audioBuf, AUDIO_BUF_SIZE);
            if (n > 0)
            {
                telSdBytes += n;
                size_t written = decoderStream.write(audioBuf, n);
                telWrittenBytes += written;
                if ((int)written < n) telDrops++;

                // Log co 5s przez pierwsze 30s tracka
                if (telWindows < 6)
                {
                    unsigned long now = millis();
                    if (now - telWindowStart >= 5000)
                    {
                        float s = (now - telWindowStart) / 1000.0f;
                        LOG("[AUDIO_TEL] SD=%u B/s dec_in=%u B/s drops=%u (write<n)\n",
                            (uint32_t)(telSdBytes / s),
                            (uint32_t)(telWrittenBytes / s),
                            telDrops);
                        telSdBytes = telWrittenBytes = telDrops = 0;
                        telWindowStart = now;
                        telWindows++;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        else if (f && !f.available())
        {
            f.close();
            isPlaying = false;
            lastNfcUid[0] = '\0';
            ledSetIdle();
            LOGLN("[AUDIO] Track ended");
            telWindows = 6;
            a2dp.clear();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
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

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 20))
    {
        noTagCount = 0;
        String uidStr = uidToString(uid, uidLength);
        LOG("\nNFC Tag: %s\n", uidStr.c_str());
        return uidStr;
    }
    return "";
}

// =============================================================================
// NFC TASK (core 1, prio 1 - razem z audio task, ale audio ma prio 5)
// =============================================================================

void nfcTaskFunc(void *param)
{
    int localNoTagCount = 0;
    char localLastUid[30] = {};

    for (;;)
    {
        String uid = readNfcTag();

        if (!uid.isEmpty())
        {
            localNoTagCount = 0;
            if (strcmp(uid.c_str(), localLastUid) != 0)
            {
                strlcpy(localLastUid, uid.c_str(), sizeof(localLastUid));
                NfcEvent evt = {true, {}};
                strlcpy(evt.uid, uid.c_str(), sizeof(evt.uid));
                xQueueSend(nfcQueue, &evt, 0);
            }
        }
        else
        {
            if (++localNoTagCount >= NO_TAG_THRESHOLD && localLastUid[0] != '\0')
            {
                localLastUid[0] = '\0';
                localNoTagCount = 0;
                NfcEvent evt = {false, {}};
                xQueueSend(nfcQueue, &evt, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(NFC_READ_INTERVAL));
    }
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
    if (g_btConnected)
        return true;

    // JBL nie jest połączony. Jeśli ADC mówi OFF - wciśnij power (z debounce).
    static unsigned long lastPulseMs = 0;
    if (!isJblOn() && millis() - lastPulseMs > 10000)
    {
        LOGLN("[JBL] ADC says OFF - pressing power");
        lastPulseMs = millis();
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS); // 500ms — konieczne fizycznie
        digitalWrite(JBL_POWER, LOW);
    }

    // Zwracamy false — caller ustawi pendingPlaybackPath, auto_reconnect zadba o resztę
    return false;
}

void startPlayback(const String &uid)
{
    if (!sdReady)
    {
        LOGLN("SD not ready");
        return;
    }

    auto it = figurineMap.find(uid);
    if (it == figurineMap.end())
    {
        LOG("No mapping for UID: %s\n", uid.c_str());
        ledFlashWarning();
        return;
    }

    String path = "/music/" + it->second;
    if (!SD.exists(path))
    {
        LOG("File missing: %s\n", path.c_str());
        ledFlashWarning();
        return;
    }

    // Auto-recovery: JBL mógł się sam wyłączyć po bezczynności.
    // ensureJblReady() sprawdza ADC, w razie potrzeby wciska power i czeka na A2DP.
    if (!ensureJblReady())
    {
        LOGLN("[PLAY] JBL not ready - deferring until BT connects");
        pendingPlaybackPath = path;
        pendingPlaybackUid = uid;
        ledSetWaitBt();
        return;
    }

    AudioCmd cmd;
    cmd.type = AudioCmdType::PLAY;
    strlcpy(cmd.path, path.c_str(), sizeof(cmd.path));
    xQueueSend(audioQueue, &cmd, 0);
    strlcpy((char*)lastNfcUid, uid.c_str(), sizeof(lastNfcUid));
    isPlaying = true;
    if (!bootTimingDone)
    {
        LOG("[T+%4lu] >>> PLAYBACK START (NFC trigger)\n", millis() - bootStart);
        LOG("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
        bootTimingDone = true;
    }
}

void stopPlayback()
{
    LOGLN("Stopping playback");
    AudioCmd cmd = { AudioCmdType::STOP, {} };
    xQueueSend(audioQueue, &cmd, 0);
    lastNfcUid[0] = '\0';
}

// =============================================================================
// DEEP SLEEP
// =============================================================================

// Odtwarza dźwięk systemowy przez BT i czeka na zakończenie (blokujące).
// Zwraca natychmiast jeśli dźwięk nie istnieje, BT nie podłączony lub timeout.
void playSystemSoundSync(const char *name, uint32_t timeoutMs = 10000)
{
    auto it = systemSoundMap.find(String(name));
    if (it == systemSoundMap.end()) return;
    if (!SD.exists(it->second) || !g_btConnected || !audioQueue || !audioTaskHandle) return;

    // Zatrzymaj bieżący utwór
    { AudioCmd cmd = { AudioCmdType::STOP, {} }; xQueueSend(audioQueue, &cmd, 0); }
    delay(100);

    // Zacznij odtwarzać dźwięk systemowy
    AudioCmd cmd;
    cmd.type = AudioCmdType::PLAY;
    strlcpy(cmd.path, it->second.c_str(), sizeof(cmd.path));
    xQueueSend(audioQueue, &cmd, 0);
    isPlaying = true;

    // Czekaj na zakończenie (audio task ustawi isPlaying=false gdy plik się skończy)
    unsigned long start = millis();
    while (isPlaying && (millis() - start) < timeoutMs)
        delay(50);
}

void enterDeepSleep()
{
    LOGLN("Preparing for deep sleep...");

    // 0. Zagraj dźwięk "sleep" przez BT (jeśli przypisany).
    playSystemSoundSync("power_off");

    // 1. Zabij audio task — zatrzymuje dekoder MP3, nie dotyka BT.
    if (audioTaskHandle) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = NULL;
    }
    delay(50);

    // 2. NIE wywołuj end() ani disconnect().
    //    esp_a2d_disconnect() zawsze wywołuje esp_a2d_media_ctrl(STOP) →
    //    SUSPEND_STREAM_REQ → state Closing → btc_av_state_closing_handler
    //    unhandled → NULL deref → StoreProhibited.
    //
    //    esp_bt_controller_disable() operuje na poziomie radia (poniżej A2DP SM):
    //    A2DP state machine nie dostaje żadnego eventu disconnect.
    //    Musi być wywołane PRZED jblPowerOff() — fizyczne odłączenie JBL
    //    triggeruje bta_av_str_stopped → crash jeśli controller nadal aktywny.
    LOGLN("[SLEEP] Disabling BT controller...");
    esp_bt_controller_disable();
    delay(50);

#if ENABLE_LEDS
    ledMode = LED_OFF;
    if (ledTaskHandle)
        vTaskSuspend(ledTaskHandle);
    delay(20);
    ledShutdownAnim();
#endif

    // 3. JBL OFF — bezpieczne, BT controller już wyłączony, brak eventów.
    jblPowerOff();

    LOGLN("Entering deep sleep...");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

// Wybudzanie z deep sleep wymaga przytrzymania BTN_D przez LONG_PRESS_MS.
// ESP32 ext0 wybudza się natychmiast po wykryciu LOW, więc "hold-to-wake"
// musi być zaimplementowane w software: tu odpytujemy przycisk i wracamy
// do snu jeśli zostanie puszczony za wcześnie. Animacja LED (skalowana do
// LED_COUNT) pokazuje postęp przytrzymania. Funkcja musi być wywołana na
// samym początku setup(), PRZED initLeds() (które uruchamia FreeRTOS task).
void handleWakeFromDeepSleep()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
        return;

    LOGLN("[WAKE] Hold BTN_D to confirm wake-up...");

    pinMode(BTN_D, INPUT_PULLUP);

#if ENABLE_LEDS
    // Minimalny init FastLED bez taska animacji - sam panel + jasność.
    // initLeds() później pominie addLeds dzięki fastLedInitialized.
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
    fastLedInitialized = true;
#endif

    // Debounce po wybudzeniu - kontaktron mechaniczny może bouncować do ~30ms.
    // 50ms daje bezpieczny margines żeby pierwszy glitch nie ubił legalnego holdu.
    delay(50);

    const unsigned long holdStart = millis();
    while (true)
    {
        bool pressed = (digitalRead(BTN_D) == LOW);
        if (!pressed)
        {
            // Potwierdź zwolnienie po krótkim opóźnieniu (debounce)
            delay(10);
            if (digitalRead(BTN_D) != LOW)
                break; // naprawdę puszczony
        }

        unsigned long elapsed = millis() - holdStart;
        if (elapsed >= LONG_PRESS_MS)
        {
            // Przytrzymanie kompletne - kontynuuj normalny boot.
            // LEDy zostaną nadpisane przez initLeds()/ledSetBootProgress().
            LOGLN("[WAKE] Hold confirmed - booting");
            return;
        }

#if ENABLE_LEDS
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
#endif
        delay(20);
    }

    // Puszczony za wcześnie - cicho z powrotem do deep sleep.
    LOGLN("[WAKE] Released too early - back to deep sleep");
    Serial.flush();
#if ENABLE_LEDS
    FastLED.clear();
    FastLED.show();
#endif
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

// =============================================================================
// SYNC (WiFi - uruchamiane ZAMIAST BT, nigdy jednocześnie)
// =============================================================================

String syncServerIP;

void loadSyncMeta(std::map<String, uint32_t> &meta)
{
    meta.clear();
    if (!SD.exists("/data/sync_meta.json")) return;
    File f = SD.open("/data/sync_meta.json", FILE_READ);
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok)
    {
        for (JsonPair kv : doc.as<JsonObject>())
            meta[kv.key().c_str()] = kv.value().as<uint32_t>();
    }
    f.close();
}

void saveSyncMeta(const std::map<String, uint32_t> &meta)
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

bool readHttpHeaders(WiFiClient &client, int &outContentLength)
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

String httpGet(WiFiClient &client, const String &path)
{
    if (!client.connected())
    {
        if (!client.connect(syncServerIP.c_str(), SERVER_PORT))
        {
            LOG("[SYNC] Reconnect failed for GET %s\n", path.c_str());
            return "";
        }
        client.setNoDelay(true);
        client.setTimeout(HTTP_TIMEOUT);
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

bool syncDownloadFile(WiFiClient &client, const String &urlPath, const String &sdPath,
                      uint32_t totalExpectedBytes, uint32_t &syncBytesDownloaded, uint32_t &lastLedUpdate)
{
    LOG("[SYNC] Download: %s\n", sdPath.c_str());

    if (!client.connected())
    {
        if (!client.connect(syncServerIP.c_str(), SERVER_PORT))
        {
            LOGLN("[SYNC] Reconnect failed");
            return false;
        }
        client.setNoDelay(true);
        client.setTimeout(HTTP_TIMEOUT);
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
                LOG("[SYNC] Removing: %s\n", fullPath.c_str());
                SD.remove(fullPath);
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

bool performSync()
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

void clearSyncFlag()
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

// =============================================================================
// BUTTONS
// =============================================================================
//
// Mapowanie akcji:
//   BTN_D (VOL+)  krótki → BT volume +5%
//   BTN_C (VOL-)  krótki → BT volume -5%
//   BTN_C         długi 2s (sam) → deep sleep
//   BTN_C + BTN_D długie 2s → sync mode
//   BTN_A         wolny
//   BTN_B         wolny
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
            lastActivityMs = millis(); // reset idle timer przy każdym naciśnięciu
            // Akcje krótkie (natychmiast)
            switch (i)
            {
            case 0: // BTN_A
            case 1: // BTN_B
                LOG("[BTN] Short press: %s (no action)\n", b.name);
                break;
            case 2:
                volumeDown();
                break; // BTN_C
            case 3:
                volumeUp();
                break; // BTN_D
            }
        }
    }

    // Combo: BTN_C + BTN_D trzymane LONG_PRESS_MS -> SYNC MODE.
    // Wymagamy pressStart>0 dla OBU - inaczej trzymanie BTN_D z hold-to-wake
    // (które omija ISR) + późniejsze BTN_C mogłyby fałszywie wejść w sync.
    if (down[2] && down[3] && !bothCDHandled &&
        buttons[2].pressStart > 0 && buttons[3].pressStart > 0)
    {
        unsigned long earliest = max(buttons[2].pressStart, buttons[3].pressStart);
        if (now - earliest >= LONG_PRESS_MS)
        {
            bothCDHandled = true;
            LOGLN("\n>>> SYNC MODE");

            playSystemSoundSync("sync");

            if (audioQueue)
            {
                AudioCmd cmd = { AudioCmdType::STOP, {} };
                xQueueSend(audioQueue, &cmd, 0);
            }

            {
                File f = SD.open("/data/sync_pending", FILE_WRITE);
                bool written = (bool)f;
                if (f) f.close();
                LOG(">>> Sync flag written & verified: %d\n", written);
            }

            delay(100);
            ESP.restart();
        }
    }

    // Długie BTN_C (bez BTN_D) -> deep sleep
    if (down[2] && !down[3] && buttons[2].pressStart > 0 &&
        now - buttons[2].pressStart >= LONG_PRESS_MS && !buttons[2].longHandled)
    {
        buttons[2].longHandled = true;
        LOGLN("\n>>> DEEP SLEEP");
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
    // Flaga combo resetuje się gdy którykolwiek z C/D zostanie puszczony
    if (!down[2] || !down[3])
        bothCDHandled = false;
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

void onBtStateChange(esp_a2d_connection_state_t state, void *)
{
    g_btConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    LOG("[BT] connection state=%d connected=%d\n", (int)state, (int)g_btConnected);
}

void setup()
{
    bootStart = millis();
    Serial.begin(115200);
    LOGLN("\n\n=== MusicBox ===");
    LOG("[T+%4lu] Boot start\n", 0UL);

    // Hold-to-wake: jeśli boot pochodzi z deep sleep, wymaga przytrzymania
    // BTN_D przez LONG_PRESS_MS. Inicjalizuje minimalnie LEDy do animacji
    // postępu i wraca do snu jeśli przycisk puszczony za wcześnie.
    handleWakeFromDeepSleep();
    // Reset bootStart - hold-to-wake może zabrać ~2s, nie chcemy żeby
    // wszystkie późniejsze logi [T+...] były przesunięte o czas trzymania.
    bootStart = millis();

    // LED - jako pierwsze, żeby pokazać że urządzenie żyje
    initLeds();
#if ENABLE_LEDS
    fill_solid(leds, LED_COUNT, CRGB(0, 0, 30));
    FastLED.show();
#endif

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
    LOG("[T+%4lu] GPIO ready\n", millis() - bootStart);

    // SD Card - bez delay
    sdReady = initSD();
    if (!sdReady)
    {
        LOGLN("WARNING: No SD card");
    }
    LOG("[T+%4lu] SD %s\n", millis() - bootStart, sdReady ? "OK" : "FAIL");
    ledSetBootProgress(0); // SD done

    // Sprawdź flagę sync PRZED inicjalizacją BT
    bool syncPending = SD.exists("/data/sync_pending");
    LOG("[BOOT] sync_pending flag: %d\n", syncPending);

    if (syncPending)
    {
        runSyncMode();
        return;
    }

    // === Normalny tryb ===
    LOGLN("\n--- Normal mode (fast boot) ---");

    // --- JBL Power ON: puls startuje tutaj, inne operacje wypełniają czas ---
    // Sprawdzamy status i startujemy puls PRZED NFC/mappings,
    // dzięki czemu 500ms pulsu mija w trakcie inicjalizacji
    bool jblNeedsPower = !isJblOn();
    unsigned long jblPulseStart = 0;
    if (jblNeedsPower)
    {
        LOG("[T+%4lu] JBL OFF - starting power pulse\n", millis() - bootStart);
        digitalWrite(JBL_POWER, HIGH);
        jblPulseStart = millis();
    }
    else
    {
        LOG("[T+%4lu] JBL already ON\n", millis() - bootStart);
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
        LOGLN("ERROR: PN532 not found!");
    }
    LOG("[T+%4lu] NFC %s\n", millis() - bootStart, nfcReady ? "OK" : "FAIL");
    ledSetBootProgress(1); // NFC done

    // Mappings - ładowane wcześniej (potrzebne do NFC pre-scan)
    if (sdReady)
    {
        loadMappings();
        loadSystemSounds();
    }
    LOG("[T+%4lu] Mappings loaded (%d)\n", millis() - bootStart, figurineMap.size());
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
            LOG("[T+%4lu] NFC pre-scan: %s\n", millis() - bootStart, preUid.c_str());

            auto it = figurineMap.find(preUid);
            if (it != figurineMap.end())
            {
                String path = "/music/" + it->second;
                if (SD.exists(path))
                {
                    pendingPlaybackPath = path;
                    pendingPlaybackUid = preUid;
                    LOG("[T+%4lu] Queued: %s\n", millis() - bootStart, path.c_str());
                }
            }
        }
        else
        {
            LOG("[T+%4lu] NFC pre-scan: no tag\n", millis() - bootStart);
        }
    }
#else
    // W test mode - ustaw pending na test file
    if (sdReady && SD.exists(TEST_SD_FILE))
    {
        pendingPlaybackPath = TEST_SD_FILE;
        LOG("[T+%4lu] Test file queued: %s\n", millis() - bootStart, TEST_SD_FILE);
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
        LOG("[T+%4lu] JBL power pulse done (%lu ms)\n",
                      millis() - bootStart, millis() - jblPulseStart);
    }

    ledSetBootProgress(3); // JBL done

    // Głośność z NVS
    loadBtVolume();

    // Bluetooth A2DP
    ledSetBootProgress(4); // BT step
    LOG("[T+%4lu] BT A2DP starting -> %s\n", millis() - bootStart, BT_SPEAKER_NAME);
    ledSetWaitBt(); // PRZED a2dp.begin() - bo begin() może blokować

    auto cfg = a2dp.defaultConfig(TX_MODE);
    cfg.name = BT_SPEAKER_NAME;
    cfg.auto_reconnect = true;
    a2dp.source().set_avrc_rn_events({});  // ESP jest master volume — ignoruj AVRCP notify od JBL
    a2dp.source().set_on_connection_state_changed(onBtStateChange);  // PRZED begin()
    a2dp.begin(cfg);
    // Bootstrap: na wypadek race condition gdy callback ominął pierwsze połączenie
    delay(100);
    g_btConnected = a2dp.source().is_connected();
    LOG("[BT] initial state captured: connected=%d\n", (int)g_btConnected);
    decoderStream.begin();
    mp3Decoder.addNotifyAudioChange(audioInfoLogger);

#if !TEST_AUDIO_MODE
    nfcQueue = xQueueCreate(5, sizeof(NfcEvent));
    xTaskCreatePinnedToCore(nfcTaskFunc, "nfc", 4096, NULL, 1, NULL, 1);
    LOG("[T+%4lu] NFC task started (core 1)\n", millis() - bootStart);
#endif

    audioQueue = xQueueCreate(3, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(audioTaskFunc, "audio", 8192, NULL, 2, &audioTaskHandle, 1);
    LOG("[T+%4lu] Audio task started (core 1, prio 2)\n", millis() - bootStart);

    LOG("[T+%4lu] BT A2DP initiated\n", millis() - bootStart);
    LOG("[BOOT] Loop task core: %d\n", xPortGetCoreID());
    LOG("\n[BOOT] Setup complete in %lu ms\n", millis() - bootStart);

#if TEST_AUDIO_MODE
    LOGLN("=== TEST MODE ===");
#endif
    lastActivityMs = millis();
    LOGLN("Ready! Waiting for BT connection...");
}

// =============================================================================
// LOOP
// =============================================================================

void loop()
{
    // Obsługa rozłączenia BT - reset flagi żeby ponowne połączenie ustawiło LED
    if (btVolumeApplied && !g_btConnected)
    {
        btVolumeApplied = false;
        if (!isPlaying)
            ledSetWaitBt();
    }

    // Po połączeniu BT: uruchom odłożone odtwarzanie
    if (!btVolumeApplied && g_btConnected)
    {
        LOG("[T+%4lu] BT connected!\n", millis() - bootStart);
        btVolumeApplied = true;
        applyBtVolume();
        ledSetIdle();

        // Deferred playback - plik wykryty przy boot, czekał na BT
        if (!pendingPlaybackPath.isEmpty() && sdReady)
        {
            AudioCmd cmd;
            cmd.type = AudioCmdType::PLAY;
            strlcpy(cmd.path, pendingPlaybackPath.c_str(), sizeof(cmd.path));
            xQueueSend(audioQueue, &cmd, 0);
            strlcpy((char*)lastNfcUid, pendingPlaybackUid.c_str(), sizeof(lastNfcUid));
            isPlaying = true;
            ledSetPlaying();
            LOG("[T+%4lu] >>> PLAYBACK START: %s\n", millis() - bootStart, pendingPlaybackPath.c_str());
            LOG("[BOOT] Total boot-to-play: %lu ms\n", millis() - bootStart);
            bootTimingDone = true;
            pendingPlaybackPath = "";
            pendingPlaybackUid = "";
        }
    }

    handleButtons();
    vTaskDelay(pdMS_TO_TICKS(5)); // yield — pętla nie może głodzić IDLE1

#if !TEST_AUDIO_MODE
    if (nfcQueue)
    {
        NfcEvent nfcEvt;
        while (xQueueReceive(nfcQueue, &nfcEvt, 0) == pdTRUE)
        {
            if (nfcEvt.tagPresent)
            {
                if (strcmp(nfcEvt.uid, (const char*)lastNfcUid) != 0)
                    startPlayback(String(nfcEvt.uid));
            }
            else
            {
                pendingPlaybackPath = "";
                pendingPlaybackUid = "";
                if (isPlaying)
                    stopPlayback();
            }
        }
    }
#endif

    // Idle timeout - brak odtwarzania przez IDLE_TIMEOUT_MS → deep sleep
    if (isPlaying)
        lastActivityMs = millis();
    else if (lastActivityMs > 0 && millis() - lastActivityMs > IDLE_TIMEOUT_MS)
    {
        LOGLN("[IDLE] Timeout - entering deep sleep");
        enterDeepSleep();
    }

    // Heartbeat - diagnostyka zawieszania loop
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 2000) {
        lastHeartbeat = millis();
        LOG("[LOOP] alive, isPlaying=%d btConn=%d pendingPath=%s\n",
            (int)isPlaying,
            (int)g_btConnected,
            pendingPlaybackPath.c_str());
    }
}
