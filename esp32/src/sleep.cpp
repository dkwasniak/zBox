#include "sleep.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "audio.h"
#include "nfc_module.h"
#include "jbl.h"
#include "leds.h"
#include "night_light.h"
#include "playback.h"
#include <esp_sleep.h>
#include <esp_bt.h>

void enterDeepSleep()
{
    LOGC("[SLEEP] Preparing for deep sleep\n");
    nightLightFlushPendingSave();

    // 0. Zagraj dźwięk "sleep" przez BT (jeśli przypisany).
    playSystemSoundSync("power_off");

    // 1. Zabij audio task — zatrzymuje dekoder MP3, nie dotyka BT.
    audioDeleteTaskForSleep();
    delay(50);

    // 1a. Zatrzymaj NFC task przed PowerDown. Bez tego task potrafi równolegle
    // wykonywać readPassiveTargetID()/reinitNfc() na tym samym Software SPI.
    nfcStopTaskForSleep();

    // 1b. PN532 PowerDown — ~1mA zamiast ~100mA podczas snu.
    nfcPowerDown();

    // 2. NIE wywołuj end() ani disconnect().
    //    esp_a2d_disconnect() zawsze wywołuje esp_a2d_media_ctrl(STOP) →
    //    SUSPEND_STREAM_REQ → state Closing → btc_av_state_closing_handler
    //    unhandled → NULL deref → StoreProhibited.
    //
    //    esp_bt_controller_disable() operuje na poziomie radia (poniżej A2DP SM):
    //    A2DP state machine nie dostaje żadnego eventu disconnect.
    //    Musi być wywołane PRZED jblPowerOff() — fizyczne odłączenie JBL
    //    triggeruje bta_av_str_stopped → crash jeśli controller nadal aktywny.
    LOGC("[SLEEP] Disabling BT controller\n");
    esp_bt_controller_disable();
    delay(50);

    ledSuspendTask();
    delay(20);
    ledShutdownAnim();
    ledPowerOff();    // FastLED clear + wyłącz zasilanie LEDów

    // 3. JBL OFF — bezpieczne, BT controller już wyłączony, brak eventów.
    jblPowerOff();

    LOGC("[SLEEP] Entering deep sleep\n");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}

void enterEmergencyDeepSleep()
{
    LOGC("[SLEEP] Emergency deep sleep\n");

    pinMode(JBL_POWER, OUTPUT);
    digitalWrite(JBL_POWER, LOW);
    pinMode(JBL_STATUS, INPUT);

    int maxVal = 0;
    for (int i = 0; i < 5; i++)
    {
        int v = analogRead(JBL_STATUS);
        if (v > maxVal)
            maxVal = v;
        delayMicroseconds(200);
    }

    if (maxVal > JBL_STATUS_THRESHOLD)
    {
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);
        digitalWrite(JBL_POWER, LOW);
        delay(50);
    }

    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
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
