#include "sleep.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "audio.h"
#include "nfc_module.h"
#include "jbl.h"
#include "leds.h"
#include "playback.h"
#include <esp_sleep.h>
#include <esp_bt.h>

void enterDeepSleep()
{
    LOGC("[SLEEP] Preparing for deep sleep\n");

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
void handleWakeFromDeepSleep()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
        return;

    LOGC("[WAKE] Hold BTN_D to confirm wake-up\n");

    pinMode(BTN_D, INPUT_PULLUP);

    // Minimalny init FastLED bez taska animacji — ZERO FreeRTOS.
    // ledInit() później wykryje fastLedInitialized=true i pominie ponowne addLeds.
    ledPreInitHardware();
    ledClear();

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
            // LEDy zostaną nadpisane przez ledInit()/ledSetBootProgress().
            LOGC("[WAKE] Hold confirmed - booting\n");
            return;
        }

        // Pasek postępu skalowany do dowolnej liczby diod.
        // Lerp od 1 do LED_COUNT w zależności od czasu trzymania.
        int lit = (int)((elapsed * (unsigned long)LED_COUNT) / LONG_PRESS_MS);
        if (lit < 1)
            lit = 1;
        if (lit > LED_COUNT)
            lit = LED_COUNT;
        ledSetWakeProgress(lit); // ciepłe pomarańczowe
        delay(20);
    }

    // Puszczony za wcześnie - cicho z powrotem do deep sleep.
    LOGC("[WAKE] Released too early - back to deep sleep\n");
    Serial.flush();
    ledPowerOff(); // clear + wyłącz zasilanie LEDów
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_D, LOW);
    esp_deep_sleep_start();
}
