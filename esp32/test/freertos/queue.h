#pragma once
#include "FreeRTOS.h"

using QueueHandle_t = void*;

inline QueueHandle_t xQueueCreate(unsigned int, unsigned int) { return reinterpret_cast<QueueHandle_t>(1); }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t* woken) {
    if (woken) *woken = pdFALSE;
    return pdTRUE;
}

