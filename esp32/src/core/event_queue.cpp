#include "event_queue.h"
#include "logging.h"
#include <esp_system.h>

QueueHandle_t g_dispatcherQueue = nullptr;

EventDropPolicy getEventDropPolicy(EventType type) {
    switch (type) {
        case EventType::SleepRequested:
        case EventType::BtHeadphonesModeStopped:
            return EventDropPolicy::Critical;

        case EventType::IdleTimeoutFired:
        case EventType::NightLightTimeoutFired:
        case EventType::VolumeOverlayExpired:
        case EventType::BatteryPreviewExpired:
        case EventType::BrightnessSaveDeadlineFired:
        case EventType::SleepTimeoutFired:
            return EventDropPolicy::Coalescible;

        case EventType::NfcTagDetected:
        case EventType::BtConnected:
        case EventType::BtDisconnected:
            return EventDropPolicy::EdgeTriggered;

        case EventType::SyncModeEntered:
            return EventDropPolicy::Telemetry;

        default:
            return EventDropPolicy::Coalescible;
    }
}

bool postEventFromTask(const Event& ev) {
    if (!g_dispatcherQueue) return false;
    if (xQueueSend(g_dispatcherQueue, &ev, 0) == pdTRUE) return true;

    auto policy = getEventDropPolicy(ev.type);
    if (policy == EventDropPolicy::Critical) {
        LOGC("[QUEUE] Critical event dropped: %d — restarting\n", (int)ev.type);
        esp_restart();
    } else if (policy != EventDropPolicy::Telemetry) {
        LOGW("[QUEUE] Event dropped (queue full): %d\n", (int)ev.type);
    }
    return false;
}

bool postEventFromIsr(const Event& ev, BaseType_t* pxHigherPriorityTaskWoken) {
    if (!g_dispatcherQueue) return false;
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(g_dispatcherQueue, &ev, &woken) == pdTRUE) {
        if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = woken;
        return true;
    }
    // Cannot call LOGC or esp_restart from ISR context.
    // If critical: WDT (15s) will fire and produce a restart with the event context
    // available from the crash backtrace. An RTC flag mechanism would go here.
    return false;
}
