#pragma once

using BaseType_t = int;
using TickType_t = unsigned int;
using UBaseType_t = unsigned int;

static const BaseType_t pdFALSE = 0;
static const BaseType_t pdTRUE = 1;
static const BaseType_t pdPASS = 1;

#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(x) (void)(x)
#define portMAX_DELAY 0xffffffffu
#define IRAM_ATTR

using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)

