#pragma once
#include <stdint.h>
#include "musicbox_config.h"

#if ENABLE_LEDS

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
void ledPowerOff();               // clear + show + wyłącz zasilanie (LED_EN Hi-Z)

// Diagnostyka
uint32_t ledGetTaskHWM(); // uxTaskGetStackHighWaterMark dla LED taska

// Publiczne API animacji / trybów
void ledSetBootProgress(int step);
void ledSetWaitBt();
void ledSetIdle();
void ledSetPlaying();
void ledShowVolume(int volumePercent);
void ledShowModeChange(bool musicMode);
void ledSetSleepReady();
void ledSetSyncWifi();
void ledSetSyncProgress(int current, int total);
void ledSetDiagnostic();
void ledFlashResult(bool success);
void ledFlashWarning();
void ledShutdownAnim();
void ledShowBattery(int bars);

#else // !ENABLE_LEDS — stubs

inline void ledPreInitHardware() {}
inline void ledInit() {}
inline uint32_t ledGetTaskHWM() { return 0; }
inline void ledSuspendTask() {}
inline void ledResumeTask() {}
inline void ledClear() {}
inline void ledSetWakeProgress(int) {}
inline void ledPowerOff() {}
inline void ledSetBootProgress(int) {}
inline void ledSetWaitBt() {}
inline void ledSetIdle() {}
inline void ledSetPlaying() {}
inline void ledShowVolume(int) {}
inline void ledShowModeChange(bool) {}
inline void ledSetSleepReady() {}
inline void ledSetSyncWifi() {}
inline void ledSetSyncProgress(int, int) {}
inline void ledSetDiagnostic() {}
inline void ledFlashResult(bool) {}
inline void ledFlashWarning() {}
inline void ledShutdownAnim() {}
inline void ledShowBattery(int) {}

#endif // ENABLE_LEDS
