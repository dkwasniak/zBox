#include "leds.h"
#if ENABLE_LEDS

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <SD.h>
#include "battery.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "volume_scale.h"

// =============================================================================
// Private module variables for LED
// =============================================================================

static CRGB leds[LED_COUNT];
static TaskHandle_t ledTaskHandle = NULL;
static bool fastLedInitialized = false;
static const char *LED_CONFIG_PATH = "/data/led_config.json";
static constexpr UBaseType_t LED_TASK_PRIORITY = 1;
static constexpr unsigned long LED_SHOW_WARN_MS = 30;
static constexpr unsigned long LED_FRAME_GAP_WARN_MS = 250;

enum LedMode
{
    LED_OFF,
    LED_BOOT,
    LED_WAIT_BT,
    LED_IDLE,
    LED_PLAYING,
    LED_NIGHT_LIGHT,
    LED_VOLUME,
    LED_SLEEP_READY,
    LED_WARN_FLASH,
    LED_SYNC_WIFI,
    LED_SYNC_PROGRESS,
    LED_SYNC_ENTRY
};

static volatile LedMode ledMode = LED_OFF;
static volatile unsigned long ledLastUpdate = 0;
static volatile int ledAnimStep = 0;

// Beat animation state (LED_PLAYING)
static uint8_t beatHue    = 0;   // current rainbow hue, advances with each beat
static uint8_t beatBright = 90;  // brightness: 255 on beat, decays to the music floor
static uint8_t beatRot    = 0;   // slow rainbow rotation
static volatile int ledBootStep = -1;
static volatile unsigned long ledVolumeShowTime = 0;
static LedMode ledPreVolumeMode = LED_IDLE;
static volatile int ledSyncLit = 0;
static volatile int ledNightLightBrightnessPercent = NIGHT_LIGHT_BRIGHTNESS_DEFAULT;
static bool s_lowBatteryWarningEnabled = false;
static unsigned long s_lowBatteryLastCheckMs = 0;
static unsigned long s_lowBatteryBlinkStartMs = 0;
static LedConfig ledConfig = {
    .waitBtColor = {0, 0, 80},
    .idleColor = {0, 80, 0},
    .playingColor = {0, 140, 180},
    .volumeColor = {80, 80, 80},
    .sleepReadyColor = {140, 0, 0},
    .syncColor = {0, 0, 120},
    .syncEntryHeadColor = {90, 0, 120},
    .syncEntryTrailColor = {0, 70, 100},
    .modeMusicColor = {0, 120, 0},
    .modeNfcColor = {0, 0, 120},
    .successColor = {0, 120, 0},
    .errorColor = {120, 0, 0},
    .warningColor = {120, 60, 0},
    .nightLightColor = {255, 72, 4},
    .animateWaitBt = true,
    .animateIdle = true,
    .animatePlaying = true,
    .animateSleepReady = true,
    .animateSync = true,
    .animateSyncEntry = true,
};

namespace {
constexpr float LOW_BATTERY_WARNING_VOLTAGE = 3.725f;   // approx. 25% for 1S Li-Po
constexpr unsigned long LOW_BATTERY_CHECK_MS = 60000UL; // sample battery once per minute
constexpr unsigned long LOW_BATTERY_BLINK_MS = 1200UL;  // 4 short blinks
constexpr unsigned long LOW_BATTERY_BLINK_STEP_MS = 150UL;
constexpr uint8_t LOW_BATTERY_LED_INDEX = LED_COUNT - 1;
}

static void showLedsMeasured(const char *modeName);

static CRGB toCRGB(const LedColorConfig &cfg)
{
    return CRGB(cfg.r, cfg.g, cfg.b);
}

static CRGB scaledNightLightColor(int brightnessPercent)
{
    uint8_t level = map(constrain(brightnessPercent, 0, 100), 0, 100, 0, 255);
    const LedColorConfig &base = ledConfig.nightLightColor;

    // WS2812 at higher brightness levels tends to "wash out" warm colours
    // because green and blue grow too aggressively in perceived brightness.
    // For the night light we scale R/G/B independently to keep a warmer tint
    // even at higher brightness levels.
    uint8_t redLevel = level;
    uint8_t greenLevel = scale8(level, 176);
    uint8_t blueLevel = scale8(level, 96);

    return CRGB(
        scale8(base.r, redLevel),
        scale8(base.g, greenLevel),
        scale8(base.b, blueLevel));
}

