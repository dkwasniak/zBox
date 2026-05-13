#pragma once

#include <stdint.h>
#include "app_state.h"

enum class WakeDecision : uint8_t {
    NONE = 0,
    NORMAL_BOOT = 1,
    NIGHT_LIGHT = 2,
};

WakeDecision handleWakeFromDeepSleep();

// Direct deep-sleep entry used by sync / service flows outside the normal
// dispatcher path.
void enterDeepSleep();
void enterEmergencyDeepSleep();

// Dispatcher executor: called by dispatcher when EnterDeepSleep effect fires.
// BT has already been shut down via btAdapterShutdown() before this is called.
void sleepExecuteDeepSleep(RequestedSleepKind kind);
