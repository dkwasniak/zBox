#pragma once
using BaseType_t = int;
static const BaseType_t pdFALSE = 0;
static const BaseType_t pdTRUE  = 1;
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(x) (void)(x)
#define IRAM_ATTR
