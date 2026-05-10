#pragma once
#include "FreeRTOS.h"
using QueueHandle_t = void*;
inline QueueHandle_t xQueueCreate(int, int)                                    { return nullptr; }
inline int           xQueueReceive(QueueHandle_t, void*, int)                  { return 0; }
inline int           xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*){ return pdTRUE; }
