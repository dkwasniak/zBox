#include "buttons.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "buttons_isr.h"
#include "helpers.h"
#include "volume.h"
#include "leds.h"
#include "sleep.h"
#include "battery.h"
#include "playback.h"
#include "audio.h"
#include "persistent_log.h"
#include <SD.h>
#include <Arduino.h>

void handleButtons()
{
    static bool bothABHandled = false;
    static bool bothCDHandled = false;
    unsigned long now = millis();

    // Odczyt aktualnego surowego stanu
    bool down[BTN_COUNT];
    for (int i = 0; i < BTN_COUNT; i++)
    {
        down[i] = (digitalRead(buttons[i].pin) == LOW);
    }

    // Obsłuż flagi ISR - rozpocznij timing + fire krótkich akcji
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (b.pressed && b.pressStart == 0)
        {
            b.pressStart = now;
            b.longHandled = false;
            lastActivityMs = millis(); // reset idle timer przy każdym naciśnięciu
            // Akcje krótkie (natychmiast)
            switch (i)
            {
            case 0: // BTN_A
            case 1: // BTN_B
                LOG("[BTN] Short press: %s (no action)\n", b.name);
                break;
            case 2:
                volumeDown();
                break; // BTN_C
            case 3:
                volumeUp();
                break; // BTN_D
            }
        }
    }

    if (down[0] && down[1] && !bothABHandled &&
        buttons[0].pressStart > 0 && buttons[1].pressStart > 0)
    {
        unsigned long earliest = max(buttons[0].pressStart, buttons[1].pressStart);
        if (now - earliest >= LONG_PRESS_MS)
        {
            bothABHandled = true;
            LOGLN("\n>>> DIAGNOSTIC MODE");

            audioStop();

            File f = SD.open(DIAG_PENDING_PATH, FILE_WRITE);
            bool written = (bool)f;
            if (f) f.close();
            LOG(">>> Diagnostic flag written & verified: %d\n", written);

            delay(100);
            ESP.restart();
        }
    }

    // Combo: BTN_C + BTN_D trzymane LONG_PRESS_MS -> SYNC MODE.
    // Wymagamy pressStart>0 dla OBU - inaczej trzymanie BTN_D z hold-to-wake
    // (które omija ISR) + późniejsze BTN_C mogłyby fałszywie wejść w sync.
    if (down[2] && down[3] && !bothCDHandled &&
        buttons[2].pressStart > 0 && buttons[3].pressStart > 0)
    {
        unsigned long earliest = max(buttons[2].pressStart, buttons[3].pressStart);
        if (now - earliest >= LONG_PRESS_MS)
        {
            bothCDHandled = true;
            LOGLN("\n>>> SYNC MODE");

            playSystemSoundSync("sync");
            audioStop();

            {
                File f = SD.open("/data/sync_pending", FILE_WRITE);
                bool written = (bool)f;
                if (f) f.close();
                LOG(">>> Sync flag written & verified: %d\n", written);
            }

            delay(100);
            ESP.restart();
        }
    }

    // Długie BTN_C (bez BTN_D) -> deep sleep
    if (down[2] && !down[3] && buttons[2].pressStart > 0 &&
        now - buttons[2].pressStart >= LONG_PRESS_MS && !buttons[2].longHandled)
    {
        buttons[2].longHandled = true;
        LOGLN("\n>>> DEEP SLEEP");
        enterDeepSleep();
    }

    // Długie BTN_A (sam) -> sprawdź baterię: animacja LED
    if (down[0] && !down[1] && buttons[0].pressStart > 0 &&
        now - buttons[0].pressStart >= LONG_PRESS_MS && !buttons[0].longHandled)
    {
        buttons[0].longHandled = true;
        float v = readBatteryVoltage();
        int bars = batteryBars(v);
        PLOGF("[BAT] Voltage: %.2fV -> %d bar(s)", v, bars);
        ledShowBattery(bars);
        // Wyczyść lastNfcUid - jeśli figurka nadal stoi, NFC wznowi muzykę
        lastNfcUid[0] = '\0';
    }

    // Zwolnienie przycisków
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (!down[i] && b.pressed)
        {
            b.pressed = false;
            b.pressStart = 0;
            b.longHandled = false;
        }
    }
    // Flaga combo resetuje się gdy którykolwiek z C/D zostanie puszczony
    if (!down[0] || !down[1])
        bothABHandled = false;
    if (!down[2] || !down[3])
        bothCDHandled = false;
}
