#include "leds.h"
#if ENABLE_LEDS

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <SD.h>
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"  // isPlaying

// =============================================================================
// Zmienne prywatne modułu LED
// =============================================================================

static CRGB leds[LED_COUNT];
static TaskHandle_t ledTaskHandle = NULL;
static bool fastLedInitialized = false;
static const char *LED_CONFIG_PATH = "/data/led_config.json";

enum LedMode
{
    LED_OFF,
    LED_BOOT,
    LED_WAIT_BT,
    LED_IDLE,
    LED_PLAYING,
    LED_VOLUME,
    LED_SLEEP_READY,
    LED_SYNC_WIFI,
    LED_SYNC_PROGRESS,
    LED_DIAGNOSTIC
};

static volatile LedMode ledMode = LED_OFF;
static volatile unsigned long ledLastUpdate = 0;
static volatile int ledAnimStep = 0;

// Stan animacji beat (LED_PLAYING)
static uint8_t beatHue    = 0;   // aktualny odcień tęczy, przesuwa się z każdym beatem
static uint8_t beatBright = 60;  // jasność: 255 na beat, opada do 60
static uint8_t beatRot    = 0;   // powolna rotacja tęczy
static volatile int ledBootStep = -1;
static volatile unsigned long ledVolumeShowTime = 0;
static volatile int ledSyncLit = 0;
static LedConfig ledConfig = {
    .waitBtColor = {0, 0, 80},
    .idleColor = {0, 80, 0},
    .playingColor = {0, 140, 180},
    .volumeColor = {80, 80, 80},
    .sleepReadyColor = {140, 0, 0},
    .syncColor = {0, 0, 120},
    .diagnosticHeadColor = {90, 0, 120},
    .diagnosticTrailColor = {0, 70, 100},
    .modeMusicColor = {0, 120, 0},
    .modeNfcColor = {0, 0, 120},
    .successColor = {0, 120, 0},
    .errorColor = {120, 0, 0},
    .warningColor = {120, 60, 0},
    .animateWaitBt = true,
    .animateIdle = true,
    .animatePlaying = true,
    .animateSleepReady = true,
    .animateSync = true,
    .animateDiagnostic = true,
};

static CRGB toCRGB(const LedColorConfig &cfg)
{
    return CRGB(cfg.r, cfg.g, cfg.b);
}

static bool parseColor(JsonObject obj, const char *key, LedColorConfig &out)
{
    JsonObject c = obj[key].as<JsonObject>();
    if (!c)
        return false;
    out.r = c["r"] | out.r;
    out.g = c["g"] | out.g;
    out.b = c["b"] | out.b;
    return true;
}

static void writeColor(JsonObject obj, const char *key, const LedColorConfig &cfg)
{
    JsonObject c = obj[key].to<JsonObject>();
    c["r"] = cfg.r;
    c["g"] = cfg.g;
    c["b"] = cfg.b;
}

static bool applyLedConfigDocument(JsonDocument &doc)
{
    JsonObject root = doc.as<JsonObject>();
    if (!root)
        return false;

    parseColor(root, "wait_bt_color", ledConfig.waitBtColor);
    parseColor(root, "idle_color", ledConfig.idleColor);
    parseColor(root, "playing_color", ledConfig.playingColor);
    parseColor(root, "volume_color", ledConfig.volumeColor);
    parseColor(root, "sleep_ready_color", ledConfig.sleepReadyColor);
    parseColor(root, "sync_color", ledConfig.syncColor);
    parseColor(root, "diagnostic_head_color", ledConfig.diagnosticHeadColor);
    parseColor(root, "diagnostic_trail_color", ledConfig.diagnosticTrailColor);
    parseColor(root, "mode_music_color", ledConfig.modeMusicColor);
    parseColor(root, "mode_nfc_color", ledConfig.modeNfcColor);
    parseColor(root, "success_color", ledConfig.successColor);
    parseColor(root, "error_color", ledConfig.errorColor);
    parseColor(root, "warning_color", ledConfig.warningColor);

    ledConfig.animateWaitBt = root["animate_wait_bt"] | ledConfig.animateWaitBt;
    ledConfig.animateIdle = root["animate_idle"] | ledConfig.animateIdle;
    ledConfig.animatePlaying = root["animate_playing"] | ledConfig.animatePlaying;
    ledConfig.animateSleepReady = root["animate_sleep_ready"] | ledConfig.animateSleepReady;
    ledConfig.animateSync = root["animate_sync"] | ledConfig.animateSync;
    ledConfig.animateDiagnostic = root["animate_diagnostic"] | ledConfig.animateDiagnostic;
    return true;
}