static CRGB batteryGradientColor(int index, int lit)
{
    if (lit <= 1) return CRGB(140, 0, 0);

    const uint8_t t = static_cast<uint8_t>((index * 255) / (lit - 1));
    if (t < 85) {
        const uint8_t u = static_cast<uint8_t>((t * 255) / 85);
        return CRGB(140, (50U * u) / 255, 0);
    }
    if (t < 170) {
        const uint8_t u = static_cast<uint8_t>(((t - 85) * 255) / 85);
        return CRGB(140 - ((40U * u) / 255), 50 + ((70U * u) / 255), 0);
    }

    const uint8_t u = static_cast<uint8_t>(((t - 170) * 255) / 85);
    return CRGB(100 - ((100U * u) / 255), 120 + ((10U * u) / 255), 0);
}

static CRGB volumeGradientColor(int index, int lit)
{
    if (lit <= 1) return CRGB(0, 130, 0);

    const uint8_t t = static_cast<uint8_t>((index * 255) / (lit - 1));
    if (t < 85) {
        const uint8_t u = static_cast<uint8_t>((t * 255) / 85);
        return CRGB((130U * u) / 255, 130, 0);
    }
    if (t < 170) {
        const uint8_t u = static_cast<uint8_t>(((t - 85) * 255) / 85);
        return CRGB(130 + ((10U * u) / 255), 130 - ((80U * u) / 255), 0);
    }

    const uint8_t u = static_cast<uint8_t>(((t - 170) * 255) / 85);
    return CRGB(140, 50 - ((50U * u) / 255), 0);
}

static void updateLowBatteryWarning(unsigned long now)
{
    if (s_lowBatteryLastCheckMs != 0 &&
        now - s_lowBatteryLastCheckMs < LOW_BATTERY_CHECK_MS)
        return;

    s_lowBatteryLastCheckMs = now;
    const float batteryVoltage = readBatteryVoltage();
    s_lowBatteryWarningEnabled = batteryVoltage < LOW_BATTERY_WARNING_VOLTAGE;
    if (s_lowBatteryWarningEnabled)
        s_lowBatteryBlinkStartMs = now;
}

static void applyLowBatteryOverlay(unsigned long now)
{
    if (!s_lowBatteryWarningEnabled || LED_COUNT == 0)
        return;

    const unsigned long blinkElapsed = now - s_lowBatteryBlinkStartMs;
    if (blinkElapsed < LOW_BATTERY_BLINK_MS &&
        ((blinkElapsed / LOW_BATTERY_BLINK_STEP_MS) % 2 == 0)) {
        leds[LOW_BATTERY_LED_INDEX] = CRGB(140, 0, 0);
    }
}

