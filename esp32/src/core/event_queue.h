#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "events.h"

static constexpr uint8_t DISPATCHER_QUEUE_DEPTH = 16;

extern QueueHandle_t g_dispatcherQueue;

// Drop policy for when queue is full
enum class EventDropPolicy : uint8_t { Critical, Coalescible, EdgeTriggered, Telemetry };

EventDropPolicy getEventDropPolicy(EventType type);

// Post from task context (xQueueSend). Restarts on Critical overflow.
bool postEventFromTask(const Event& ev);

// Post from ISR context (xQueueSendFromISR). Sets RTC flag on Critical overflow.
bool postEventFromIsr(const Event& ev, BaseType_t* pxHigherPriorityTaskWoken = nullptr);
