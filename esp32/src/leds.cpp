#include "leds.h"
#if ENABLE_LEDS

#include <Arduino.h>
#include <FastLED.h>
#include "musicbox_config.h"
#include "persistent_log.h"
#include "state.h"  // isPlaying

// =============================================================================
// Zmienne prywatne modułu LED
// =============================================================================

static CRGB leds[LED_COUNT];
static TaskHandle_t ledTaskHandle = NULL;
static bool fastLedInitialized = false;

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

static volatile LedMode ledMode = LED_OFF;
static volatile unsigned long ledLastUpdate = 0;
static volatile int ledAnimStep = 0;
static volatile int ledBootStep = -1;
static volatile unsigned long ledVolumeShowTime = 0;
static volatile int ledSyncLit = 0;

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

void ledSetWakeProgress(int lit)
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        leds[i] = (i < lit) ? CRGB(80, 40, 0) : CRGB::Black; // ciepłe pomarańczowe
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
            PLOGF("[LED] alive mode=%d hwm=%u", (int)ledMode, uxTaskGetStackHighWaterMark(NULL));
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

#endif // ENABLE_LEDS
