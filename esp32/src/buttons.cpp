#include "buttons.h"
#include "zbox_config.h"
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
#include "night_light.h"
#include <SD.h>
#include <Arduino.h>

namespace {

constexpr int BTN_IDX_A = 0;
constexpr int BTN_IDX_B = 1;
constexpr int BTN_IDX_C = 2;
constexpr int BTN_IDX_D = 3;

void clearPendingClicks(Button &b)
{
    b.clickCount = 0;
    b.lastReleaseMs = 0;
}

void suppressButton(Button &b)
{
    b.clickSuppressed = true;
    clearPendingClicks(b);
}

void resolveButtonAction(int idx, uint8_t clicks)
{
    if (clicks == 0)
        return;

    if (runtimeIsNightLight())
    {
        switch (idx)
        {
        case BTN_IDX_C:
            nightLightDecrease();
            break;
        case BTN_IDX_D:
            nightLightIncrease();
            break;
        default:
            LOGI("[NIGHT] BTN_%c click ignored\n", 'A' + idx);
            break;
        }
        return;
    }

    switch (idx)
    {
    case BTN_IDX_A:
        if (!playbackIsMusicMode())
        {
            LOGI("[BTN] A click ignored in NFC mode\n");
            return;
        }
        if (clicks >= 2)
            playbackPrevTrack();
        else
            playbackTogglePlayPause();
        break;
    case BTN_IDX_B:
        if (!playbackIsMusicMode())
        {
            LOGI("[BTN] B click ignored in NFC mode\n");
            return;
        }
        if (clicks >= 2)
            playbackNextTrack();
        else
            LOGI("[BTN] B single click: no action\n");
        break;
    case BTN_IDX_C:
        volumeDown();
        break;
    case BTN_IDX_D:
        volumeUp();
        break;
    }
}

void finalizePendingClicks(Button &b, int idx, unsigned long now)
{
    if (b.clickCount == 0 || b.pressStart != 0)
        return;
    if (now - b.lastReleaseMs < DOUBLE_CLICK_WINDOW_MS)
        return;

    uint8_t clicks = b.clickCount;
    clearPendingClicks(b);
    resolveButtonAction(idx, clicks);
}

void markComboHandled(Button &a, Button &b)
{
    a.longHandled = true;
    b.longHandled = true;
    suppressButton(a);
    suppressButton(b);
}

} // namespace