// =============================================================================
// Forward declaration
// =============================================================================

static void ledTaskFunc(void *param);

// =============================================================================
// Implementacja
// =============================================================================

void ledPreInitHardware()
{
    if (!fastLedInitialized)
    {
        pinMode(LED_EN, OUTPUT);
        digitalWrite(LED_EN, LOW); // włącz zasilanie LEDów (P-MOSFET)
        FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
        FastLED.setBrightness(LED_BRIGHTNESS);
        fastLedInitialized = true;
    }
}

void ledInit()
{
    ledPreInitHardware();
    FastLED.clear();
    FastLED.show();
    ledMode = LED_OFF;

    // Osobny task FreeRTOS - animacje LED niezależne od loop()
    xTaskCreatePinnedToCore(ledTaskFunc, "led", 4096, NULL, 1, &ledTaskHandle, 1);

    // Pokaż że urządzenie żyje — dim niebieski do pierwszego ledSetBootProgress()
    fill_solid(leds, LED_COUNT, CRGB(0, 0, 30));
    FastLED.show();
}

uint32_t ledGetTaskHWM()
{
    return ledTaskHandle ? uxTaskGetStackHighWaterMark(ledTaskHandle) : 0;
}

bool ledLoadConfigFromSd()
{
    if (!SD.exists(LED_CONFIG_PATH))
        return false;

    File f = SD.open(LED_CONFIG_PATH, FILE_READ);
    if (!f)
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        LOGE("[LED] Config parse error: %s\n", err.c_str());
        return false;
    }
    bool ok = applyLedConfigDocument(doc);
    LOGC("[BOOT] led_config_loaded=%d\n", (int)ok);
    return ok;
}

bool ledSaveConfigJson(const String &json)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err)
        return false;
    if (!applyLedConfigDocument(doc))
        return false;

    if (SD.exists(LED_CONFIG_PATH))
        SD.remove(LED_CONFIG_PATH);
    File f = SD.open(LED_CONFIG_PATH, FILE_WRITE);
    if (!f)
        return false;
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

String ledGetConfigJson()
{
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    writeColor(root, "wait_bt_color", ledConfig.waitBtColor);
    writeColor(root, "idle_color", ledConfig.idleColor);
    writeColor(root, "playing_color", ledConfig.playingColor);
    writeColor(root, "volume_color", ledConfig.volumeColor);
    writeColor(root, "sleep_ready_color", ledConfig.sleepReadyColor);
    writeColor(root, "sync_color", ledConfig.syncColor);
    writeColor(root, "diagnostic_head_color", ledConfig.diagnosticHeadColor);
    writeColor(root, "diagnostic_trail_color", ledConfig.diagnosticTrailColor);
    writeColor(root, "mode_music_color", ledConfig.modeMusicColor);
    writeColor(root, "mode_nfc_color", ledConfig.modeNfcColor);
    writeColor(root, "success_color", ledConfig.successColor);
    writeColor(root, "error_color", ledConfig.errorColor);
    writeColor(root, "warning_color", ledConfig.warningColor);
    root["animate_wait_bt"] = ledConfig.animateWaitBt;
    root["animate_idle"] = ledConfig.animateIdle;
    root["animate_playing"] = ledConfig.animatePlaying;
    root["animate_sleep_ready"] = ledConfig.animateSleepReady;
    root["animate_sync"] = ledConfig.animateSync;
    root["animate_diagnostic"] = ledConfig.animateDiagnostic;

    String out;
    serializeJson(doc, out);
    return out;
}