static void showLowBatteryWarningOnly(unsigned long now)
{
    FastLED.clear();
    applyLowBatteryOverlay(now);
    showLedsMeasured("off");
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

static const char *ledModeName(LedMode mode)
{
    switch (mode)
    {
    case LED_WAIT_BT:
        return "wait_bt";
    case LED_IDLE:
        return "idle";
    case LED_PLAYING:
        return "playing";
    case LED_NIGHT_LIGHT:
        return "night_light";
    case LED_SLEEP_READY:
        return "sleep_ready";
    case LED_WARN_FLASH:
        return "warn_flash";
    case LED_SYNC_ENTRY:
        return "sync_entry";
    default:
        return "other";
    }
}

static bool shouldLogFrameGap(LedMode mode)
{
    switch (mode)
    {
    case LED_WAIT_BT:
    case LED_IDLE:
    case LED_PLAYING:
    case LED_NIGHT_LIGHT:
    case LED_SLEEP_READY:
    case LED_WARN_FLASH:
    case LED_SYNC_ENTRY:
        return true;
    default:
        return false;
    }
}

static void showLedsMeasured(const char *modeName)
{
    unsigned long start = millis();
    FastLED.show();
    unsigned long elapsed = millis() - start;
    if (elapsed > LED_SHOW_WARN_MS)
    {
        LOGW("[LED] FastLED.show mode=%s dt=%lu ms\n", modeName, elapsed);
    }
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
    parseColor(root, "sync_entry_head_color", ledConfig.syncEntryHeadColor);
    parseColor(root, "sync_entry_trail_color", ledConfig.syncEntryTrailColor);
    parseColor(root, "mode_music_color", ledConfig.modeMusicColor);
    parseColor(root, "mode_nfc_color", ledConfig.modeNfcColor);
    parseColor(root, "success_color", ledConfig.successColor);
    parseColor(root, "error_color", ledConfig.errorColor);
    parseColor(root, "warning_color", ledConfig.warningColor);
    parseColor(root, "night_light_color", ledConfig.nightLightColor);

    ledConfig.animateWaitBt = root["animate_wait_bt"] | ledConfig.animateWaitBt;
    ledConfig.animateIdle = root["animate_idle"] | ledConfig.animateIdle;
    ledConfig.animatePlaying = root["animate_playing"] | ledConfig.animatePlaying;
    ledConfig.animateSleepReady = root["animate_sleep_ready"] | ledConfig.animateSleepReady;
    ledConfig.animateSync = root["animate_sync"] | ledConfig.animateSync;
    ledConfig.animateSyncEntry = root["animate_sync_entry"] | ledConfig.animateSyncEntry;
    return true;
}

// =============================================================================
// Forward declarations
// =============================================================================

static void ledTaskFunc(void *param);

// =============================================================================
// Implementation
// =============================================================================

void ledPreInitHardware()
{
    if (!fastLedInitialized)
    {
        pinMode(LED_EN, OUTPUT);
        digitalWrite(LED_EN, LOW); // enable LED power supply (P-MOSFET)
        FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
        FastLED.setBrightness(LED_BRIGHTNESS);
        fastLedInitialized = true;
    }
}

void ledInit()
{
    ledPreInitHardware();
    bool preserveNightLight = (ledMode == LED_NIGHT_LIGHT) && runtimeIsNightLight();
    if (!preserveNightLight)
    {
        FastLED.clear();
        FastLED.show();
        ledMode = LED_OFF;
    }

    // Separate FreeRTOS task - LED animations independent of loop()
    xTaskCreatePinnedToCore(ledTaskFunc, "led", 4096, NULL, LED_TASK_PRIORITY, &ledTaskHandle, 1);

    // Leave LEDs off here; the dispatcher applies the first real scene after boot init.
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
    writeColor(root, "sync_entry_head_color", ledConfig.syncEntryHeadColor);
    writeColor(root, "sync_entry_trail_color", ledConfig.syncEntryTrailColor);
    writeColor(root, "mode_music_color", ledConfig.modeMusicColor);
    writeColor(root, "mode_nfc_color", ledConfig.modeNfcColor);
    writeColor(root, "success_color", ledConfig.successColor);
    writeColor(root, "error_color", ledConfig.errorColor);
    writeColor(root, "warning_color", ledConfig.warningColor);
    writeColor(root, "night_light_color", ledConfig.nightLightColor);
    root["animate_wait_bt"] = ledConfig.animateWaitBt;
    root["animate_idle"] = ledConfig.animateIdle;
    root["animate_playing"] = ledConfig.animatePlaying;
    root["animate_sleep_ready"] = ledConfig.animateSleepReady;
    root["animate_sync"] = ledConfig.animateSync;
    root["animate_sync_entry"] = ledConfig.animateSyncEntry;

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
    ledMode = LED_OFF;
    FastLED.clear();
    FastLED.show();
}

// Gradient: dark teal (0,15,35) → emerald (0,130,50) → bright lime (50,210,10)
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

void ledSetWakeNightLightBreathing(uint8_t phase)
{
    CRGB color = toCRGB(ledConfig.nightLightColor);
    uint8_t breath = map(cubicwave8(phase), 0, 255, 32, 180);
    color.nscale8_video(breath);
    fill_solid(leds, LED_COUNT, color);
    FastLED.show();
}

void ledPowerOff()
{
    FastLED.clear();
    FastLED.show();
    pinMode(LED_EN, INPUT); // Hi-Z → R8 pulls Gate to BAT → Vgs=0 → MOSFET OFF
}

void ledSetBootProgress(int step)
{
    ledMode = LED_BOOT;
    ledBootStep = step;
    // Completed steps stay lit permanently
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < step) ? toCRGB(ledConfig.waitBtColor) : CRGB::Black;
    }
    // Current step - 3 quick flashes
    for (int flash = 0; flash < 3; flash++)
    {
        leds[step] = toCRGB(ledConfig.waitBtColor);
        FastLED.show();
        delay(80);
        leds[step] = CRGB::Black;
        FastLED.show();
        delay(80);
    }
    // Leave lit after the flashes
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
    beatBright = 90;
    beatRot    = 0;
    ledMode = LED_PLAYING;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledSetNightLight(int brightnessPercent)
{
    ledMode = LED_NIGHT_LIGHT;
    ledNightLightBrightnessPercent = constrain(brightnessPercent, 0, 100);
    ledAnimStep = 0;
    ledLastUpdate = millis();
    fill_solid(leds, LED_COUNT, scaledNightLightColor(ledNightLightBrightnessPercent));
    FastLED.show();
}

