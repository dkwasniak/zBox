#include "battery.h"
#include "musicbox_config.h"
#include "persistent_log.h"
#include <Arduino.h>

float readBatteryVoltage()
{
    long sum = 0;
    for (int i = 0; i < 16; i++)
    {
        sum += analogReadMilliVolts(BAT_ADC_PIN);
        delayMicroseconds(100);
    }
    float vPin = (sum / 16.0f) / 1000.0f; // mV → V na pinie (VBAT/2)
    PLOGF("[BAT] ADC pin voltage: %.3fV", vPin);
    return vPin * 2.0f; // dzielnik 100k/100k na Lolin D32 Pro
}
