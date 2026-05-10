#include "sleep.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "audio.h"
#include "nfc_module.h"
#include "jbl.h"
#include "leds.h"
#include "night_light.h"
#include "musicbox_assert.h"
#include <esp_sleep.h>
#include <esp_bt.h>

// Used only by sync mode (outside normal dispatcher path).
void enterDeepSleep()
{
    LOGC("[SLEEP] enterDeepSleep (sync mode path)\n");
    audioDeleteTaskForSleep();
    delay(50);
    nfcStopTaskForSleep();
    nfcPowerDown();
    esp_bt_controller_disable();
    delay(50);
    ledSuspendTask();
    delay(20);
    ledShutdownAnim();
    ledPowerOff();
    jblPowerOff();
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

void enterEmergencyDeepSleep()
{
    LOGC("[SLEEP] enterEmergencyDeepSleep (sync mode path)\n");
    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);
    int maxVal = 0;
    for (int i = 0; i < 5; i++) {
        int v = analogRead(JBL_STATUS);
        if (v > maxVal) maxVal = v;
        delayMicroseconds(200);
    }
    if (maxVal > JBL_STATUS_THRESHOLD) {
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);
        digitalWrite(JBL_POWER, LOW);
        delay(50);
    }
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

void sleepExecuteDeepSleep(RequestedSleepKind kind)
{
    gInSleepExecutorPath = true;

    switch (kind) {
        case RequestedSleepKind::Emergency:
            LOGC("[SLEEP] Executing deep sleep (emergency, via dispatcher)\n");
            // BT already disabled by btAdapterShutdown()
            audioDeleteTaskForSleep();            // MUST
            if (nfcReady) nfcPowerDown();         // conditional
            ledPowerOff();                        // best-effort, no animation
            jblPowerOff();                        // MUST
            Serial.flush();
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
            esp_deep_sleep_start();
            break;

        case RequestedSleepKind::NightLightTimeout:
            LOGC("[SLEEP] Executing deep sleep (night-light timeout, via dispatcher)\n");
            nightLightFlushPendingSave();         // Step 1, best-effort
            // BT already handled (Disabled in NL mode, btAdapterShutdown no-ops)
            audioDeleteTaskForSleep();            // Step 3, conditional
            delay(50);
            nfcStopTaskForSleep();                // Step 4a
            nfcPowerDown();                       // Step 4b
            ledSuspendTask();                     // Step 6a
            delay(20);
            ledShutdownAnim();                    // Step 6b
            ledPowerOff();                        // Step 6c
            jblPowerOff();                        // Step 7, conditional (safe if already off)
            Serial.flush();
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
            esp_deep_sleep_start();
            break;

        case RequestedSleepKind::Normal:
        default:
            LOGC("[SLEEP] Executing deep sleep (normal, via dispatcher)\n");
            nightLightFlushPendingSave();         // Step 1, best-effort
            // Step 2 (power_off sound) already played by reducer state machine
            // BT (Step 5) already disabled by btAdapterShutdown()
            audioDeleteTaskForSleep();            // Step 3, MUST
            delay(50);
            nfcStopTaskForSleep();                // Step 4a
            nfcPowerDown();                       // Step 4b
            ledSuspendTask();                     // Step 6a
            delay(20);
            ledShutdownAnim();                    // Step 6b
            ledPowerOff();                        // Step 6c
            jblPowerOff();                        // Step 7, MUST
            Serial.flush();
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
            esp_deep_sleep_start();
            break;
    }
}

// Wybudzanie z deep sleep wymaga przytrzymania BTN_D przez LONG_PRESS_MS.
// ESP32 ext0 wybudza się natychmiast po wykryciu LOW, więc "hold-to-wake"
// musi być zaimplementowane w software: tu odpytujemy przycisk i wracamy
// do snu jeśli zostanie puszczony za wcześnie. Animacja LED (skalowana do
// LED_COUNT) pokazuje postęp przytrzymania. Funkcja musi być wywołana na
// samym początku setup(), PRZED ledInit() (które uruchamia FreeRTOS task).
WakeDecision handleWakeFromDeepSleep()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
        return WakeDecision::NONE;

    LOGC("[WAKE] BTN_D wake confirm: <1s abort, 1-2s boot, >=2s night light\n");

    pinMode(BTN_D, INPUT_PULLUP);

    // Minimalny init FastLED bez taska animacji — ZERO FreeRTOS.
    // ledInit() później wykryje fastLedInitialized=true i pominie ponowne addLeds.
    ledPreInitHardware();
    ledClear();

    // Krótki debounce po wybudzeniu. Zbyt długi sztucznie wydłuża wymagany hold.
    delay(25);

    const unsigned long holdStart = millis();
    while (true)
    {
        unsigned long elapsed = millis() - holdStart;
        if (elapsed >= WAKE_NIGHT_LIGHT_MS)
        {
            LOGC("[WAKE] Night light hold confirmed\n");
            ledSetNightLight(100);
            return WakeDecision::NIGHT_LIGHT;
        }

        bool pressed = (digitalRead(BTN_D) == LOW);
        if (!pressed)
        {
            // Potwierdź zwolnienie po krótkim opóźnieniu (debounce)
            delay(10);
            if (digitalRead(BTN_D) != LOW)
            {
                if (elapsed < WAKE_ABORT_MS)
                    break;

                LOGC("[WAKE] Hold confirmed for normal boot\n");
                return WakeDecision::NORMAL_BOOT;
            }
        }

        if (elapsed < WAKE_ABORT_MS)
        {
            int lit = (int)((elapsed * (unsigned long)LED_COUNT) / WAKE_ABORT_MS);
            if (lit < 1)
                lit = 1;
            if (lit > LED_COUNT)
                lit = LED_COUNT;
            ledSetWakeProgress(lit);
        }
        else
        {
            // Po przekroczeniu progu normalnego bootu nie pokazuj jeszcze koloru lampki.
            // Pełny pomarańcz lampki zapala się dopiero po osiągnięciu progu NIGHT_LIGHT.
            ledSetWakeProgress(LED_COUNT);
        }
        delay(20);
    }

    // Puszczony za wcześnie - cicho z powrotem do deep sleep.
    LOGC("[WAKE] Released too early - back to deep sleep\n");
    Serial.flush();
    ledPowerOff(); // clear + wyłącz zasilanie LEDów
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
    return WakeDecision::NONE;
}
