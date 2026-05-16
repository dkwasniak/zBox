#pragma once
#include <Arduino.h>
#include "zbox_config.h"

enum class RuntimeSessionMode : uint8_t {
    NORMAL = 0,
    NIGHT_LIGHT = 1,
};

// Only truly shared globals (read/written by multiple modules)

extern volatile bool g_beatDetected;
extern volatile uint8_t g_audioEnergy;  // 0-255, current audio energy (for LED)
extern bool sdReady;
extern unsigned long bootStart;

RuntimeSessionMode runtimeGetSessionMode();
void runtimeSetSessionMode(RuntimeSessionMode mode);
bool runtimeIsNightLight();
