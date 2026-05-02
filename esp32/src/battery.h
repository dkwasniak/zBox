#pragma once

struct BatteryReading {
    float adcPinVoltage;
    float batteryVoltage;
    int bars;
    const char *color;
};

// Odczyt napięcia baterii przez ADC (hardware only).
// batteryBars() jest inline w helpers.h — używany też przez buttons.cpp.
BatteryReading readBatteryReading();
float readBatteryVoltage();
