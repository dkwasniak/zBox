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

// Hardware-only init — no FreeRTOS.
// Call BEFORE ledInit() when booting from deep sleep (handleWakeFromDeepSleep).
// ledInit() will detect fastLedInitialized=true and skip re-adding LEDs.
void ledPreInitHardware();

// Full initialization: ledPreInitHardware (if not already done) + FreeRTOS task.
void ledInit();

// Task control (safe to call from any RTOS context)
void ledSuspendTask(); // vTaskSuspend(ledTaskHandle), without changing the current mode
void ledResumeTask();  // vTaskResume(ledTaskHandle)

// FastLED operations without FreeRTOS (for handleWakeFromDeepSleep / enterDeepSleep)
void ledClear();                  // FastLED.clear() + show()
void ledSetWakeProgress(int lit); // progress bar (orange) + show()
void ledSetWakeNightLightBreathing(uint8_t phase);
void ledPowerOff();               // clear + show + disable power supply (LED_EN Hi-Z)

// Diagnostics
uint32_t ledGetTaskHWM(); // uxTaskGetStackHighWaterMark for the LED task

// Public animation / mode API
void ledSetBootProgress(int step);
void ledSetWaitBt();
void ledSetIdle();
void ledSetPlaying();
void ledSetNightLight(int brightnessPercent);
void ledShowVolume(int volumeLevel);
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

#else // !ENABLE_LEDS — stub implementations

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