void ledSuspendTask()
{
    if (ledTaskHandle)
        vTaskSuspend(ledTaskHandle);
}

void ledResumeTask()
{
    if (ledTaskHandle)
        vTaskResume(ledTaskHandle);
}

void ledClear()
{
    FastLED.clear();
    FastLED.show();
}

// Gradient: ciemny teal (0,15,35) → szmaragd (0,130,50) → jasna limonka (50,210,10)
static CRGB wakeColor(int i, int total)
{
    if (total <= 1) return CRGB(50, 210, 10);
    int t = (i * 255) / (total - 1); // 0..255
    uint8_t r, g, b;
    if (t < 128) {
        uint8_t u = (uint8_t)(t * 2);
        r =        ((uint16_t)u * 0)            / 255;
        g = 15  + ((uint16_t)u * (130 - 15))   / 255;
        b = 35  + ((uint16_t)u * (50 - 35))    / 255; // 35→50
    } else {
        uint8_t u = (uint8_t)((t - 128) * 2);
        r =        ((uint16_t)u * 50)           / 255;
        g = 130 + ((uint16_t)u * (210 - 130))  / 255;
        b = 50  - ((uint16_t)u * 40)            / 255;
    }
    return CRGB(r, g, b);
}

void ledSetWakeProgress(int lit)
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < lit) ? wakeColor(i, LED_COUNT) : CRGB::Black;
    }
    FastLED.show();
}

void ledPowerOff()
{
    FastLED.clear();
    FastLED.show();
    pinMode(LED_EN, INPUT); // Hi-Z → R8 podciąga Gate do BAT → Vgs=0 → MOSFET OFF
}

void ledSetBootProgress(int step)
{
    ledMode = LED_BOOT;
    ledBootStep = step;
    // Zakończone kroki świecą na stałe
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < step) ? toCRGB(ledConfig.waitBtColor) : CRGB::Black;
    }
    // Aktualny krok - 3 szybkie mignięcia
    for (int flash = 0; flash < 3; flash++)
    {
        leds[step] = toCRGB(ledConfig.waitBtColor);
        FastLED.show();
        delay(80);
        leds[step] = CRGB::Black;
        FastLED.show();
        delay(80);
    }
    // Zostaw zapalony po mignięciach
    leds[step] = toCRGB(ledConfig.waitBtColor);
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
    beatHue    = 0;
    beatBright = 60;
    beatRot    = 0;
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
        leds[i] = (i < lit) ? toCRGB(ledConfig.volumeColor) : CRGB::Black;
    }
    FastLED.show();
    ledVolumeShowTime = millis();
}

void ledShowModeChange(bool musicMode)
{
    CRGB color = musicMode ? toCRGB(ledConfig.modeMusicColor) : toCRGB(ledConfig.modeNfcColor);
    for (int flash = 0; flash < 2; flash++)
    {
        fill_solid(leds, LED_COUNT, color);
        FastLED.show();
        delay(140);
        FastLED.clear();
        FastLED.show();
        delay(120);
    }
    ledSetIdle();
}

void ledSetSleepReady()
{
    ledMode = LED_SLEEP_READY;
    ledAnimStep = 0;
    ledLastUpdate = millis();
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
        leds[i] = (i < ledSyncLit) ? toCRGB(ledConfig.syncColor) : CRGB(0, 0, 15);
    }
    FastLED.show();
}

