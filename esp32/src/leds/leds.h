#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "zbox_config.h"

#if ENABLE_LEDS

struct LedColorConfig {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct LedConfig {
    LedColorConfig waitBtColor;
    LedColorConfig idleColor;
    LedColorConfig playingColor;
    LedColorConfig volumeColor;
    LedColorConfig sleepReadyColor;
    LedColorConfig syncColor;
    LedColorConfig syncEntryHeadColor;
    LedColorConfig syncEntryTrailColor;
    LedColorConfig modeMusicColor;
    LedColorConfig modeNfcColor;
    LedColorConfig successColor;
    LedColorConfig errorColor;
    LedColorConfig warningColor;
    LedColorConfig nightLightColor;
    bool animateWaitBt;
    bool animateIdle;
    bool animatePlaying;
    bool animateSleepReady;
    bool animateSync;
    bool animateSyncEntry;
};

// Hardware-only init — bez FreeRTOS.
// Wywołać PRZED initLeds() gdy boot pochodzi z deep sleep (handleWakeFromDeepSleep).
// initLeds() wykryje fastLedInitialized=true i pominie ponowne addLeds.
void ledPreInitHardware();

// Pełna inicjalizacja: ledPreInitHardware (jeśli nie zrobione) + FreeRTOS task.
void ledInit();

// Sterowanie taskiem (safe do wywołania z dowolnego kontekstu RTOS)
void ledSuspendTask(); // vTaskSuspend(ledTaskHandle), bez zmiany aktualnego trybu
void ledResumeTask();  // vTaskResume(ledTaskHandle)

// Operacje FastLED bez FreeRTOS (dla handleWakeFromDeepSleep / enterDeepSleep)
void ledClear();                  // FastLED.clear() + show()
void ledSetWakeProgress(int lit); // pasek postępu (pomarańczowy) + show()
void ledSetWakeNightLightBreathing(uint8_t phase);
void ledPowerOff();               // clear + show + wyłącz zasilanie (LED_EN Hi-Z)

// Diagnostyka
uint32_t ledGetTaskHWM(); // uxTaskGetStackHighWaterMark dla LED taska

// Publiczne API animacji / trybów
void ledSetBootProgress(int step);
void ledSetWaitBt();
void ledSetIdle();
void ledSetPlaying();
void ledSetNightLight(int brightnessPercent);
void ledShowVolume(int volumePercent);
void ledShowModeChange(bool musicMode);
void ledSetSleepReady();
void ledSetWarningFlash();
void ledSetSyncWifi();
void ledSetSyncProgress(int current, int total);
void ledSetSyncEntry();
void ledFlashSyncTransition(bool entering);
void ledFlashResult(bool success);
void ledFlashWarning();
void ledShutdownAnim();
void ledShowBattery(int bars);
bool ledLoadConfigFromSd();
bool ledSaveConfigJson(const String &json);
String ledGetConfigJson();

#else // !ENABLE_LEDS — stubs

inline void ledPreInitHardware() {}
inline void ledInit() {}
inline uint32_t ledGetTaskHWM() { return 0; }
inline void ledSuspendTask() {}
inline void ledResumeTask() {}
inline void ledClear() {}
inline void ledSetWakeProgress(int) {}
inline void ledSetWakeNightLightBreathing(uint8_t) {}
inline void ledPowerOff() {}
inline void ledSetBootProgress(int) {}
inline void ledSetWaitBt() {}
inline void ledSetIdle() {}
inline void ledSetPlaying() {}
inline void ledSetNightLight(int) {}
inline void ledShowVolume(int) {}
inline void ledShowModeChange(bool) {}
inline void ledSetSleepReady() {}
inline void ledSetWarningFlash() {}
inline void ledSetSyncWifi() {}
inline void ledSetSyncProgress(int, int) {}
inline void ledSetSyncEntry() {}
inline void ledFlashSyncTransition(bool) {}
inline void ledFlashResult(bool) {}
inline void ledFlashWarning() {}
inline void ledShutdownAnim() {}
inline void ledShowBattery(int) {}
inline bool ledLoadConfigFromSd() { return false; }
inline bool ledSaveConfigJson(const String &) { return false; }
inline String ledGetConfigJson() { return "{}"; }

#endif // ENABLE_LEDS
