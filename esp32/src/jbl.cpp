#include "jbl.h"
#include "musicbox_config.h"
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

// UWAGA: włączanie JBL przy boot jest robione inline w setup() (nieblokująco,
// puls interleaved z NFC init). Runtime recovery (auto-power-off JBL po
// bezczynności) obsługuje ensureJblReady() w playback.cpp.

// Blokujące wyłączanie - używane tylko przed deep sleep
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
