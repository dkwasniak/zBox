#pragma once
#include "shared_types.h"
#include "zbox_config.h"   // BTN_COUNT, DEBOUNCE_MS

extern Button buttons[BTN_COUNT];
void buttonsInit();
