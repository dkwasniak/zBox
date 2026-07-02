#include "sleep.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "audio.h"
#include "nfc_module.h"
#include "leds.h"
#include "night_light.h"
#include "peripheral_power.h"
#include "musicbox_assert.h"
#include <esp_sleep.h>

// Used only by sync mode (outside normal dispatcher path).
void enterDeepSleep()
{
    LOGC("[SLEEP] enterDeepSleep (sync mode path)\n");
    audioDeleteTaskForSleep();
    delay(50);
    nfcStopTaskForSleep();
    nfcPrepareForPowerOff();
    nfcBusHiZForPowerOff();
    nfcPowerSwitchOff();
    ledSuspendTask();
    delay(20);
    ledShutdownAnim();
    ledPowerOff();
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

void sleepExecuteDeepSleep(RequestedSleepKind kind)
{
    gInSleepExecutorPath = true;

    LOGC("[SLEEP] Executing deep sleep (kind=%d, via dispatcher)\n", (int)kind);
    nightLightFlushPendingSave();
    audioDeleteTaskForSleep();
    delay(50);
    nfcStopTaskForSleep();
    nfcPrepareForPowerOff();
    nfcBusHiZForPowerOff();
    nfcPowerSwitchOff();
    ledSuspendTask();
    delay(20);
    ledShutdownAnim();
    ledPowerOff();
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

// Waking from deep sleep requires holding BTN_D for LONG_PRESS_MS.
// The ESP32 ext0 wakes immediately on detecting LOW, so "hold-to-wake"
// must be implemented in software: here we poll the button and go back
// to sleep if it is released too early. The LED animation (scaled to
// LED_COUNT) shows hold progress. This function must be called at the very
// beginning of setup(), BEFORE ledInit() (which starts the FreeRTOS task).
WakeDecision handleWakeFromDeepSleep()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
        return WakeDecision::NONE;

    // Hold BTN_D past WAKE_ABORT_MS to confirm boot.
    // Simultaneously holding BTN_C activates night-light mode.
    LOGC("[WAKE] BTN_D wake confirm: <%dms abort, >=%dms boot (+ BTN_C = night light)\n",
         WAKE_ABORT_MS, WAKE_ABORT_MS);

    pinMode(BTN_D, INPUT_PULLUP);
    pinMode(BTN_C, INPUT_PULLUP);

    // Minimal FastLED init without an animation task — ZERO FreeRTOS.
    // ledInit() will later detect fastLedInitialized=true and skip re-adding LEDs.
    ledPreInitHardware();
    ledClear();

    // Short debounce after wake-up. Too long artificially extends the required hold time.
    delay(25);

    const unsigned long holdStart = millis();
    while (true)
    {
        unsigned long elapsed = millis() - holdStart;

        if (elapsed >= WAKE_ABORT_MS)
        {
            if (digitalRead(BTN_C) == LOW)
            {
                LOGC("[WAKE] Night light combo confirmed (C+D)\n");
                ledSetNightLight(100);
                return WakeDecision::NIGHT_LIGHT;
            }
            LOGC("[WAKE] Hold confirmed for normal boot\n");
            return WakeDecision::NORMAL_BOOT;
        }

        bool pressed = (digitalRead(BTN_D) == LOW);
        if (!pressed)
        {
            delay(10);
            if (digitalRead(BTN_D) != LOW)
            {
                if (elapsed < WAKE_ABORT_MS)
                    break;
            }
        }

        int lit = (int)((elapsed * (unsigned long)LED_COUNT) / WAKE_ABORT_MS);
        if (lit < 1) lit = 1;
        if (lit > LED_COUNT) lit = LED_COUNT;
        ledSetWakeProgress(lit);
        delay(20);
    }

    // Released too early - silently go back to deep sleep.
    LOGC("[WAKE] Released too early - back to deep sleep\n");
    Serial.flush();
    nfcBusHiZForPowerOff();
    nfcPowerSwitchOff();
    nsPowerOff();
    ledPowerOff(); // clear + disable LED power supply
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
    return WakeDecision::NONE;
}
