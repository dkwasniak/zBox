#pragma once

// Odczyt napięcia baterii przez ADC (hardware only).
// batteryBars() jest inline w helpers.h — używany też przez buttons.cpp.
float readBatteryVoltage();
