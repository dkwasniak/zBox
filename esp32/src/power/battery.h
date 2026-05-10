#pragma once

struct BatteryReading {
    float adcPinVoltage;
    float batteryVoltage;
    int bars;
    const char *color;
};

// Read battery voltage via ADC (hardware only).
// batteryBars() is inline in helpers.h — also used by buttons.cpp.
BatteryReading readBatteryReading();
float readBatteryVoltage();