void handleButtons()
{
    static bool bothABHandled = false;
    static bool cSleepReadyShown = false;
    static bool nightLightSleepHandled = false;
    unsigned long now = millis();

    // Odczyt aktualnego surowego stanu
    bool down[BTN_COUNT];
    for (int i = 0; i < BTN_COUNT; i++)
    {
        down[i] = (digitalRead(buttons[i].pin) == LOW);
    }

    // Obsłuż nowe wciśnięcia
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (down[i] && b.pressed && b.pressStart == 0)
        {
            b.pressStart = now;
            b.longHandled = false;
            b.clickSuppressed = false;
            if (!runtimeIsNightLight())
                lastActivityMs = millis(); // reset idle timer przy każdym naciśnięciu
        }
    }

    if (runtimeIsNightLight())
    {
        if (down[BTN_IDX_C] && buttons[BTN_IDX_C].pressStart > 0 &&
            now - buttons[BTN_IDX_C].pressStart >= NIGHT_LIGHT_SLEEP_HOLD_MS &&
            !nightLightSleepHandled)
        {
            nightLightSleepHandled = true;
            buttons[BTN_IDX_C].longHandled = true;
            suppressButton(buttons[BTN_IDX_C]);
            LOGC("[NIGHT] BTN_C hold -> deep sleep\n");
            enterDeepSleep();
        }
    }
    else if (down[0] && down[1] && !bothABHandled &&
             buttons[0].pressStart > 0 && buttons[1].pressStart > 0)
    {
        unsigned long earliest = max(buttons[0].pressStart, buttons[1].pressStart);
        if (now - earliest >= LONG_PRESS_MS)
        {
            bothABHandled = true;
            markComboHandled(buttons[0], buttons[1]);
            LOGC(">>> DIAGNOSTIC MODE\n");

            audioStop();

            File f = SD.open(DIAG_PENDING_PATH, FILE_WRITE);
            bool written = (bool)f;
            if (f) f.close();
            if (!written)
                LOGE("[DIAG] Failed to write diagnostic flag\n");
            LOGC("[DIAG] Diagnostic flag written=%d\n", written);
            ESP.restart();
        }
    }

    // Bardzo długie BTN_C (bez BTN_D) -> awaryjny deep sleep.
    // Normalny deep sleep dla BTN_C odpalamy dopiero po puszczeniu, żeby
    // przytrzymanie mogło dojść do progu emergency.
    if (!runtimeIsNightLight() && down[2] && !down[3] && buttons[2].pressStart > 0 &&
        now - buttons[2].pressStart >= EMERGENCY_SLEEP_MS && !buttons[2].longHandled)
    {
        buttons[2].longHandled = true;
        suppressButton(buttons[2]);
        LOGC(">>> EMERGENCY DEEP SLEEP\n");
        enterEmergencyDeepSleep();
    }

    if (!runtimeIsNightLight() && down[2] && !down[3] && buttons[2].pressStart > 0 &&
        now - buttons[2].pressStart >= LONG_PRESS_MS && !cSleepReadyShown)
    {
        cSleepReadyShown = true;
        buttons[2].clickSuppressed = true;
        LOGI("[SLEEP] Release BTN_C now for normal deep sleep; keep holding for emergency\n");
        ledSetSleepReady();
    }

    // Długie BTN_A (sam) -> sprawdź baterię: animacja LED
    if (!runtimeIsNightLight() && down[0] && !down[1] && buttons[0].pressStart > 0 &&
        now - buttons[0].pressStart >= LONG_PRESS_MS && !buttons[0].longHandled)
    {
        buttons[0].longHandled = true;
        suppressButton(buttons[0]);
        float v = readBatteryVoltage();
        int bars = batteryBars(v);
        LOGI("[BAT] Voltage: %.2fV -> %d bar(s)\n", v, bars);
        ledShowBattery(bars);
        // Wyczyść lastNfcUid - jeśli figurka nadal stoi, NFC wznowi muzykę
        lastNfcUid[0] = '\0';
    }

    if (!runtimeIsNightLight() && down[1] && !down[0] && buttons[1].pressStart > 0 &&
        now - buttons[1].pressStart >= LONG_PRESS_MS && !buttons[1].longHandled)
    {
        buttons[1].longHandled = true;
        suppressButton(buttons[1]);
        LOGI("[MODE] Toggle requested by long press B\n");
        playbackToggleMode();
    }

    // Zwolnienie przycisków
    for (int i = 0; i < BTN_COUNT; i++)
    {
        Button &b = buttons[i];
        if (!down[i] && b.pressStart > 0)
        {
            unsigned long pressDuration = now - b.pressStart;

            if (!runtimeIsNightLight() && i == BTN_IDX_C && !down[BTN_IDX_D] &&
                pressDuration >= LONG_PRESS_MS && !b.longHandled)
            {
                b.longHandled = true;
                LOGC(">>> DEEP SLEEP\n");
                enterDeepSleep();
            }

            if (!b.longHandled && !b.clickSuppressed && pressDuration < LONG_PRESS_MS)
            {
                if (i == BTN_IDX_A || i == BTN_IDX_B)
                {
                    b.clickCount = (b.clickCount >= 2) ? 2 : (uint8_t)(b.clickCount + 1);
                    b.lastReleaseMs = now;
                }
                else
                {
                    resolveButtonAction(i, 1);
                }
            }
            else if (i == BTN_IDX_A || i == BTN_IDX_B)
            {
                clearPendingClicks(b);
            }

            b.pressed = false;
            b.pressStart = 0;
            b.longHandled = false;
            b.clickSuppressed = false;
            if (i == 2)
            {
                cSleepReadyShown = false;
                nightLightSleepHandled = false;
            }
        }
    }

    finalizePendingClicks(buttons[BTN_IDX_A], BTN_IDX_A, now);
    finalizePendingClicks(buttons[BTN_IDX_B], BTN_IDX_B, now);

    // Flaga combo resetuje się gdy którykolwiek z C/D zostanie puszczony
    if (!down[0] || !down[1])
        bothABHandled = false;
}
