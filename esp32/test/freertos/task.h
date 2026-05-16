#pragma once
#include "FreeRTOS.h"

using TaskHandle_t = void*;

inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned int, void*, UBaseType_t, TaskHandle_t*, BaseType_t) {
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }

