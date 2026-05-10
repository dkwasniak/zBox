#include "jbl.h"
#include "zbox_config.h"
#include "logging.h"
#include <Arduino.h>

void jblPressButtonBlocking(int pin, int durationMs)
{
    digitalWrite(pin, HIGH);
    delay(durationMs);
    digitalWrite(pin, LOW);
}

bool isJblOn()
{
    int maxVal = 0;
    for (int i = 0; i < 5; i++)
    {
        int v = analogRead(JBL_STATUS);
        if (v > maxVal)
            maxVal = v;
        delayMicroseconds(200);
    }
    LOG("[JBL] Status ADC: %d\n", maxVal);
    return maxVal > JBL_STATUS_THRESHOLD;
}

// NOTE: JBL power-on at boot is done inline in setup() (non-blocking,
// pulse interleaved with NFC init). Runtime recovery (auto-power-off JBL after
// inactivity) is handled by ensureJblReady() in playback.cpp.

// Blocking power-off — used only before deep sleep
void jblPowerOff()
{
    if (!isJblOn())
    {
        LOGLN("[JBL] Already OFF");
        return;
    }
    LOGLN("[JBL] Powering OFF...");
    jblPressButtonBlocking(JBL_POWER, JBL_POWER_PRESS_MS);
    delay(500);
}
