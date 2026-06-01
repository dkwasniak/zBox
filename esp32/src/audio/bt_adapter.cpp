#include "bt_adapter.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "dispatcher.h"
#include "audio.h"
#include "state.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"

namespace {

enum class BtCommandType : uint8_t { Start, Stop };

struct BtCommand {
    BtCommandType type;
    CmdId cmd_id;
};

static QueueHandle_t s_cmdQueue = nullptr;
static TaskHandle_t s_workerTask = nullptr;
static bool s_prevConnected = false;
static bool s_stopPending = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#ifdef NATIVE_BUILD
static bool s_testCmdPending = false;
static BtCommand s_testCmd{};
#endif

static Event makeBtFailEvent(EventType type, BtFailReason reason) {
    Event ev = makeEvent(type);
    ev.payload.bt_fail.reason = reason;
    return ev;
}

static void setPrevConnected(bool connected) {
    portENTER_CRITICAL(&s_mux);
    s_prevConnected = connected;
    portEXIT_CRITICAL(&s_mux);
}

static bool prevConnected() {
    portENTER_CRITICAL(&s_mux);
    const bool connected = s_prevConnected;
    portEXIT_CRITICAL(&s_mux);
    return connected;
}

static void setStopPending(bool pending) {
    portENTER_CRITICAL(&s_mux);
    s_stopPending = pending;
    portEXIT_CRITICAL(&s_mux);
}

static bool stopPending() {
    portENTER_CRITICAL(&s_mux);
    const bool pending = s_stopPending;
    portEXIT_CRITICAL(&s_mux);
    return pending;
}

static void postBtStopCompleted() {
    portENTER_CRITICAL(&s_mux);
    if (!s_stopPending) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_stopPending = false;
    s_prevConnected = false;
    portEXIT_CRITICAL(&s_mux);

    postEventFromTask(makeEvent(EventType::BtHeadphonesModeStopped));
    LOGI("[BT_ADAPTER] BtHeadphonesModeStopped\n");
}

static bool enqueueBtCommand(BtCommandType type, CmdId cmd_id) {
#ifdef NATIVE_BUILD
    if (s_testCmdPending) return false;
    s_testCmd = {type, cmd_id};
    s_testCmdPending = true;
    return true;
#else
    if (!s_cmdQueue) return false;
    const BtCommand cmd{type, cmd_id};
    return xQueueSend(s_cmdQueue, &cmd, 0) == pdTRUE;
#endif
}

static bool processBtCommand(TickType_t waitTicks) {
    BtCommand cmd{};
#ifdef NATIVE_BUILD
    (void)waitTicks;
    if (!s_testCmdPending) {
        return false;
    }
    cmd = s_testCmd;
    s_testCmdPending = false;
#else
    if (!s_cmdQueue || xQueueReceive(s_cmdQueue, &cmd, waitTicks) != pdTRUE) {
        return false;
    }
#endif

    const unsigned long startMs = millis();
    if (cmd.type == BtCommandType::Start) {
        setStopPending(false);
        LOGI("[BT_ADAPTER] start begin cmd_id=%u\n", (unsigned)cmd.cmd_id);
        if (!audioStartBtHeadphonesMode()) {
            LOGW("[BT_ADAPTER] start failed elapsed_ms=%lu\n", millis() - startMs);
            postEventFromTask(makeBtFailEvent(EventType::BtHeadphonesModeStartFailed,
                                             BtFailReason::StartFailed));
            return true;
        }

        const bool connected = audioBtHeadphonesAreConnected();
        setPrevConnected(connected);
        LOGI("[BT_ADAPTER] start end elapsed_ms=%lu connected=%d running=%d\n",
             millis() - startMs,
             (int)connected,
             (int)audioBtHeadphonesModeIsRunning());
        if (connected) {
            postEventFromTask(makeEvent(EventType::BtConnected));
            LOGI("[BT_ADAPTER] BtConnected (already connected on mode start)\n");
        }
        return true;
    }

    LOGI("[BT_ADAPTER] stop begin cmd_id=%u\n", (unsigned)cmd.cmd_id);
    audioStopBtHeadphonesMode();
    LOGI("[BT_ADAPTER] stop end elapsed_ms=%lu running=%d\n",
         millis() - startMs,
         (int)audioBtHeadphonesModeIsRunning());
    postBtStopCompleted();
    return true;
}

static void btWorkerTask(void*) {
    for (;;) {
        processBtCommand(portMAX_DELAY);
    }
}

} // namespace

void btAdapterInit() {
    setPrevConnected(audioBtHeadphonesAreConnected());
    setStopPending(false);
#ifdef NATIVE_BUILD
    s_testCmdPending = false;
    s_cmdQueue = xQueueCreate(4, sizeof(BtCommand));
#else
    if (!s_cmdQueue) {
        s_cmdQueue = xQueueCreate(4, sizeof(BtCommand));
    }
#endif
    if (s_cmdQueue && !s_workerTask) {
        const BaseType_t ok = xTaskCreatePinnedToCore(
            btWorkerTask, "btctrl", 4096, nullptr, 1, &s_workerTask, 0);
        if (ok != pdPASS) {
            LOGW("[BT_ADAPTER] worker task create failed\n");
            s_workerTask = nullptr;
        }
    }
}

void btAdapterPoll() {
    const bool running = audioBtHeadphonesModeIsRunning();
    const bool connected = running && audioBtHeadphonesAreConnected();

    if (stopPending()) {
        if (!running) {
            postBtStopCompleted();
        }
        return;
    }

    if (connected != prevConnected()) {
        setPrevConnected(connected);
        postEventFromTask(makeEvent(connected ? EventType::BtConnected : EventType::BtDisconnected));
        LOGI("[BT_ADAPTER] %s\n", connected ? "BtConnected" : "BtDisconnected");
    }
}

void btAdapterStartHeadphonesMode(CmdId cmd_id) {
    setStopPending(false);
    if (!enqueueBtCommand(BtCommandType::Start, cmd_id)) {
        LOGW("[BT_ADAPTER] start queue full/unavailable cmd_id=%u\n", (unsigned)cmd_id);
        postEventFromTask(makeBtFailEvent(EventType::BtHeadphonesModeStartFailed,
                                         BtFailReason::StartFailed));
        return;
    }
}

void btAdapterStopHeadphonesMode(CmdId cmd_id) {
    setStopPending(true);
    if (!enqueueBtCommand(BtCommandType::Stop, cmd_id)) {
        LOGW("[BT_ADAPTER] stop queue full/unavailable cmd_id=%u\n", (unsigned)cmd_id);
        setStopPending(false);
        postEventFromTask(makeBtFailEvent(EventType::BtHeadphonesModeStopFailed,
                                         BtFailReason::StopFailed));
    }
}

#ifdef NATIVE_BUILD
bool btAdapterProcessOneForTest() {
    return processBtCommand(0);
}
#endif
