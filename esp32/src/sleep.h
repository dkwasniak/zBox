#pragma once

#include <stdint.h>

enum class WakeDecision : uint8_t {
    NONE = 0,
    NORMAL_BOOT = 1,
    NIGHT_LIGHT = 2,
};

void enterDeepSleep();
void enterEmergencyDeepSleep();
WakeDecision handleWakeFromDeepSleep();
