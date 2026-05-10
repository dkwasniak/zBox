#pragma once
#include "events.h"
// implemented in test file
bool postEventFromTask(const Event& ev);
bool postEventFromIsr(const Event& ev, int* = nullptr);