void ledSetDiagnostic()
{
    ledMode = LED_DIAGNOSTIC;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledFlashDiagnosticTransition(bool entering)
{
    CRGB outer = toCRGB(ledConfig.diagnosticTrailColor);
    CRGB inner = toCRGB(ledConfig.diagnosticHeadColor);

    for (int flash = 0; flash < 2; ++flash)
    {
        fill_solid(leds, LED_COUNT, outer);
        int mid = LED_COUNT / 2;
        leds[mid] = inner;
        if (mid > 0)
            leds[mid - 1] = inner;
        FastLED.show();
        delay(120);

        FastLED.clear();
        for (int i = 0; i < LED_COUNT; ++i)
        {
            bool edge = entering ? (i <= flash || i >= LED_COUNT - 1 - flash)
                                 : (i >= mid - flash - 1 && i <= mid + flash);
            CRGB dimOuter = outer;
            dimOuter.nscale8_video(80);
            leds[i] = edge ? inner : dimOuter;
        }
        FastLED.show();
        delay(140);

        FastLED.clear();
        FastLED.show();
        delay(90);
    }
}

void ledFlashResult(bool success)
{
    CRGB color = success ? toCRGB(ledConfig.successColor) : toCRGB(ledConfig.errorColor);
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
        fill_solid(leds, LED_COUNT, toCRGB(ledConfig.warningColor));
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

void ledShowBattery(int bars)
{
    // bars: 1 (krytyczny) ... 5 (pełny)
    // Zatrzymaj LED task na czas animacji (LED_OFF → default:break w tasku)
    LedMode prevMode = ledMode;
    ledMode = LED_OFF;
    delay(20); // daj taskowi czas na wyjście z FastLED.show()

    // Kolor zależny od poziomu — 5 odrębnych hue'ów
    CRGB color;
    if      (bars >= 5) color = CRGB(0,    50, 140);  // niebieski  (pełny)
    else if (bars == 4) color = CRGB(0,   130,   0);  // zielony
    else if (bars == 3) color = CRGB(130, 120,   0);  // żółty
    else if (bars == 2) color = CRGB(140,  50,   0);  // pomarańczowy
    else                color = CRGB(140,   0,   0);  // czerwony   (krytyczny)

    // Liczba zapalonych diod: bars=1 → 2, bars=2 → 4, bars=3 → 7, bars=4 → 9, bars=5 → 12
    int lit = map(bars, 1, 5, 2, LED_COUNT);

    // Faza 1: sweep in — zapala po jednej diodzie od lewej
    FastLED.clear();
    FastLED.show();
    for (int i = 0; i < lit; i++) {
        leds[i] = color;
        FastLED.show();
        delay(40);
    }

    // Faza 2: hold 1.5s
    delay(1500);

    // Faza 3: krytyczny poziom — mrugnij 3x na czerwono
    if (bars == 1) {
        for (int b = 0; b < 3; b++) {
            FastLED.clear();
            FastLED.show();
            delay(180);
            for (int i = 0; i < lit; i++) leds[i] = color;
            FastLED.show();
            delay(180);
        }
        delay(300);
    }

    // Faza 4: sweep out — gaśnij od prawej do lewej
    for (int i = lit - 1; i >= 0; i--) {
        leds[i] = CRGB::Black;
        FastLED.show();
        delay(30);
    }

    // Przywróć tryb animacji LED
    if (isPlaying)
        ledSetPlaying();
    else
        ledSetIdle();
    (void)prevMode;
}

static void ledTaskFunc(void *param)
{
    static unsigned long lastLedHeartbeat = 0;
    for (;;)
    {
        unsigned long now = millis();

        // Heartbeat co 5s — PRZED FastLED.show(), żeby log był widoczny nawet gdy show() wisi
        if (now - lastLedHeartbeat > 5000) {
            lastLedHeartbeat = now;
            LOGI("[LED] alive mode=%d hwm=%u\n", (int)ledMode, uxTaskGetStackHighWaterMark(NULL));
        }

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
            if (ledConfig.animateWaitBt)
            {
                ledAnimStep = (ledAnimStep + 1) % 256;
                uint8_t val = cubicwave8(ledAnimStep);
                CRGB base = toCRGB(ledConfig.waitBtColor);
                fill_solid(leds, LED_COUNT, CRGB(map(val, 0, 255, 0, base.r),
                                                map(val, 0, 255, 0, base.g),
                                                map(val, 0, 255, 5, base.b)));
            }
            else
            {
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.waitBtColor));
            }
            FastLED.show();
            break;
        }
        case LED_IDLE:
        {
            if (ledConfig.animateIdle)
            {
                ledAnimStep = (ledAnimStep + 1) % 256;
                uint8_t val = cubicwave8(ledAnimStep);
                CRGB base = toCRGB(ledConfig.idleColor);
                fill_solid(leds, LED_COUNT, CRGB(map(val, 0, 255, 0, base.r),
                                                map(val, 0, 255, 5, base.g),
                                                map(val, 0, 255, 0, base.b)));
            }
            else
            {
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.idleColor));
            }
            FastLED.show();
            break;
        }
        case LED_PLAYING:
        {
            if (!ledConfig.animatePlaying)
            {
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.playingColor));
                FastLED.show();
                break;
            }
            // Na beat: błysk do 255 + przeskok koloru
            if (g_beatDetected)
            {
                g_beatDetected = false;
                beatBright = 255;
                beatHue += 21;  // ~12 beatów = pełny spektrum
            }

            // Między beatami: flash po beacie łagodnie opada do bieżącej energii audio.
            uint8_t target = g_audioEnergy;
            if (beatBright > target)
                beatBright = (uint8_t)max((int)target, (int)beatBright - 30);
            else
                beatBright = target;

            // Tęczowy pierścień — szybsza rotacja
            beatRot += 2;

            for (int i = 0; i < LED_COUNT; i++)
            {
                uint8_t hue = beatHue + beatRot + (uint8_t)(i * 255 / LED_COUNT);
                leds[i] = CHSV(hue, 230, beatBright);
            }
            FastLED.show();
            break;
        }
        case LED_SYNC_WIFI:
        {
            ledAnimStep = !ledAnimStep;
            CRGB color = (ledConfig.animateSync && ledAnimStep) ? toCRGB(ledConfig.syncColor) : CRGB::Black;
            if (!ledConfig.animateSync)
                color = toCRGB(ledConfig.syncColor);
            fill_solid(leds, LED_COUNT, color);
            FastLED.show();
            break;
        }
        case LED_SLEEP_READY:
        {
            ledAnimStep = !ledAnimStep;
            CRGB color = (ledConfig.animateSleepReady && ledAnimStep) ? toCRGB(ledConfig.sleepReadyColor) : CRGB::Black;
            if (!ledConfig.animateSleepReady)
                color = toCRGB(ledConfig.sleepReadyColor);
            fill_solid(leds, LED_COUNT, color);
            FastLED.show();
            break;
        }
        case LED_DIAGNOSTIC:
        {
            if (ledConfig.animateDiagnostic)
            {
                ledAnimStep = (ledAnimStep + 1) % (LED_COUNT * 2);
                int head = ledAnimStep % LED_COUNT;
                uint8_t breath = map(cubicwave8((uint8_t)(ledAnimStep * 8)), 0, 255, 20, 90);
                for (int i = 0; i < LED_COUNT; i++)
                {
                    int dist = abs(i - head);
                    dist = min(dist, LED_COUNT - dist);
                    if (dist == 0)
                    {
                        leds[i] = toCRGB(ledConfig.diagnosticHeadColor);
                    }
                    else if (dist == 1)
                    {
                        CRGB trail = toCRGB(ledConfig.diagnosticTrailColor);
                        leds[i] = CRGB(map(breath, 20, 90, 0, trail.r),
                                       map(breath, 20, 90, 0, trail.g),
                                       map(breath, 20, 90, 0, trail.b));
                    }
                    else
                    {
                        leds[i] = CRGB(8, 0, 18);
                    }
                }
            }
            else
            {
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.diagnosticHeadColor));
            }
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
        else if (ledMode == LED_SLEEP_READY)
            delayMs = 200;
        else if (ledMode == LED_DIAGNOSTIC)
            delayMs = 90;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}

#endif // ENABLE_LEDS