void ledShowVolume(int volumeLevel)
{
    ledPreVolumeMode = (ledMode == LED_VOLUME) ? ledPreVolumeMode : ledMode;
    ledMode = LED_VOLUME;
    int lit = constrain((int)clampVolumeLevel(volumeLevel), 0, LED_COUNT);
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < lit) ? volumeGradientColor(i, lit) : CRGB::Black;
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

void ledSetWarningFlash()
{
    ledMode = LED_WARN_FLASH;
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

void ledSetSyncEntry()
{
    ledMode = LED_SYNC_ENTRY;
    ledAnimStep = 0;
    ledLastUpdate = millis();
}

void ledFlashSyncTransition(bool entering)
{
    ledMode = LED_OFF;
    delay(20); // stop the current task animation before the blocking transition

    CRGB outer = toCRGB(ledConfig.syncEntryTrailColor);
    CRGB inner = toCRGB(ledConfig.syncEntryHeadColor);

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
    ledMode = LED_OFF;
    delay(20); // prevent the LED task from overwriting the shutdown animation

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
    // bars: 1 (critical) ... 5 (full)
    // Pause the LED task for the duration of the animation (LED_OFF → default:break in task)
    LedMode prevMode = ledMode;
    ledMode = LED_OFF;
    delay(20); // give the task time to exit FastLED.show()

    // Number of lit LEDs: bars=1 → 2, bars=2 → 4, bars=3 → 7, bars=4 → 9, bars=5 → 12
    int lit = map(bars, 1, 5, 2, LED_COUNT);
    lit = constrain(lit, 1, LED_COUNT);

    // Phase 1: sweep in — light one LED at a time from the left
    FastLED.clear();
    FastLED.show();
    for (int i = 0; i < lit; i++) {
        leds[i] = batteryGradientColor(i, lit);
        FastLED.show();
        delay(40);
    }

    // Phase 2: hold 1.5s
    delay(1500);

    // Phase 3: critical level — blink 3x in red
    if (bars == 1) {
        for (int b = 0; b < 3; b++) {
            FastLED.clear();
            FastLED.show();
            delay(180);
            for (int i = 0; i < lit; i++) leds[i] = CRGB(140, 0, 0);
            FastLED.show();
            delay(180);
        }
        delay(300);
    }

    // Phase 4: sweep out — fade from right to left
    for (int i = lit - 1; i >= 0; i--) {
        leds[i] = CRGB::Black;
        FastLED.show();
        delay(30);
    }

    // Restore mode from before battery animation
    if (prevMode == LED_PLAYING)
        ledSetPlaying();
    else
        ledSetIdle();
}

static void ledTaskFunc(void *param)
{
    static unsigned long lastLedHeartbeat = 0;
    static unsigned long lastFrameMs = 0;
    for (;;)
    {
        unsigned long now = millis();
        LedMode currentMode = ledMode;
        if (lastFrameMs != 0 && shouldLogFrameGap(currentMode) && now - lastFrameMs > LED_FRAME_GAP_WARN_MS) {
            LOGW("[LED] %s frame gap=%lu ms\n", ledModeName(currentMode), now - lastFrameMs);
        }
        lastFrameMs = now;
        updateLowBatteryWarning(now);

        // Heartbeat every 5s — BEFORE FastLED.show(), so the log is visible even if show() hangs
        if (now - lastLedHeartbeat > 5000) {
            lastLedHeartbeat = now;
            LOGI("[LED] alive mode=%d hwm=%u\n", (int)ledMode, uxTaskGetStackHighWaterMark(NULL));
        }

        // Volume overlay - return to previous mode after 1s
        if (ledMode == LED_VOLUME && now - ledVolumeShowTime >= 1000)
        {
            if (ledPreVolumeMode == LED_PLAYING)
                ledSetPlaying();
            else
                ledSetIdle();
        }

        switch (ledMode)
        {
        case LED_OFF:
        {
            if (s_lowBatteryWarningEnabled) {
                showLowBatteryWarningOnly(now);
            }
            break;
        }
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
            showLedsMeasured("wait_bt");
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
            applyLowBatteryOverlay(now);
            showLedsMeasured("idle");
            break;
        }
        case LED_PLAYING:
        {
            if (!ledConfig.animatePlaying)
            {
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.playingColor));
                showLedsMeasured("playing");
                break;
            }
            // On beat: flash to 255 + colour jump
            if (g_beatDetected)
            {
                g_beatDetected = false;
                beatBright = 255;
                beatHue += 37;  // stronger colour jump on each hit
            }

            // Between beats: post-beat flash gently decays to current audio energy.
            uint8_t target = max((int)g_audioEnergy, 80);
            if (beatBright > target)
                beatBright = (uint8_t)max((int)target, (int)beatBright - 42);
            else
                beatBright = target;

            // Rainbow ring — faster rotation so quiet passages still feel alive.
            beatRot += 5;

            for (int i = 0; i < LED_COUNT; i++)
            {
                uint8_t hue = beatHue + beatRot + (uint8_t)(i * 255 / LED_COUNT);
                leds[i] = CHSV(hue, 230, beatBright);
            }
            applyLowBatteryOverlay(now);
            showLedsMeasured("playing");
            break;
        }
        case LED_NIGHT_LIGHT:
        {
            fill_solid(leds, LED_COUNT, scaledNightLightColor(ledNightLightBrightnessPercent));
            showLedsMeasured("night_light");
            break;
        }
        case LED_SYNC_WIFI:
        {
            ledAnimStep = !ledAnimStep;
            CRGB color = (ledConfig.animateSync && ledAnimStep) ? toCRGB(ledConfig.syncColor) : CRGB::Black;
            if (!ledConfig.animateSync)
                color = toCRGB(ledConfig.syncColor);
            fill_solid(leds, LED_COUNT, color);
            showLedsMeasured("sync_wifi");
            break;
        }
        case LED_SLEEP_READY:
        {
            ledAnimStep = !ledAnimStep;
            CRGB color = (ledConfig.animateSleepReady && ledAnimStep) ? toCRGB(ledConfig.sleepReadyColor) : CRGB::Black;
            if (!ledConfig.animateSleepReady)
                color = toCRGB(ledConfig.sleepReadyColor);
            fill_solid(leds, LED_COUNT, color);
            showLedsMeasured("sleep_ready");
            break;
        }
        case LED_WARN_FLASH:
        {
            // Slow breath using cubicwave — gentle amber pulsing, not abrupt on/off
            ledAnimStep = (ledAnimStep + 3) % 256;
            uint8_t breath = cubicwave8(ledAnimStep);
            CRGB base = toCRGB(ledConfig.warningColor);
            uint8_t lo = 20;
            fill_solid(leds, LED_COUNT, CRGB(
                map(breath, 0, 255, lo, base.r),
                map(breath, 0, 255, lo / 4, base.g),
                0));
            showLedsMeasured("warn_flash");
            break;
        }
        case LED_SYNC_ENTRY:
        {
            if (ledConfig.animateSyncEntry)
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
                        leds[i] = toCRGB(ledConfig.syncEntryHeadColor);
                    }
                    else if (dist == 1)
                    {
                        CRGB trail = toCRGB(ledConfig.syncEntryTrailColor);
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
                fill_solid(leds, LED_COUNT, toCRGB(ledConfig.syncEntryHeadColor));
            }
            showLedsMeasured("sync_entry");
            break;
        }
        default:
            break;
        }

        // Delay depends on current mode
        int delayMs = 15;
        if (ledMode == LED_PLAYING)
            delayMs = 45;
        else if (ledMode == LED_SYNC_WIFI)
            delayMs = 400;
        else if (ledMode == LED_SLEEP_READY)
            delayMs = 200;
        else if (ledMode == LED_WARN_FLASH)
            delayMs = 15;
        else if (ledMode == LED_SYNC_ENTRY)
            delayMs = 90;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}

#endif // ENABLE_LEDS
