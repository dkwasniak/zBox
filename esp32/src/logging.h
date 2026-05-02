#pragma once
#include <Arduino.h>
#define LOG(fmt, ...) Serial.printf("<%lu> " fmt, millis(), ##__VA_ARGS__)
#define LOGLN(msg) LOG(msg "\n")
