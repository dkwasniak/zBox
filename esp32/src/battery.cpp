#include "battery.h"
#include "musicbox_config.h"
#include "helpers.h"
#include "persistent_log.h"
#include <Arduino.h>

BatteryReading readBatteryReading()
{
    long sum = 0;
    for (int i = 0; i < 16; i++)
    {
        sum += analogReadMilliVolts(BAT_ADC_PIN);
        delayMicroseconds(100);
    }
    float vPin = (sum / 16.0f) / 1000.0f; // mV -> V na GPIO35 (VBAT/2)
    float vBat = vPin * BAT_ADC_SCALE;
    int bars = batteryBars(vBat);
    PLOGF("[BAT] ADC pin voltage: %.3fV", vPin);
    return {vPin, vBat, bars, batteryColorName(bars)};
}

float readBatteryVoltage()
{
    return readBatteryReading().batteryVoltage;
}
